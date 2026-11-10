#!/usr/bin/env python3
"""Render the compositing showcase from the CM3D-resolved stack.

The JSON is produced by `composite_stack_showcase` and contains, for every
frame, the *real* `resolveCompositeStack` result in Stack and Depth mode, plus
the camera and layer anchors. This script is only a presentation layer: it
paints the layers in exactly the emitted order, so the visual difference in the
split-screen video is the resolved order and nothing else.

Output: three 1920x1080 H.264 MP4s, 8 s at 30 fps, plus a contact sheet.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

WIDTH = 1920
HEIGHT = 1080
FPS = 30

BG_TOP = np.array([6, 9, 20], dtype=np.float32)
BG_BOTTOM = np.array([16, 28, 52], dtype=np.float32)
ACCENT = (86, 226, 202)
WARM = (255, 158, 88)
TEXT_COLOR = (246, 248, 255)
MUTED = (148, 166, 192)
DARK = (10, 17, 31)

FONT_BOLD = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
FONT_REGULAR = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")

_FONTS: dict[tuple[int, bool], ImageFont.FreeTypeFont] = {}
_BACKGROUNDS: dict[tuple[int, int], Image.Image] = {}


def font(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    key = (size, bold)
    if key not in _FONTS:
        path = FONT_BOLD if bold else FONT_REGULAR
        _FONTS[key] = ImageFont.truetype(str(path), size)
    return _FONTS[key]


def background(width: int, height: int) -> Image.Image:
    key = (width, height)
    if key in _BACKGROUNDS:
        return _BACKGROUNDS[key].copy()

    yy, xx = np.mgrid[0:height, 0:width]
    t = (yy / max(height - 1, 1)).astype(np.float32)
    arr = BG_TOP[None, None, :] + (BG_BOTTOM - BG_TOP)[None, None, :] * t[..., None]

    nx = (xx - width * 0.5) / (width * 0.62)
    ny = (yy - height * 0.55) / (height * 0.72)
    glow = np.clip(1.0 - np.sqrt(nx * nx + ny * ny), 0.0, 1.0) ** 2
    arr = arr + glow[..., None] * np.array([18, 60, 66], dtype=np.float32)

    vx = (xx - width * 0.5) / (width * 0.62)
    vy = (yy - height * 0.5) / (height * 0.62)
    vignette = np.clip(1.0 - 0.45 * np.sqrt(vx * vx + vy * vy), 0.0, 1.0)
    arr = arr * vignette[..., None]

    image = Image.fromarray(np.uint8(np.clip(arr, 0, 255)), "RGB")
    _BACKGROUNDS[key] = image
    return image.copy()


class Camera:
    def __init__(self, spec: dict, width: int, height: int) -> None:
        self.position = np.array(spec["position"], dtype=float)
        self.target = np.array(spec["target"], dtype=float)
        self.width = width
        self.height = height
        forward = self.target - self.position
        forward /= max(np.linalg.norm(forward), 1e-9)
        right = np.cross(forward, np.array([0.0, 1.0, 0.0]))
        if np.linalg.norm(right) < 1e-6:
            right = np.array([1.0, 0.0, 0.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, forward)
        self.forward = forward
        self.right = right
        self.up = up
        self.focal = (height * 0.5) / math.tan(math.radians(float(spec["fov"])) * 0.5)

    def project(self, point) -> tuple[float, float, float] | None:
        rel = np.asarray(point, dtype=float) - self.position
        x = float(rel @ self.right)
        y = float(rel @ self.up)
        z = float(rel @ self.forward)
        if z <= 0.05:
            return None
        return (
            self.width * 0.5 + x * self.focal / z,
            self.height * 0.5 - y * self.focal / z,
            z,
        )

    def project_many(self, points: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        rel = points - self.position
        x = rel @ self.right
        y = rel @ self.up
        z = rel @ self.forward
        safe = np.where(z > 0.05, z, 1.0)
        sx = self.width * 0.5 + x * self.focal / safe
        sy = self.height * 0.5 - y * self.focal / safe
        return np.stack([sx, sy, z], axis=1), z > 0.05


CUBE_VERTS = np.array(
    [
        (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
    ],
    dtype=float,
)
CUBE_FACES = [
    ([0, 1, 2, 3], np.array([0.0, 0.0, -1.0])),
    ([4, 5, 6, 7], np.array([0.0, 0.0, 1.0])),
    ([0, 4, 7, 3], np.array([-1.0, 0.0, 0.0])),
    ([1, 5, 6, 2], np.array([1.0, 0.0, 0.0])),
    ([3, 7, 6, 2], np.array([0.0, 1.0, 0.0])),
    ([0, 4, 5, 1], np.array([0.0, -1.0, 0.0])),
]


def rotation_matrix(yaw: float, pitch: float) -> np.ndarray:
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    ry = np.array([[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]])
    rx = np.array([[1.0, 0.0, 0.0], [0.0, cp, -sp], [0.0, sp, cp]])
    return ry @ rx


def draw_grid(canvas: Image.Image, camera: Camera) -> None:
    overlay = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    floor = -2.15
    for x in range(-10, 11):
        p0 = camera.project((x, floor, -12.0))
        p1 = camera.project((x, floor, 7.0))
        if p0 and p1:
            draw.line([p0[:2], p1[:2]], fill=(74, 122, 162, 34), width=2)
    for z in range(-12, 8):
        p0 = camera.project((-10.0, floor, z))
        p1 = camera.project((10.0, floor, z))
        if p0 and p1:
            draw.line([p0[:2], p1[:2]], fill=(74, 122, 162, 26), width=2)
    canvas.alpha_composite(overlay)


def draw_object_layer(canvas: Image.Image, camera: Camera, sample: dict, props: dict) -> None:
    centers = {int(layer["id"]): np.array(layer["world"], dtype=float) for layer in sample["layers"]}
    center = centers[1]
    seconds = float(sample["seconds"])
    size = float(props["object_size"])
    half = size * 0.5

    projected, visible = camera.project_many(
        center[None, :] + (CUBE_VERTS * half) @ rotation_matrix(seconds * 0.85, 0.36 + 0.22 * math.sin(seconds * 0.9)).T
    )
    if visible.mean() < 0.5:
        return

    depth = camera.project(center)
    if depth is None:
        return

    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(shadow)
    ground = camera.project((center[0], -2.05, center[2]))
    if ground:
        radius = max(28.0, size * 0.62 * camera.focal / max(depth[2], 0.1))
        shadow_draw.ellipse(
            [ground[0] - radius, ground[1] - radius * 0.24,
             ground[0] + radius, ground[1] + radius * 0.24],
            fill=(0, 0, 0, 135),
        )
        shadow = shadow.filter(ImageFilter.GaussianBlur(18))
        canvas.alpha_composite(shadow)

    rotation = rotation_matrix(seconds * 0.85, 0.36 + 0.22 * math.sin(seconds * 0.9))
    light = np.array([-0.32, 0.84, 0.44])
    light /= np.linalg.norm(light)

    faces = []
    for indices, normal in CUBE_FACES:
        world_normal = rotation @ normal
        to_camera = camera.position - center
        to_camera /= max(np.linalg.norm(to_camera), 1e-9)
        if float(world_normal @ to_camera) <= 0.02:
            continue
        points = [projected[index] for index in indices]
        if any(not visible[index] for index in indices):
            continue
        faces.append((float(np.mean([p[2] for p in points])), world_normal, points))

    faces.sort(key=lambda item: item[0], reverse=True)

    overlay = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    for _, world_normal, points in faces:
        shade = 0.34 + 0.66 * max(0.0, float(world_normal @ light))
        to_camera = camera.position - center
        to_camera /= max(np.linalg.norm(to_camera), 1e-9)
        rim = (1.0 - max(0.0, float(world_normal @ to_camera))) ** 2
        base = np.array([44, 196, 188], dtype=float) * shade
        base = base + np.array([255, 136, 76], dtype=float) * (0.42 * rim)
        color = tuple(
            int(max(0, min(255, round(float(channel)))))
            for channel in np.asarray(base, dtype=float).reshape(-1)[:3]
        ) + (255,)
        polygon = [(float(p[0]), float(p[1])) for p in points]
        draw.polygon(polygon, fill=color)
        draw.line(polygon + [polygon[0]], fill=(205, 255, 248, 95), width=2)

    # A soft specular streak on the most camera-facing face.
    if faces:
        _, world_normal, points = max(
            faces, key=lambda item: float(item[1] @ (camera.position - center) / max(np.linalg.norm(camera.position - center), 1e-9))
        )
        polygon = [(float(p[0]), float(p[1])) for p in points]
        cx = sum(p[0] for p in polygon) / len(polygon)
        cy = sum(p[1] for p in polygon) / len(polygon)
        highlight = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        hd = ImageDraw.Draw(highlight)
        hd.ellipse([cx - 34, cy - 18, cx + 34, cy + 18], fill=(220, 255, 250, 52))
        highlight = highlight.filter(ImageFilter.GaussianBlur(16))
        overlay.alpha_composite(highlight)

    canvas.alpha_composite(overlay)


def tracked_width(draw: ImageDraw.ImageDraw, text: str, fnt: ImageFont.FreeTypeFont, tracking: float) -> float:
    return sum(draw.textlength(ch, font=fnt) for ch in text) + tracking * max(0, len(text) - 1)


def tracked_text(
    draw: ImageDraw.ImageDraw,
    center_x: float,
    center_y: float,
    text: str,
    fnt: ImageFont.FreeTypeFont,
    fill,
    tracking: float,
    anchor: str = "lm",
) -> None:
    total = tracked_width(draw, text, fnt, tracking)
    x = center_x - total * 0.5
    for char in text:
        draw.text((x, center_y), char, font=fnt, fill=fill, anchor=anchor)
        x += draw.textlength(char, font=fnt) + tracking


def draw_text_layer(canvas: Image.Image, camera: Camera, sample: dict, props: dict) -> None:
    centers = {int(layer["id"]): np.array(layer["world"], dtype=float) for layer in sample["layers"]}
    projected = camera.project(centers[2])
    if projected is None:
        return
    center_x, center_y, depth = projected
    width = float(props["text_width"]) * camera.focal / max(depth, 0.1)
    height = float(props["text_height"]) * camera.focal / max(depth, 0.1)
    if width < 80 or height < 28:
        return

    fit = min(1.0, (canvas.width * 0.94) / width)
    width *= fit
    height *= fit
    pad = max(36, int(height * 0.28))
    image_w = max(64, int(width) + pad * 2)
    image_h = max(48, int(height) + pad * 2)

    layer = Image.new("RGBA", (image_w, image_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    main = "CHRONON"
    sub = "MOTION 3D"

    main_size = max(24, int(height * 0.48))
    while main_size > 18:
        fnt = font(main_size, True)
        if tracked_width(draw, main, fnt, main_size * 0.035) <= image_w * 0.88:
            break
        main_size = int(main_size * 0.94)
    main_font = font(main_size, True)
    sub_font = font(max(14, int(main_size * 0.30)), True)

    main_y = image_h * 0.44
    sub_y = image_h * 0.76
    tracked_text(draw, image_w * 0.5, main_y, main, main_font, TEXT_COLOR, main_size * 0.035, "lm")
    tracked_text(draw, image_w * 0.5, sub_y, sub, sub_font, ACCENT, max(2.0, main_size * 0.10), "lm")

    alpha = layer.getchannel("A").filter(ImageFilter.GaussianBlur(max(5, main_size // 7)))
    glow = Image.new("RGBA", (image_w, image_h), ACCENT + (0,))
    glow.putalpha(alpha.point(lambda value: int(value * 0.52)))
    layer = Image.alpha_composite(glow, layer)

    canvas.alpha_composite(layer, (int(center_x - image_w * 0.5), int(center_y - image_h * 0.5)))


def render_scene(sample: dict, order: list[int], width: int, height: int, props: dict) -> tuple[Image.Image, Camera]:
    canvas = background(width, height).convert("RGBA")
    camera = Camera(sample["camera"], width, height)
    draw_grid(canvas, camera)
    for layer_id in order:
        if int(layer_id) == 1:
            draw_object_layer(canvas, camera, sample, props)
        elif int(layer_id) == 2:
            draw_text_layer(canvas, camera, sample, props)
    return canvas, camera


def depth_map(sample: dict) -> dict[int, float]:
    return {int(layer["id"]): float(layer["depth"]) for layer in sample["layers"]}


LABELS = {1: "OBJECT", 2: "CHRONON"}


def draw_full_hud(canvas: Image.Image, sample: dict, order: list[int], frame: int, total: int,
                  mode: str, flip: bool) -> None:
    width, height = canvas.size
    hud = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(hud)

    draw.rectangle([64, 54, 70, 98], fill=ACCENT + (255,))
    draw.text((86, 55), "CHRONONMOTION3D", font=font(33, True), fill=TEXT_COLOR + (255,))
    draw.text((86, 97), "compositing order  ·  resolved in CM3D", font=font(19, False), fill=MUTED + (255,))

    badge_color = ACCENT if mode == "depth" else MUTED
    draw.rounded_rectangle([width - 438, 50, width - 64, 138], radius=16,
                           fill=(11, 20, 35, 222), outline=badge_color + (255,), width=2)
    draw.text((width - 416, 66), "DEPTH SORT" if mode == "depth" else "STACK",
              font=font(27, True), fill=badge_color + (255,))
    draw.text((width - 416, 103), "camera-space layer order" if mode == "depth" else "authored layer order",
              font=font(16, False), fill=MUTED + (255,))

    panel_top = height - 192
    draw.rounded_rectangle([64, panel_top, width - 64, height - 58], radius=18,
                           fill=(8, 14, 27, 216), outline=(72, 108, 142, 120), width=1)
    draw.text((94, panel_top + 18), "RESOLVED ORDER   BACK → FRONT", font=font(17, True), fill=MUTED + (255,))

    depths = depth_map(sample)
    chip_w, chip_h = 302, 62
    x = 94
    chip_y = panel_top + 48
    for index, layer_id in enumerate(order):
        near = index == len(order) - 1
        fill = (17, 64, 66, 235) if near else (20, 32, 50, 228)
        outline = ACCENT + (255,) if near else (74, 106, 138, 120)
        draw.rounded_rectangle([x, chip_y, x + chip_w, chip_y + chip_h], radius=12,
                               fill=fill, outline=outline, width=2)
        draw.text((x + 15, chip_y + 11), str(index + 1), font=font(18, True),
                  fill=(ACCENT if near else MUTED) + (255,))
        draw.text((x + 43, chip_y + 8), LABELS[int(layer_id)], font=font(21, True),
                  fill=TEXT_COLOR + (255,))
        draw.text((x + 43, chip_y + 35), f"depth {depths[int(layer_id)]:6.2f}", font=font(15, False),
                  fill=MUTED + (255,))
        if near:
            draw.text((x + chip_w - 14, chip_y + 10), "NEAR", font=font(13, True),
                      fill=ACCENT + (255,), anchor="ra")
        x += chip_w
        if index + 1 < len(order):
            draw.text((x + 20, chip_y + chip_h * 0.5), "→", font=font(25, True),
                      fill=MUTED + (255,), anchor="mm")
            x += 44

    draw.text((width - 94, height - 92), f"FRAME {frame:03d} / {total - 1:03d}",
              font=font(16, False), fill=MUTED + (255,), anchor="ra")
    draw.rectangle([64, height - 38, width - 64, height - 34], fill=(30, 46, 66, 255))
    draw.rectangle([64, height - 38, 64 + (width - 128) * (frame / max(total - 1, 1)), height - 34],
                   fill=ACCENT + (255,))

    if flip:
        draw.rounded_rectangle([width * 0.5 - 142, 45, width * 0.5 + 142, 99], radius=14,
                               fill=(72, 40, 18, 238), outline=WARM + (255,), width=2)
        draw.text((width * 0.5, 72), "ORDER FLIP", font=font(21, True), fill=WARM + (255,), anchor="mm")

    canvas.alpha_composite(hud)


def draw_panel_hud(canvas: Image.Image, sample: dict, order: list[int], frame: int,
                   label: str, accent: tuple[int, int, int], flip: bool) -> None:
    width, height = canvas.size
    hud = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(hud)

    draw.rectangle([0, 0, width, 5], fill=accent + (255,))
    draw.rectangle([30, 32, 36, 70], fill=accent + (255,))
    draw.text((52, 29), label, font=font(23, True), fill=TEXT_COLOR + (255,))
    draw.text((52, 60), "camera-space order" if accent == ACCENT else "authored order",
              font=font(15, False), fill=MUTED + (255,))

    depths = depth_map(sample)
    y = height - 172
    draw.text((30, y - 24), "BACK → FRONT", font=font(12, True), fill=MUTED + (255,))
    for index, layer_id in enumerate(order):
        near = index == len(order) - 1
        fill = (17, 64, 66, 236) if near else (20, 32, 50, 228)
        outline = accent + (255,) if near else (74, 106, 138, 120)
        draw.rounded_rectangle([30, y, width - 30, y + 54], radius=10,
                               fill=fill, outline=outline, width=2)
        draw.text((42, y + 8), str(index + 1), font=font(14, True),
                  fill=(accent if near else MUTED) + (255,))
        draw.text((64, y + 5), LABELS[int(layer_id)], font=font(18, True), fill=TEXT_COLOR + (255,))
        draw.text((64, y + 29), f"depth {depths[int(layer_id)]:.2f}", font=font(13, False),
                  fill=MUTED + (255,))
        if near:
            draw.text((width - 42, y + 19), "NEAR", font=font(12, True),
                      fill=accent + (255,), anchor="ra")
        y += 62

    draw.text((width - 30, height - 22), f"{frame:03d}", font=font(13, False),
              fill=MUTED + (255,), anchor="rm")

    if flip:
        draw.rounded_rectangle([width - 190, 104, width - 30, 146], radius=10,
                               fill=(72, 40, 18, 238), outline=WARM + (255,), width=2)
        draw.text((width - 110, 125), "ORDER FLIP", font=font(15, True),
                  fill=WARM + (255,), anchor="mm")

    canvas.alpha_composite(hud)


def draw_clean_hud(canvas: Image.Image, sample: dict, frame: int, total: int) -> None:
    width, height = canvas.size
    hud = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(hud)

    draw.rectangle([64, 54, 70, 98], fill=ACCENT + (255,))
    draw.text((86, 55), "CHRONONMOTION3D", font=font(31, True), fill=TEXT_COLOR + (255,))
    draw.text((86, 96), "depth-sorted compositing  ·  CM3D", font=font(18, False), fill=MUTED + (255,))

    depths = depth_map(sample)
    object_front = depths[1] < depths[2]
    title = "TESTO DIETRO L'OGGETTO" if object_front else "TESTO DAVANTI ALL'OGGETTO"
    draw.text((64, height - 104), title, font=font(32, True), fill=TEXT_COLOR + (255,))
    draw.text((64, height - 62), "l'ordine risolto segue la profondità, non l'ordine di authoring",
              font=font(17, False), fill=MUTED + (255,))

    draw.text((width - 64, height - 62), f"FRAME {frame:03d} / {total - 1:03d}",
              font=font(16, False), fill=MUTED + (255,), anchor="ra")
    values = [depths[1], depths[2]]
    low, high = min(values), max(values)
    span = max(high - low, 0.001)
    line_x = width - 112
    top, bottom = 270, height - 250
    draw.line([(line_x, top), (line_x, bottom)], fill=(70, 112, 146, 150), width=2)
    draw.text((line_x, top - 24), "CAMERA", font=font(12, True), fill=MUTED + (255,), anchor="mm")
    for layer_id, label, color in ((1, "OBJECT", WARM), (2, "TEXT", ACCENT)):
        t = (depths[layer_id] - low) / span
        y = top + t * (bottom - top)
        draw.ellipse([line_x - 7, y - 7, line_x + 7, y + 7], fill=color + (255,))
        draw.text((line_x - 18, y), label, font=font(14, True), fill=color + (255,), anchor="rm")
        draw.text((line_x + 18, y), f"{depths[layer_id]:.1f}", font=font(12, False),
                  fill=MUTED + (255,), anchor="lm")

    canvas.alpha_composite(hud)


def order_flip_frames(samples: list[dict]) -> set[int]:
    flips: set[int] = set()
    previous = None
    for index, sample in enumerate(samples):
        current = tuple(sample["order_depth"])
        if previous is not None and current != previous:
            flips.update(range(index, min(index + 15, len(samples))))
        previous = current
    return flips


def encode(frame_dir: Path, output: Path) -> None:
    command = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-framerate", str(FPS),
        "-i", str(frame_dir / "%04d.png"),
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        str(output),
    ]
    subprocess.run(command, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_depth_video(samples: list[dict], props: dict, output: Path, tmp: Path,
                      flips: set[int]) -> None:
    frame_dir = tmp / "_depth"
    if frame_dir.exists():
        shutil.rmtree(frame_dir)
    frame_dir.mkdir(parents=True)
    total = len(samples)
    for index, sample in enumerate(samples):
        canvas, _ = render_scene(sample, sample["order_depth"], WIDTH, HEIGHT, props)
        draw_full_hud(canvas, sample, sample["order_depth"], index, total, "depth", index in flips)
        canvas.convert("RGB").save(frame_dir / f"{index:04d}.png")
    encode(frame_dir, output)
    shutil.rmtree(frame_dir, ignore_errors=True)


def build_split_video(samples: list[dict], props: dict, output: Path, tmp: Path,
                      flips: set[int]) -> None:
    frame_dir = tmp / "_split"
    if frame_dir.exists():
        shutil.rmtree(frame_dir)
    frame_dir.mkdir(parents=True)
    total = len(samples)
    for index, sample in enumerate(samples):
        left, _ = render_scene(sample, sample["order_stack"], WIDTH // 2, HEIGHT, props)
        draw_panel_hud(left, sample, sample["order_stack"], index, "STACK (default)", MUTED, False)
        right, _ = render_scene(sample, sample["order_depth"], WIDTH // 2, HEIGHT, props)
        draw_panel_hud(right, sample, sample["order_depth"], index, "DEPTH SORT", ACCENT, index in flips)

        canvas = Image.new("RGB", (WIDTH, HEIGHT))
        canvas.paste(left.convert("RGB"), (0, 0))
        canvas.paste(right.convert("RGB"), (WIDTH // 2, 0))
        draw = ImageDraw.Draw(canvas)
        draw.line([(WIDTH // 2 - 1, 0), (WIDTH // 2 - 1, HEIGHT)], fill=(38, 58, 84), width=2)
        canvas.save(frame_dir / f"{index:04d}.png")
    encode(frame_dir, output)
    shutil.rmtree(frame_dir, ignore_errors=True)


def build_clean_video(samples: list[dict], props: dict, output: Path, tmp: Path) -> dict[int, Image.Image]:
    frame_dir = tmp / "_clean"
    if frame_dir.exists():
        shutil.rmtree(frame_dir)
    frame_dir.mkdir(parents=True)
    total = len(samples)
    captures = {18, 88, 146, 214}
    captured: dict[int, Image.Image] = {}
    for index, sample in enumerate(samples):
        canvas, _ = render_scene(sample, sample["order_depth"], WIDTH, HEIGHT, props)
        draw_clean_hud(canvas, sample, index, total)
        image = canvas.convert("RGB")
        image.save(frame_dir / f"{index:04d}.png")
        if index in captures:
            captured[index] = image.copy()
    encode(frame_dir, output)
    shutil.rmtree(frame_dir, ignore_errors=True)
    return captured


def make_contact_sheet(frames: dict[int, Image.Image], output: Path) -> None:
    items = sorted(frames.items())[:4]
    sheet = Image.new("RGB", (WIDTH, HEIGHT), (5, 8, 16))
    cell_w, cell_h = WIDTH // 2, HEIGHT // 2
    for index, (_, frame) in enumerate(items):
        thumb = frame.resize((cell_w, cell_h), Image.Resampling.LANCZOS)
        x = (index % 2) * cell_w
        y = (index // 2) * cell_h
        sheet.paste(thumb, (x, y))
    draw = ImageDraw.Draw(sheet)
    for index, (frame_number, _) in enumerate(items):
        x = (index % 2) * cell_w
        y = (index // 2) * cell_h
        label = f"FRAME {frame_number:03d}"
        text_width = draw.textlength(label, font=font(15, True))
        center_x = x + cell_w * 0.5
        draw.rounded_rectangle(
            [center_x - text_width * 0.5 - 12, y + cell_h - 44,
             center_x + text_width * 0.5 + 12, y + cell_h - 14],
            radius=9, fill=(6, 12, 24, 220), outline=ACCENT + (255,), width=1,
        )
        draw.text((center_x, y + cell_h - 29), label, font=font(15, True),
                  fill=ACCENT + (255,), anchor="mm")
    sheet.save(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", default="RenderingGen/Upload/cm3d_compositing_20261110/showcase.json")
    parser.add_argument("--out", default="RenderingGen/Upload/cm3d_compositing_20261110")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    data = json.loads(Path(args.json).read_text(encoding="utf-8"))
    samples = data["samples"]
    props = data["props"]
    output = Path(args.out)
    output.mkdir(parents=True, exist_ok=True)
    tmp = output / "_work"
    tmp.mkdir(exist_ok=True)

    flips = order_flip_frames(samples)
    videos = [
        ("01_depth_sort_crossing_1080p.mp4", "depth"),
        ("02_stack_vs_depth_1080p.mp4", "split"),
        ("03_text_behind_object_1080p.mp4", "clean"),
    ]

    captured: dict[int, Image.Image] = {}
    for name, kind in videos:
        path = output / name
        print(f"rendering {name} ...", flush=True)
        if kind == "depth":
            build_depth_video(samples, props, path, tmp, flips)
        elif kind == "split":
            build_split_video(samples, props, path, tmp, flips)
        else:
            captured = build_clean_video(samples, props, path, tmp)
        print(f"  wrote {path} ({path.stat().st_size / (1024 * 1024):.2f} MB)", flush=True)

    contact = output / "contact_sheet.png"
    make_contact_sheet(captured, contact)
    print(f"  wrote {contact}", flush=True)

    manifest = {
        "schema": "chrononmotion.compositing-showcase.videos.v1",
        "source_json": str(Path(args.json)),
        "width": WIDTH,
        "height": HEIGHT,
        "fps": FPS,
        "duration_seconds": len(samples) / FPS,
        "feature": "resolveCompositeStack: Stack vs camera-depth order",
        "videos": [
            {"file": name, "bytes": (output / name).stat().st_size, "sha256": sha256(output / name)}
            for name, _ in videos
        ],
        "contact_sheet": {"file": contact.name, "bytes": contact.stat().st_size, "sha256": sha256(contact)},
    }
    (output / "videos_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    if not args.keep_frames:
        shutil.rmtree(tmp, ignore_errors=True)
    print("done")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"render failed: {error}", file=sys.stderr)
        raise SystemExit(1)
