#!/usr/bin/env python3
"""Generate a complete 3D Roblox-style animated Short scene using Chronon3D + CM3D architecture.

Character: Classic Roblox "Noob" avatar (Yellow head/arms, Blue torso, Green legs, smiley face)
Action: Waving right arm (classic Roblox wave), idle dance bobbing, smiling at dynamic camera.
Environment: Classic green Studs baseplate + sunny cartoon sky + lighting rig.
Output: RenderPlan V3 JSON + assets.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
PLANS = os.path.join(HERE, "plans")
OUT = os.path.join(HERE, "out")

os.makedirs(ASSETS, exist_ok=True)
os.makedirs(PLANS, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

# 1056x588 with 96x48 overscan, will be cropped/upscaled to 1920x1080 @ 60fps
W = 1056
H = 588
FPS = 60
SECONDS = 4.0
FRAMES = int(round(FPS * SECONDS))  # 240 frames

# ── Textures ─────────────────────────────────────────────────────────────────

def make_roblox_sky(w: int, h: int) -> Image.Image:
    """Vibrant cartoon sky with stylized clouds."""
    img = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(img)
    # Gradient from bright sky blue to cyan
    for y in range(h):
        ratio = y / h
        r = int(70 + (160 - 70) * ratio)
        g = int(170 + (225 - 170) * ratio)
        b = int(255 + (255 - 255) * ratio)
        draw.line([(0, y), (w, y)], fill=(r, g, b, 255))
    
    # Stylized fluffy clouds
    cloud_color = (255, 255, 255, 220)
    clouds = [
        (150, 100, 140, 50), (220, 85, 160, 60), (310, 105, 120, 45),
        (750, 130, 180, 55), (840, 115, 150, 65), (920, 140, 130, 45),
        (450, 60, 200, 60), (560, 50, 180, 70)
    ]
    for cx, cy, rw, rh in clouds:
        draw.ellipse([cx - rw, cy - rh, cx + rw, cy + rh], fill=cloud_color)
    return img

def make_roblox_baseplate(size: int = 1024) -> Image.Image:
    """Classic green Roblox studs baseplate texture."""
    base_color = (76, 153, 56)       # classic grass green
    stud_top = (92, 178, 68)         # highlight
    stud_shadow = (55, 118, 40)      # shadow
    
    img = Image.new("RGB", (size, size), base_color)
    draw = ImageDraw.Draw(img)
    
    grid = 16
    cell = size // grid
    radius = cell // 4
    
    for i in range(grid):
        for j in range(grid):
            cx = i * cell + cell // 2
            cy = j * cell + cell // 2
            # Stud shadow
            draw.ellipse([cx - radius, cy - radius + 2, cx + radius, cy + radius + 2], fill=stud_shadow)
            # Stud body
            draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=stud_top)
    return img

# ── Geometry Primitives ──────────────────────────────────────────────────────

def box_geometry(sx: float, sy: float, sz: float):
    """Generate a box mesh split per face with normals and UVs."""
    hx, hy, hz = sx / 2.0, sy / 2.0, sz / 2.0
    faces = [
        ((0, 0, 1),  [(-hx, -hy,  hz), ( hx, -hy,  hz), ( hx,  hy,  hz), (-hx,  hy,  hz)]),
        ((0, 0, -1), [(-hx, -hy, -hz), (-hx,  hy, -hz), ( hx,  hy, -hz), ( hx, -hy, -hz)]),
        ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy,  hz), (-hx,  hy,  hz), (-hx,  hy, -hz)]),
        ((1, 0, 0),  [( hx, -hy, -hz), ( hx,  hy, -hz), ( hx,  hy,  hz), ( hx, -hy,  hz)]),
        ((0, 1, 0),  [(-hx,  hy, -hz), (-hx,  hy,  hz), ( hx,  hy,  hz), ( hx,  hy, -hz)]),
        ((0, -1, 0), [(-hx, -hy, -hz), ( hx, -hy, -hz), ( hx, -hy,  hz), (-hx, -hy,  hz)]),
    ]
    verts, normals, uvs, indices = [], [], [], []
    for n, quad in faces:
        base = len(verts)
        for k, p in enumerate(quad):
            verts.append(list(p))
            normals.append(list(n))
            uvs.append([[0, 0], [1, 0], [1, 1], [0, 1]][k])
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return verts, normals, uvs, indices

def matrix_translate(tx: float, ty: float, tz: float) -> np.ndarray:
    return np.array([
        [1, 0, 0, tx],
        [0, 1, 0, ty],
        [0, 0, 1, tz],
        [0, 0, 0, 1]
    ], dtype=np.float64)

def matrix_rotate_z(deg: float) -> np.ndarray:
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    return np.array([
        [c, -s, 0, 0],
        [s,  c, 0, 0],
        [0,  0, 1, 0],
        [0,  0, 0, 1]
    ], dtype=np.float64)

def matrix_rotate_x(deg: float) -> np.ndarray:
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    return np.array([
        [1, 0,  0, 0],
        [0, c, -s, 0],
        [0, s,  c, 0],
        [0, 0,  0, 1]
    ], dtype=np.float64)

def matrix_rotate_y(deg: float) -> np.ndarray:
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    return np.array([
        [ c, 0, s, 0],
        [ 0, 1, 0, 0],
        [-s, 0, c, 0],
        [ 0, 0, 0, 1]
    ], dtype=np.float64)

def bake_animated_mesh(verts, normals, uvs, indices, transform_func, frames: int) -> dict:
    """Bake vertex positions per frame using 4x4 matrix transforms."""
    base_mesh = {
        "vertices": [
            {
                "position": [round(float(c), 4) for c in p],
                "normal": list(n),
                "uv": [round(float(u), 4) for u in uv]
            }
            for p, n, uv in zip(verts, normals, uvs)
        ],
        "indices": list(indices)
    }
    samples = []
    for f in range(frames):
        m = transform_func(f)
        pos_f = []
        for p in verts:
            v4 = np.array([p[0], p[1], p[2], 1.0], dtype=np.float64)
            res = m @ v4
            pos_f.append([round(float(res[0]), 4), round(float(res[1]), 4), round(float(res[2]), 4)])
        samples.append({
            "frame": f,
            "positions": pos_f
        })
    base_mesh["animation"] = samples
    return base_mesh

# ── Build Roblox Character Components ────────────────────────────────────────

def build_roblox_scene():
    frames = FRAMES
    
    # Dimensions of classic Roblox Noob:
    # Torso: 80w x 90h x 45d
    # Head: 52w x 52h x 52d
    # Arms: 38w x 88h x 38d
    # Legs: 38w x 88h x 38d
    
    layers = []
    
    # 1. Fondale Cielo
    layers.append({
        "id": "sky_backdrop",
        "type": "image",
        "asset": "assets/roblox_sky.png",
        "size": [2112.0, 1176.0],
        "position": [-528.0, -294.0],
        "enable_3d": False,
        "opacity": 1.0,
        "start_frame": 0,
        "duration_frames": frames,
        "color": [1, 1, 1, 1]
    })
    
    # 2. Baseplate (Green Studded Floor)
    floor_v, floor_n, floor_uv, floor_idx = box_geometry(1400.0, 10.0, 1400.0)
    floor_mesh = {
        "vertices": [
            {"position": p, "normal": n, "uv": u}
            for p, n, u in zip(floor_v, floor_n, floor_uv)
        ],
        "indices": floor_idx,
        "material": {"diffuse": 0.8, "specular": 0.2, "shininess": 15.0, "base_color": [0.32, 0.68, 0.24, 1.0]}
    }
    layers.append({
        "id": "baseplate_floor",
        "type": "mesh",
        "size": [1.0, 1.0],
        "position": [0.0, -145.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "enable_3d": True,
        "screen_space": False,
        "loop": False,
        "opacity": 1.0,
        "start_frame": 0,
        "duration_frames": frames,
        "color": [0.35, 0.72, 0.25, 1.0],
        "inline_mesh": floor_mesh
    })
    
    # Helper for rhythmic bounce
    def character_bounce_y(f: int) -> float:
        t = f / FPS
        return 7.0 * math.sin(4.0 * math.pi * t)
    
    # 3. Torso (Blue #005FB8)
    torso_v, torso_n, torso_uv, torso_idx = box_geometry(84.0, 92.0, 44.0)
    def torso_tf(f: int):
        by = character_bounce_y(f)
        tilt = 1.5 * math.sin(2.0 * math.pi * f / FPS)
        return matrix_translate(0.0, by, 0.0) @ matrix_rotate_z(tilt)
    
    torso_baked = bake_animated_mesh(torso_v, torso_n, torso_uv, torso_idx, torso_tf, frames)
    torso_baked["material"] = {"diffuse": 0.85, "specular": 0.5, "shininess": 35.0, "base_color": [0.0, 0.44, 0.85, 1.0]}
    layers.append({
        "id": "roblox_torso",
        "type": "mesh",
        "size": [1.0, 1.0],
        "position": [0.0, -5.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "enable_3d": True,
        "opacity": 1.0,
        "start_frame": 0,
        "duration_frames": frames,
        "color": [0.0, 0.44, 0.85, 1.0],
        "inline_mesh": torso_baked
    })
    
    # 4. Head (Yellow #FED500)
    head_v, head_n, head_uv, head_idx = box_geometry(56.0, 56.0, 54.0)
    def head_tf(f: int):
        by = character_bounce_y(f)
        # Head bobs and turns playfully to look at camera
        t = f / FPS
        yaw = 8.0 * math.sin(2.0 * math.pi * t * 0.5)
        tilt = 3.0 * math.cos(2.0 * math.pi * t)
        return matrix_translate(0.0, 75.0 + by, 0.0) @ matrix_rotate_y(yaw) @ matrix_rotate_z(tilt)
        
    head_baked = bake_animated_mesh(head_v, head_n, head_uv, head_idx, head_tf, frames)
    head_baked["material"] = {"diffuse": 0.9, "specular": 0.6, "shininess": 40.0, "base_color": [1.0, 0.84, 0.05, 1.0]}
    layers.append({
        "id": "roblox_head",
        "type": "mesh",
        "size": [1.0, 1.0],
        "position": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "enable_3d": True,
        "opacity": 1.0,
        "start_frame": 0,
        "duration_frames": frames,
        "color": [1.0, 0.84, 0.05, 1.0],
        "inline_mesh": head_baked
    })
    
    # 5. Face: Left Eye & Right Eye & Smile
    eye_v, eye_n, eye_uv, eye_idx = box_geometry(8.0, 14.0, 4.0)
    def left_eye_tf(f: int):
        h_mat = head_tf(f)
        # Position relative to head center: x=-13, y=5, z=28
        offset = matrix_translate(-13.0, 5.0, 28.0)
        return h_mat @ offset
    
    def right_eye_tf(f: int):
        h_mat = head_tf(f)
        offset = matrix_translate(13.0, 5.0, 28.0)
        return h_mat @ offset

    smile_v, smile_n, smile_uv, smile_idx = box_geometry(26.0, 6.0, 4.0)
    def smile_tf(f: int):
        h_mat = head_tf(f)
        offset = matrix_translate(0.0, -12.0, 28.0)
        return h_mat @ offset

    leye_baked = bake_animated_mesh(eye_v, eye_n, eye_uv, eye_idx, left_eye_tf, frames)
    reye_baked = bake_animated_mesh(eye_v, eye_n, eye_uv, eye_idx, right_eye_tf, frames)
    smile_baked = bake_animated_mesh(smile_v, smile_n, smile_uv, smile_idx, smile_tf, frames)
    
    for eid, ebake in [("left_eye", leye_baked), ("right_eye", reye_baked), ("smile", smile_baked)]:
        ebake["material"] = {"diffuse": 0.2, "specular": 0.1, "shininess": 10.0, "base_color": [0.05, 0.05, 0.05, 1.0]}
        layers.append({
            "id": eid, "type": "mesh", "size": [1.0, 1.0], "position": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0], "enable_3d": True, "opacity": 1.0,
            "start_frame": 0, "duration_frames": frames, "color": [0.05, 0.05, 0.05, 1.0],
            "inline_mesh": ebake
        })

    # 6. Right Arm (THE WAVING ARM!)
    # Shoulder pivot at x=64, y=36, z=0
    arm_v, arm_n, arm_uv, arm_idx = box_geometry(38.0, 88.0, 38.0)
    def right_arm_tf(f: int):
        by = character_bounce_y(f)
        t = f / FPS
        # Wave angle oscillates rapidly between 110 deg and 150 deg!
        wave_angle = 125.0 + 22.0 * math.sin(6.0 * math.pi * t)
        forward_swing = 10.0 * math.cos(3.0 * math.pi * t)
        
        # Translate to shoulder pivot, rotate, translate arm center down
        shoulder = matrix_translate(63.0, 35.0 + by, 0.0)
        rot = matrix_rotate_z(wave_angle) @ matrix_rotate_x(forward_swing)
        offset_down = matrix_translate(0.0, -40.0, 0.0)
        return shoulder @ rot @ offset_down

    rarm_baked = bake_animated_mesh(arm_v, arm_n, arm_uv, arm_idx, right_arm_tf, frames)
    rarm_baked["material"] = {"diffuse": 0.9, "specular": 0.5, "shininess": 35.0, "base_color": [1.0, 0.84, 0.05, 1.0]}
    layers.append({
        "id": "roblox_right_arm_waving",
        "type": "mesh", "size": [1.0, 1.0], "position": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0], "enable_3d": True, "opacity": 1.0,
        "start_frame": 0, "duration_frames": frames, "color": [1.0, 0.84, 0.05, 1.0],
        "inline_mesh": rarm_baked
    })

    # 7. Left Arm (Gently swinging at side)
    def left_arm_tf(f: int):
        by = character_bounce_y(f)
        t = f / FPS
        sway_z = -12.0 + 6.0 * math.sin(3.0 * math.pi * t)
        sway_x = 15.0 * math.sin(4.0 * math.pi * t)
        shoulder = matrix_translate(-63.0, 35.0 + by, 0.0)
        rot = matrix_rotate_z(sway_z) @ matrix_rotate_x(sway_x)
        offset_down = matrix_translate(0.0, -40.0, 0.0)
        return shoulder @ rot @ offset_down

    larm_baked = bake_animated_mesh(arm_v, arm_n, arm_uv, arm_idx, left_arm_tf, frames)
    larm_baked["material"] = {"diffuse": 0.9, "specular": 0.5, "shininess": 35.0, "base_color": [1.0, 0.84, 0.05, 1.0]}
    layers.append({
        "id": "roblox_left_arm",
        "type": "mesh", "size": [1.0, 1.0], "position": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0], "enable_3d": True, "opacity": 1.0,
        "start_frame": 0, "duration_frames": frames, "color": [1.0, 0.84, 0.05, 1.0],
        "inline_mesh": larm_baked
    })

    # 8. Left Leg & Right Leg (Green #5EA700)
    leg_v, leg_n, leg_uv, leg_idx = box_geometry(38.0, 88.0, 38.0)
    def left_leg_tf(f: int):
        by = character_bounce_y(f)
        t = f / FPS
        step = 8.0 * math.sin(4.0 * math.pi * t)
        hip = matrix_translate(-22.0, -50.0 + by, 0.0)
        rot = matrix_rotate_x(step)
        offset_down = matrix_translate(0.0, -44.0, 0.0)
        return hip @ rot @ offset_down

    def right_leg_tf(f: int):
        by = character_bounce_y(f)
        t = f / FPS
        step = -8.0 * math.sin(4.0 * math.pi * t)
        hip = matrix_translate(22.0, -50.0 + by, 0.0)
        rot = matrix_rotate_x(step)
        offset_down = matrix_translate(0.0, -44.0, 0.0)
        return hip @ rot @ offset_down

    lleg_baked = bake_animated_mesh(leg_v, leg_n, leg_uv, leg_idx, left_leg_tf, frames)
    rleg_baked = bake_animated_mesh(leg_v, leg_n, leg_uv, leg_idx, right_leg_tf, frames)
    for lid, lbake in [("roblox_left_leg", lleg_baked), ("roblox_right_leg", rleg_baked)]:
        lbake["material"] = {"diffuse": 0.85, "specular": 0.3, "shininess": 20.0, "base_color": [0.35, 0.72, 0.25, 1.0]}
        layers.append({
            "id": lid, "type": "mesh", "size": [1.0, 1.0], "position": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0], "enable_3d": True, "opacity": 1.0,
            "start_frame": 0, "duration_frames": frames, "color": [0.35, 0.72, 0.25, 1.0],
            "inline_mesh": lbake
        })

    # 9. Lucky Block / Roblox Red Brick nearby
    prop_v, prop_n, prop_uv, prop_idx = box_geometry(60.0, 60.0, 60.0)
    def prop_tf(f: int):
        t = f / FPS
        spin = 45.0 * t
        return matrix_translate(160.0, -110.0, -30.0) @ matrix_rotate_y(spin)
        
    prop_baked = bake_animated_mesh(prop_v, prop_n, prop_uv, prop_idx, prop_tf, frames)
    prop_baked["material"] = {"diffuse": 0.9, "specular": 0.6, "shininess": 50.0, "base_color": [0.95, 0.2, 0.2, 1.0]}
    layers.append({
        "id": "roblox_lucky_block",
        "type": "mesh", "size": [1.0, 1.0], "position": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0], "enable_3d": True, "opacity": 1.0,
        "start_frame": 0, "duration_frames": frames, "color": [0.95, 0.2, 0.2, 1.0],
        "inline_mesh": prop_baked
    })

    # ── Camera Movement (Cinematic Orbit & Dolly) ───────────────────────────
    camera_samples = []
    cam_step = 6  # sample every 6 frames for smooth spline
    for f in list(range(0, frames, cam_step)) + [frames - 1]:
        prog = f / (frames - 1)
        # Orbit from slight left-low angle to center-right dynamic shot
        cam_x = -110.0 + 190.0 * math.sin(prog * 0.9 * math.pi)
        cam_y = 15.0 + 35.0 * math.sin(prog * math.pi)
        cam_z = -560.0 + 120.0 * math.sin(prog * math.pi * 0.8)
        
        yaw = -7.0 + 15.0 * prog
        pitch = 3.0 + 2.0 * math.sin(prog * math.pi)
        
        camera_samples.append({
            "frame": f,
            "camera": {
                "type": "perspective",
                "position": [round(cam_x, 3), round(cam_y, 3), round(cam_z, 3)],
                "rotation_deg": [round(pitch, 3), round(yaw, 3), 0.0],
                "fov_deg": 32.0,
                "near": 1.0,
                "far": 10000.0,
                "zoom": 1.0
            }
        })

    # ── Lighting Rig (Vibrant Sunny Daytime) ────────────────────────────────
    lighting_rig = {
        "key_direction": [0.45, -0.65, 0.60],
        "key_intensity": 1.6,
        "key_color": [1.0, 0.98, 0.92, 1.0],
        "ambient_intensity": 0.35,
        "ambient_color": [0.65, 0.78, 0.95, 1.0],
        "rim_enabled": True,
        "rim_intensity": 1.4,
        "rim_power": 3.0,
        "rim_color": [1.0, 0.92, 0.65, 1.0]
    }

    plan = {
        "schema": "chronon.render-plan.v3",
        "version": 3,
        "job_id": "roblox_noob_wave_showcase",
        "camera": {
            "type": "perspective",
            "position": [-110.0, 15.0, -560.0],
            "rotation_deg": [3.0, -7.0, 0.0],
            "fov_deg": 32.0,
            "near": 1.0,
            "far": 10000.0,
            "zoom": 1.0
        },
        "camera_samples": camera_samples,
        "canvas": {
            "width": W,
            "height": H,
            "fps_num": FPS,
            "fps_den": 1,
            "duration_frames": frames
        },
        "output": {
            "path": "roblox_noob_wave.mp4",
            "format": "mp4"
        },
        "lighting_rig": lighting_rig,
        "layers": layers
    }
    return plan

def main():
    print("1. Generating textures...")
    make_roblox_sky(1600, 900).save(os.path.join(ASSETS, "roblox_sky.png"))
    make_roblox_baseplate(1024).save(os.path.join(ASSETS, "roblox_baseplate.png"))
    
    print("2. Building Roblox 3D scene plan...")
    plan = build_roblox_scene()
    plan_path = os.path.join(PLANS, "roblox_noob_wave.plan.json")
    with open(plan_path, "w") as f:
        json.dump(plan, f, indent=1)
    print(f"Plan written to {plan_path} ({len(plan['layers'])} layers, {FRAMES} frames)")

if __name__ == "__main__":
    main()
