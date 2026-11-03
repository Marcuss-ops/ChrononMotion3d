#!/usr/bin/env python3
"""Generate the authoring side of the animation showcase: assets + render plans.

Three clips, each one giving a ticket that had no moving picture its own clip:

  01-oggetto-sotto-luce   TICKET-HDRI-ENV-V1      environment_light + rotating probe
  02-materiali            look-dev materiali       lighting_rig + baked turntable
  03-gltf-animato         TICKET-GLTF-ANIMATED-V1 baked glTF positions, looped

Measured behaviour of the 2026-11-03 build (see README "what actually moves"):
  * `camera_samples`, baked `inline_mesh.animation` and per-layer
    `animation.tracks` (position/opacity/rotation, keyframed or dense) are all
    evaluated per frame. The layer tracks used to be frozen at the first
    sampled frame; the clip below now spins and bobs its three solids through
    real per-layer tracks, so the fix is covered by the delivered pixels.

Conventions learned from the decoder and confirmed by rendering (see README):
  * `type: "image"` layers are 2.5D planes: `position` is the TOP-LEFT corner in
    canvas pixels, `size` is in pixels, and they cannot carry `inline_mesh`;
  * `type: "mesh"` layers are 3D: `position` is world space with the origin at
    the canvas centre and +Y up, `scale` multiplies the mesh-local vertices;
  * the camera is in FieldOfView mode, so `fov_deg` is chosen so that one world
    unit is one pixel at z = 0 (focal = height/2 / tan(fov/2) = 1000);
  * inline meshes do NOT sample textures (render-plan.inline-mesh v1: "no source
    texture sampling"): a mesh shows its flat `color` + `material`, so an
    animated mesh has to read through its shading, not through a texture;
  * layer animation is `{"tracks":[{"property": ..., "keyframes": [...]}]}`.

Timing: 60 fps over 4 s (240 frames). The software path renders a frame in
~0.21 s, so the three clips cost ~2m30s in total; every authored rate below is
derived from FPS/SECONDS, so the motion keeps the same wall-clock speed as the
original 30 fps/120 frame cut.

The camera in clip 03 is a pure pan: this build collapses a baked mesh to a
point over the clip as soon as the sampled camera rotation changes at all (see
README, "limiti osservati nel render").

Usage: python3 make_showcase.py [--width 960] [--height 540]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
PLANS = os.path.join(HERE, "plans")

# Authored with a 96 x 48 px overscan: a camera move shifts the whole canvas, so
# the outermost columns are cropped away again by render.sh instead of showing
# an empty edge.
W = 1056
H = 588
CROP_X = 48
CROP_Y = 24
DELIVERY_W = 960
DELIVERY_H = 540

# Delivery cadence. Every authored rate below is expressed in frames but derived
# from these two numbers, so raising the sampling stays a one-line change.
FPS = 60
SECONDS = 4.0
FRAMES = int(round(FPS * SECONDS))

# ── textures ─────────────────────────────────────────────────────────────────


def radial(w: int, h: int, inner: tuple, outer: tuple, falloff: float = 1.35) -> Image.Image:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    r = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    r = np.clip(r / falloff, 0.0, 1.0)[..., None]
    img = np.array(inner, dtype=np.float64) * (1.0 - r) + np.array(outer, dtype=np.float64) * r
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def sky_gradient(w: int, h: int) -> Image.Image:
    y = np.linspace(0.0, 1.0, h)[:, None, None]
    top = np.array([18, 38, 84], dtype=np.float64)
    mid = np.array([86, 132, 178], dtype=np.float64)
    low = np.array([214, 186, 156], dtype=np.float64)
    img = np.where(y < 0.66, top + (mid - top) * (y / 0.66), mid + (low - mid) * ((y - 0.66) / 0.34))
    img = np.repeat(img, w, axis=1)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def _value_noise(shape: tuple, rng: np.random.Generator, octaves: int) -> np.ndarray:
    """Fractal value noise in [0,1] from bilinearly upsampled random grids."""
    h, w = shape
    out = np.zeros((h, w), dtype=np.float64)
    amp, total = 1.0, 0.0
    for octave in range(octaves):
        cells = 4 * (2 ** octave)
        gh, gw = cells, max(2, int(round(cells * w / h)))
        grid = rng.random((gh, gw))
        grid[:, -1] = grid[:, 0]  # seamless horizontal scroll
        up = Image.fromarray((grid * 255).astype(np.uint8), "L").resize((w, h), Image.BICUBIC)
        out += amp * (np.asarray(up, dtype=np.float64) / 255.0)
        total += amp
        amp *= 0.5
    return out / total


def cloud_texture(w: int, h: int, seed: int) -> Image.Image:
    rng = np.random.default_rng(seed)
    n = _value_noise((h, w), rng, 6)
    yy = np.linspace(0.0, 1.0, h)[:, None]
    n *= 0.45 + 0.55 * np.clip(1.0 - np.abs(yy - 0.42) * 1.9, 0.0, 1.0)
    alpha = np.clip((n - 0.40) / (0.74 - 0.40), 0.0, 1.0) ** 1.45
    slope = np.gradient(n, axis=0)
    lit = np.clip(0.88 + slope * 7.0, 0.60, 1.0)
    rgba = np.zeros((h, w, 4), dtype=np.uint8)
    rgba[..., 0] = np.clip(255 * lit, 0, 255).astype(np.uint8)
    rgba[..., 1] = np.clip(249 * lit, 0, 255).astype(np.uint8)
    rgba[..., 2] = np.clip(242 * (0.94 + 0.06 * lit), 0, 255).astype(np.uint8)
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def glow(size: int, power: float) -> Image.Image:
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.sqrt(((xx - size / 2) / (size / 2)) ** 2 + ((yy - size / 2) / (size / 2)) ** 2)
    a = np.clip(1.0 - r, 0.0, 1.0) ** power
    rgba = np.zeros((size, size, 4), dtype=np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = 255, 236, 198
    rgba[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


# ── geometry (mesh-local, unit scale) ────────────────────────────────────────


def _rot(axis: str, deg: float) -> np.ndarray:
    t = math.radians(deg)
    c, s = math.cos(t), math.sin(t)
    if axis == "x":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def icosahedron():
    """20 flat facets, duplicated per face (the software fill leaves hairline
    gaps on the shared seams of a UV sphere, so the demo uses subdivided solids
    with one seam at most)."""
    phi = (1 + 5 ** 0.5) / 2
    raw = [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0),
           (0, -1, phi), (0, 1, phi), (0, -1, -phi), (0, 1, -phi),
           (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]
    unit = [tuple(c / math.sqrt(sum(x * x for x in p)) for c in p) for p in raw]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
             (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
             (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    verts, uvs, normals, idx = [], [], [], []
    for f in faces:
        p = [unit[i] for i in f]
        cx = sum(a[0] for a in p) / 3.0
        cy = sum(a[1] for a in p) / 3.0
        cz = sum(a[2] for a in p) / 3.0
        length = math.sqrt(cx * cx + cy * cy + cz * cz) or 1.0
        base = len(verts)
        for a in p:
            verts.append(list(a))
            normals.append([round(cx / length, 4), round(cy / length, 4), round(cz / length, 4)])
            uvs.append([0.5, 0.5])
        idx += [base, base + 1, base + 2]
    return verts, uvs, normals, idx


def torus_mesh(major_r: float, minor_r: float, major_seg: int, minor_seg: int):
    verts, uvs = [], []
    for i in range(major_seg + 1):
        tu = (i / major_seg) * 2.0 * math.pi
        for j in range(minor_seg + 1):
            tv = (j / minor_seg) * 2.0 * math.pi
            r = major_r + minor_r * math.cos(tv)
            verts.append([r * math.cos(tu), minor_r * math.sin(tv), r * math.sin(tu)])
            uvs.append([i / major_seg, j / minor_seg])
    idx = []
    for i in range(major_seg):
        for j in range(minor_seg):
            a = i * (minor_seg + 1) + j
            b = a + minor_seg + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return verts, uvs, idx


def cube():
    """Unit cube split per face so every face carries its own normal."""
    faces = (
        ((0, 0, -1), [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)]),
        ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
        ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
        ((1, 0, 0), [(1, -1, -1), (1, -1, 1), (1, 1, 1), (1, 1, -1)]),
        ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
        ((0, 1, 0), [(-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)]),
    )
    verts, normals, uvs, idx = [], [], [], []
    for n, quad in faces:
        base = len(verts)
        for k, p in enumerate(quad):
            verts.append(list(p))
            normals.append(list(n))
            uvs.append([[0, 0], [1, 0], [1, 1], [0, 1]][k])
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return verts, uvs, normals, idx


def octahedron():
    """Faceted gem; duplicated vertices so each face is flat-shaded."""
    apex, bottom = (0, 1, 0), (0, -1, 0)
    ring = [(1, 0, 0), (0, 0, 1), (-1, 0, 0), (0, 0, -1)]
    verts, normals, uvs, idx = [], [], [], []
    for i in range(4):
        a, b = ring[i], ring[(i + 1) % 4]
        for tri in ((apex, a, b), (bottom, b, a)):
            base = len(verts)
            cx = sum(p[0] for p in tri) / 3.0
            cy = sum(p[1] for p in tri) / 3.0
            cz = sum(p[2] for p in tri) / 3.0
            length = math.sqrt(cx * cx + cy * cy + cz * cz) or 1.0
            for p in tri:
                verts.append(list(p))
                normals.append([round(cx / length, 4), round(cy / length, 4), round(cz / length, 4)])
                uvs.append([0.5, 0.5])
            idx += [base, base + 1, base + 2]
    return verts, uvs, normals, idx


def bake_mesh(verts, uvs, idx, frames: int, transform=None, normals=None) -> dict:
    """One inline_mesh; per-frame baked positions when `transform` is given."""
    normals = normals or [[0.0, 0.0, 1.0]] * len(verts)
    mesh = {
        "vertices": [{"position": [round(float(c), 5) for c in p],
                      "normal": list(n),
                      "uv": [round(float(u), 5) for u in uv]}
                     for p, n, uv in zip(verts, normals, uvs)],
        "indices": list(idx),
    }
    if transform is not None:
        samples = []
        for f in range(frames):
            m = transform(f)
            samples.append({"frame": f,
                            "positions": [[round(float(c), 5) for c in (m @ np.asarray(p, dtype=np.float64))]
                                          for p in verts]})
        mesh["animation"] = samples
    return mesh


# ── plan helpers ─────────────────────────────────────────────────────────────


def camera(w: int, h: int) -> dict:
    """Field-of-view camera calibrated so 1 world unit == 1 pixel at z = 0."""
    focal = 1000.0  # camera is 1000 units in front of z = 0
    fov = 2.0 * math.degrees(math.atan((h / 2.0) / focal))
    return {"type": "perspective", "position": [0, 0, -1000], "rotation_deg": [0, 0, 0],
            "fov_deg": round(fov, 4), "near": 1, "far": 10000, "zoom": 1}


def canvas(w: int, h: int, frames: int, fps: int = FPS) -> dict:
    return {"width": w, "height": h, "fps_num": fps, "fps_den": 1, "duration_frames": frames}


def image_layer(lid: str, asset: str, size, position, frames: int, *, opacity=1.0,
                tracks=None, color=(1, 1, 1, 1)) -> dict:
    layer = {
        "id": lid, "type": "image", "asset": asset,
        "size": [float(size[0]), float(size[1])],
        "position": [float(position[0]), float(position[1])],
        "enable_3d": False, "opacity": float(opacity),
        "start_frame": 0, "duration_frames": frames,
        "color": list(color),
    }
    if tracks:
        layer["animation"] = {"tracks": tracks}
    return layer


def mesh_layer(lid: str, mesh: dict, position, scale, frames: int, *, color=(1, 1, 1, 1),
               material=None, tracks=None) -> dict:
    if material is not None:
        mesh = {**mesh, "material": material}
    layer = {
        "id": lid, "type": "mesh",
        "size": [float(scale), float(scale)], "position": [float(position[0]), float(position[1]), float(position[2])],
        "scale": [float(scale)] * 3,
        "enable_3d": True, "screen_space": False, "loop": False,
        "opacity": 1.0, "start_frame": 0, "duration_frames": frames,
        "color": list(color), "inline_mesh": mesh,
    }
    if tracks:
        layer["animation"] = {"tracks": tracks}
    return layer


def bob_track(base: float, amplitude: float, frames: int, period: int | None = None) -> dict:
    """Vertical sine, one full period every 2 s, sampled every quarter period."""
    period = period or max(1, frames // 2)  # 2 s regardless of the fps
    last = frames - 1
    step = max(1, period // 4)
    keys = []
    for f in list(range(0, last, step)) + [last]:
        keys.append({"frame": f,
                     "value": round(base + amplitude * math.sin(2.0 * math.pi * f / period), 3)})
    return {"property": "position_y", "easing": "linear", "keyframes": keys}


def sway_track(amplitude: float, frames: int) -> dict:
    """A readable Y-rotation arc.

    The solids in clip 01 are flat or thin meshes: rotating past ~90 deg puts
    them edge-on and the native 3D path backface-culls them, so the demo stays
    inside a sway that keeps every face visible.
    """
    last = frames - 1
    keys = [{"frame": f, "value": v} for f, v in
            ((0, -amplitude), (frames // 4, 0.0), (frames // 2, amplitude),
             (frames * 3 // 4, 0.0), (last, -amplitude))]
    return {"property": "rotation_y", "easing": "linear", "keyframes": keys}


def drift_track(frames: int, start, end, prop="position", easing="in_out_sine") -> dict:
    return {"property": prop, "easing": easing,
            "keyframes": [{"frame": 0, "value": [round(float(v), 3) for v in start]},
                          {"frame": frames - 1, "value": [round(float(v), 3) for v in end]}]}


STUDIO_SH = [
    11.428123, 0, 0, 3.120892e-08, 0, 0, 3.120892e-08, 0, 0,
    -3.120892e-08, 0, 0, -1.1703345e-08, 0, 0, 1.7555017e-08, 0, 0,
    -0.09688196, 0, 0, 0, 0, 0, -0.16780445, 0, 0,
]


def dolly(cam: dict, frames: int, *, pan=(55.0, -22.0), turn=(2.4, -1.1),
          step: int | None = None) -> list:
    """Camera samples: a slow push + pan, ~12 samples over the clip."""
    step = step or max(1, frames // 12)
    samples = []
    for f in range(0, frames, step):
        t = f / max(frames - 1, 1)
        c = dict(cam)
        c["position"] = [round(pan[0] * t, 3), round(pan[1] * t, 3), -1000]
        c["rotation_deg"] = [round(turn[1] * t, 4), round(turn[0] * t, 4), 0.0]
        samples.append({"frame": f, "camera": c})
    return samples


def write_plan(name: str, plan: dict) -> None:
    os.makedirs(PLANS, exist_ok=True)
    with open(os.path.join(PLANS, name), "w", encoding="utf-8") as fh:
        json.dump(plan, fh, indent=1)
    print(f"  {name}: {os.path.getsize(os.path.join(PLANS, name)) / 1024:.0f} KiB")


# ── scenes ───────────────────────────────────────────────────────────────────


def scene_object_under_light(w: int, h: int, frames: int) -> dict:
    """Three solids under a rotating HDRI probe.

    Only convex, non-overlapping meshes: the software mesh path draws in layer
    order without a depth buffer, so two solids sharing pixels would occlude
    each other incorrectly (a torus shows its own far side on top).
    """
    ico_v, ico_uv, ico_n, ico_i = icosahedron()
    solid = bake_mesh(ico_v, ico_uv, ico_i, frames, normals=ico_n)

    cube_v, cube_uv, cube_n, cube_i = cube()
    box = bake_mesh(cube_v, cube_uv, cube_i, frames, normals=cube_n)

    gem_v, gem_uv, gem_n, gem_i = octahedron()
    gem = bake_mesh(gem_v, gem_uv, gem_i, frames,
                    normals=gem_n,
                    transform=lambda f: _rot("y", 240.0 * f / frames) @ _rot("x", 28.0 * math.sin(2 * math.pi * f / frames)))

    return {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "showcase_object_under_light",
        "camera": camera(w, h),
        "canvas": canvas(w, h, frames),
        "output": {"path": "01-oggetto-sotto-luce.mp4", "format": "mp4"},
        "lighting_rig": {
            "key_direction": [0.35, -0.55, 0.75], "key_intensity": 1.5,
            "key_color": [1.0, 0.97, 0.92, 1.0],
            "ambient_intensity": 0.10, "ambient_color": [0.50, 0.60, 0.80, 1.0],
            "rim_enabled": True, "rim_intensity": 1.1, "rim_power": 3.0,
            "rim_color": [0.55, 0.75, 1.0, 1.0],
        },
        "camera_samples": dolly(camera(w, h), frames, pan=(14.0, 8.0), turn=(1.2, 0.6)),
        "environment_light": {
            "probe_asset": "assets/studio.hdri-env", "intensity": 1.25, "rotation_y": 0.0,
            "irradiance_sh": STUDIO_SH,
            "rotation_animation": {
                "property": "rotation_y",
                "keyframes": [{"frame": 0, "value": [0.0]},
                              {"frame": frames // 2, "value": [180.0]},
                              {"frame": frames - 1, "value": [358.0]}],
            },
        },
        "layers": [
            image_layer("fondale", "assets/studio-backdrop.png", (int(w * 2.0), int(h * 2.0)),
                        (-int(w * 0.5), -int(h * 0.5)), frames),
            # Per-layer tracks: each solid sways and bobs on its own, on top of
            # the camera dolly and the rotating probe.
            mesh_layer("cubo", box, (-w * 0.28, 20, 0), 88, frames,
                       color=(0.90, 0.52, 0.26, 1.0),
                       tracks=[sway_track(22.0, frames), bob_track(20.0, 12.0, frames)],
                       material={"diffuse": 0.55, "specular": 0.9, "shininess": 90.0,
                                 "metallic": 0.7, "roughness": 0.20,
                                 "base_color": [0.86, 0.48, 0.22, 1.0]}),
            mesh_layer("icosaedro", solid, (0, -15, 60), 118, frames,
                       color=(0.78, 0.88, 1.0, 1.0),
                       tracks=[sway_track(-18.0, frames), bob_track(-15.0, 20.0, frames)],
                       material={"diffuse": 0.55, "specular": 1.0, "shininess": 72.0,
                                 "metallic": 0.25, "roughness": 0.20,
                                 "base_color": [0.78, 0.88, 1.0, 1.0]}),
            mesh_layer("gemma", gem, (w * 0.28, 10, 0), 105, frames,
                       color=(0.98, 0.86, 0.42, 1.0),
                       tracks=[sway_track(15.0, frames), bob_track(10.0, 16.0, frames)],
                       material={"diffuse": 0.60, "specular": 1.0, "shininess": 48.0,
                                 "metallic": 0.4, "roughness": 0.25,
                                 "base_color": [0.97, 0.84, 0.38, 1.0]}),
        ],
    }


def plane3d(lid: str, asset: str, size, position, frames: int, *,
            opacity=1.0, color=(1, 1, 1, 1)) -> dict:
    """A 2.5D image layer projected by the camera.

    `size` is in world units and `position` is a world-space centre with +Y up,
    so the on-screen size is `size * 1000 / (1000 + z)` for the calibrated
    camera. Depth is what produces parallax here.
    """
    layer = image_layer(lid, asset, size, (0.0, 0.0), frames,
                        opacity=opacity, color=color)
    layer["enable_3d"] = True
    layer["position"] = [float(position[0]), float(position[1]), float(position[2])]
    return layer


def scene_lookdev(w: int, h: int, frames: int) -> dict:
    """Material look-dev turntable: one solid, four material patches.

    This replaced the cloud sheet clip: every animated image layer in this build
    stops being drawn after the first refreshed frame (README, "what actually
    moves"), so a clouds clip cannot be produced with the current software
    graph. A baked mesh turntable exercises the same lighting rig and
    environment light without depending on that path.
    """
    ico_v, ico_uv, ico_n, ico_i = icosahedron()
    spin = lambda f, phase=0.0: (
        _rot("y", 360.0 * f / frames + phase) @ _rot("x", 18.0 * math.sin(2 * math.pi * f / frames)))
    solid = bake_mesh(ico_v, ico_uv, ico_i, frames, normals=ico_n, transform=spin)
    cube_v, cube_uv, cube_n, cube_i = cube()
    box = bake_mesh(cube_v, cube_uv, cube_i, frames, normals=cube_n,
                    transform=lambda f: _rot("y", -220.0 * f / frames) @ _rot("z", 12.0))
    gem_v, gem_uv, gem_n, gem_i = octahedron()
    gem = bake_mesh(gem_v, gem_uv, gem_i, frames, normals=gem_n,
                    transform=lambda f: _rot("x", 300.0 * f / frames))
    return {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "showcase_lookdev",
        "camera": camera(w, h),
        "canvas": canvas(w, h, frames),
        "output": {"path": "02-materiali.mp4", "format": "mp4"},
        "lighting_rig": {
            "key_direction": [0.32, -0.42, 0.85], "key_intensity": 1.35,
            "key_color": [1.0, 0.96, 0.90, 1.0],
            "ambient_intensity": 0.22, "ambient_color": [0.42, 0.55, 0.85, 1.0],
            "rim_enabled": True, "rim_intensity": 0.9, "rim_power": 4.0,
            "rim_color": [0.45, 0.70, 1.0, 1.0],
        },
        "camera_samples": dolly(camera(w, h), frames, pan=(-18.0, 8.0), turn=(1.0, 0.4)),
        "environment_light": {
            "probe_asset": "assets/studio.hdri-env", "intensity": 1.0, "rotation_y": 0.0,
            "irradiance_sh": STUDIO_SH,
            "rotation_animation": {
                "property": "rotation_y",
                "keyframes": [{"frame": 0, "value": [0.0]},
                              {"frame": frames - 1, "value": [240.0]}],
            },
        },
        "layers": [
            image_layer("fondale", "assets/studio-backdrop.png", (int(w * 2.0), int(h * 2.0)),
                        (-int(w * 0.5), -int(h * 0.5)), frames),
            mesh_layer("metallo", solid, (-w * 0.26, 0, 0), 150, frames,
                       color=(0.86, 0.88, 0.92, 1.0),
                       material={"diffuse": 0.35, "specular": 1.0, "shininess": 160.0,
                                 "metallic": 0.95, "roughness": 0.12,
                                 "base_color": [0.82, 0.85, 0.90, 1.0]}),
            mesh_layer("plasticop", box, (0, 6, 0), 110, frames,
                       color=(0.24, 0.62, 0.86, 1.0),
                       material={"diffuse": 0.80, "specular": 0.35, "shininess": 24.0,
                                 "metallic": 0.05, "roughness": 0.75,
                                 "base_color": [0.20, 0.58, 0.84, 1.0]}),
            mesh_layer("gemma", gem, (w * 0.26, -4, 0), 120, frames,
                       color=(0.98, 0.80, 0.30, 1.0),
                       material={"diffuse": 0.55, "specular": 1.0, "shininess": 64.0,
                                 "metallic": 0.55, "roughness": 0.22,
                                 "base_color": [0.96, 0.76, 0.26, 1.0]}),
        ],
    }


def scene_gltf_loop(w: int, h: int, frames: int) -> dict:
    """Loop the baked glTF fixture (TICKET-GLTF-ANIMATED-V1) around its centroid."""
    src = os.path.join(HERE, "..", "ticket-roundtrip-scaled-20261029", "gltf", "animated-gltf.plan.json")
    with open(src, encoding="utf-8") as fh:
        fixture = json.load(fh)
    mesh = fixture["layers"][0]["inline_mesh"]
    samples = mesh["animation"]
    period = len(samples)
    # The fixture is authored in glTF units (a 1-unit triangle translated by up
    # to 2 units), and baked `inline_mesh.animation` positions are absolute in
    # world pixels: the layer `scale` is not applied on that path, so the mesh
    # has to be scaled while baking or it draws as a 2 px speck.
    UNIT = 150.0
    TRAVEL = 0.35
    # The fixture is baked at 30 samples per second; it is resampled by
    # wall-clock time so the clip plays at the same speed at any delivery fps.
    SRC_FPS = 30.0
    first = samples[0]["positions"]
    cx = sum(p[0] for p in first) / len(first)
    cy = sum(p[1] for p in first) / len(first)
    # In-plane spin so the single triangle reads as a turning solid rather than
    # a sliding polygon (a Y rotation would flatten it at 90 degrees).
    # Start off-axis: an exactly 45-degree hypotenuse makes the software
    # rasteriser's ordered dither stripe along its scanlines.
    spin_at = lambda f: _rot("z", -22.0 + 150.0 * f / max(frames - 1, 1))

    def posed(index: int, spin: np.ndarray) -> list[np.ndarray]:
        """The fixture sample placed in delivery pixels, spun by `spin`."""
        pos = samples[index]["positions"]
        fx = sum(p[0] for p in pos) / len(pos)
        fy = sum(p[1] for p in pos) / len(pos)
        # The fixture animates scale as well as translation. Normalise every
        # sample to a constant on-screen size, then re-apply only the centroid
        # path: otherwise the clip reads as a pulse that drifts out of frame.
        local = np.array([[p[0] - fx, p[1] - fy, p[2]] for p in pos], dtype=np.float64)
        radius = max(float(np.hypot(v[0], v[1])) for v in local) or 1.0
        local = local / radius * UNIT
        travel = np.array([(fx - cx) * UNIT * TRAVEL, (fy - cy) * UNIT * TRAVEL, 0.0])
        return [spin @ v + travel for v in local]

    looped = []
    span = 2 * (period - 1)
    for f in range(frames):
        # Ping-pong so the baked clip loops without a jump cut at its end, and
        # interpolate between the two neighbouring fixture samples: the fixture
        # is baked at 30 samples/s, so a 60 fps delivery that snapped to the
        # nearest sample would ship every other frame twice.
        folded = ((f / float(FPS)) * SRC_FPS) % span
        if folded >= period - 1:
            folded = span - folded
        index = min(int(folded), period - 2)
        weight = folded - index
        spin = spin_at(f)
        a = posed(index, spin)
        b = posed(index + 1, spin)
        looped.append({"frame": f, "positions": [
            [round(float(c), 5) for c in (1.0 - weight) * va + weight * vb]
            for va, vb in zip(a, b)]})
    out_mesh = {"vertices": mesh["vertices"], "indices": mesh["indices"], "animation": looped}
    return {
        "schema": "chronon.render-plan.v3", "version": 3, "job_id": "showcase_gltf_animated",
        "camera": camera(w, h),
        "canvas": canvas(w, h, frames),
        "output": {"path": "03-gltf-animato.mp4", "format": "mp4"},
        # A neutral rig instead of the HDRI probe: with the probe on, the 3D
        # backdrop comes out tinted warm and the flat triangle reads as a
        # different lighting setup from clips 01/02.
        "lighting_rig": {
            "key_direction": [0.15, -0.30, 0.94], "key_intensity": 0.35,
            "key_color": [1.0, 1.0, 1.0, 1.0],
            "ambient_intensity": 1.0, "ambient_color": [1.0, 1.0, 1.0, 1.0],
            "rim_enabled": False, "rim_intensity": 0.0, "rim_power": 1.0,
            "rim_color": [1.0, 1.0, 1.0, 1.0],
        },
        # Dolly only: the camera below never rotates. With any camera rotation
        # in play the engine draws this baked mesh progressively smaller until
        # it collapses to a point around frame 56 and then grows back (see
        # README, "limits observed in the render"); position-only camera motion
        # is clean, so the move is a pure pan.
        "camera_samples": dolly(camera(w, h), frames, pan=(14.0, 8.0), turn=(0.0, 0.0),
                                step=max(1, frames // 24)),
        "layers": [
            # A 3D backdrop, not a 2D `image` layer: with a flat 2D layer under
            # this mesh the composite punches a 50% random dither through the
            # mesh (measured: 7.5k solid pixels instead of 20k, the rest left as
            # backdrop pixels). Same geometry behind a `plane3d` renders solid.
            # `color` doubles the backdrop: a 3D plane is shaded, and this rig
            # lights it dimmer than the unlit 2D layer clips 01/02 use.
            plane3d("fondale", "assets/studio-backdrop.png", (1400, 800), (0.0, 0.0, 100.0),
                    frames, color=(2.0, 2.0, 2.0, 1.0)),
            mesh_layer("gltf-baked", out_mesh, (0, 0, 0), 1.0, frames,
                       color=(1.0, 0.94, 0.35, 1.0),
                       material={"diffuse": 0.65, "specular": 0.8, "shininess": 40.0,
                                 "base_color": [1.0, 0.93, 0.30, 1.0]}),
        ],
    }


BUILDERS = (
    ("01-oggetto-sotto-luce.plan.json", scene_object_under_light, FRAMES),
    ("02-materiali.plan.json", scene_lookdev, FRAMES),
    ("03-gltf-animato.plan.json", scene_gltf_loop, FRAMES),
)


def build_assets() -> None:
    os.makedirs(ASSETS, exist_ok=True)
    sky_gradient(1600, 900).save(os.path.join(ASSETS, "sky.png"))
    radial(900, 900, (62, 66, 78), (6, 7, 11)).save(os.path.join(ASSETS, "studio-backdrop.png"))
    for name, seed in (("cloud-far.png", 7), ("cloud-mid.png", 21), ("cloud-near.png", 33)):
        cloud_texture(1400, 620, seed).save(os.path.join(ASSETS, name))
    glow(512, 2.4).save(os.path.join(ASSETS, "sun.png"))
    env = os.path.join(HERE, "..", "ticket-roundtrip-scaled-20261029", "hdri", "assets", "studio.hdri-env")
    if os.path.exists(env):
        shutil.copyfile(env, os.path.join(ASSETS, "studio.hdri-env"))
    else:  # pragma: no cover
        print("  ! studio.hdri-env missing: the environment light will not resolve")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=W)
    ap.add_argument("--height", type=int, default=H)
    args = ap.parse_args()
    globals()["W"], globals()["H"] = args.width, args.height
    build_assets()
    for name, builder, frames in BUILDERS:
        write_plan(name, builder(args.width, args.height, frames))
    print("plans written to", PLANS)


if __name__ == "__main__":
    main()
