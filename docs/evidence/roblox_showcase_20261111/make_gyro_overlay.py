#!/usr/bin/env python3
"""Generate a high-tech minimalist telemetry overlay for the Precision Gyroscope Showcase (1920x1080)."""

import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
os.makedirs(ASSETS, exist_ok=True)

W, H = 1920, 1080
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

try:
    font_bold = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24)
    font_med = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18)
    font_mono = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 17)
    font_mono_small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14)
except Exception:
    font_bold = ImageFont.load_default()
    font_med = font_bold
    font_mono = font_bold
    font_mono_small = font_bold

# ── TOP BANNER ───────────────────────────────────────────────────────────────
draw.rectangle([(0, 0), (W, 72)], fill=(12, 14, 18, 220))
draw.line([(0, 72), (W, 72)], fill=(0, 200, 255, 120), width=2)

# Left: Badge
draw.rounded_rectangle([(40, 14), (210, 56)], radius=6, fill=(0, 180, 255, 35), outline=(0, 200, 255, 180), width=1)
draw.text((52, 23), "CHRONON 3D", fill=(0, 230, 255, 255), font=font_bold)

draw.text((230, 24), "PRECISION 3-AXIS CARDAN GYROSCOPE", fill=(255, 255, 255, 240), font=font_bold)
draw.text((820, 28), "•  STUDIO CYCLORAMA LOOK-DEV", fill=(160, 175, 195, 220), font=font_med)

# Right: Technical specs
draw.text((W - 470, 26), "1080p @ 60 FPS  |  360 RPM ROTOR", fill=(0, 230, 255, 230), font=font_mono)

# ── CORNER ALIGNMENT RETICLES ───────────────────────────────────────────────
reticle_color = (0, 200, 255, 80)
arm = 28
draw.line([(70, 100), (70 + arm, 100)], fill=reticle_color, width=2)
draw.line([(70, 100), (70, 100 + arm)], fill=reticle_color, width=2)
draw.line([(W - 70, 100), (W - 70 - arm, 100)], fill=reticle_color, width=2)
draw.line([(W - 70, 100), (W - 70, 100 + arm)], fill=reticle_color, width=2)
draw.line([(70, H - 110), (70 + arm, H - 110)], fill=reticle_color, width=2)
draw.line([(70, H - 110), (70, H - 110 - arm)], fill=reticle_color, width=2)
draw.line([(W - 70, H - 110), (W - 70 - arm, H - 110)], fill=reticle_color, width=2)
draw.line([(W - 70, H - 110), (W - 70, H - 110 - arm)], fill=reticle_color, width=2)

# ── BOTTOM BANNER (PRIMITIVES & KINEMATICS FOOTER) ───────────────────────────
draw.rectangle([(0, H - 76), (W, H)], fill=(12, 14, 18, 230))
draw.line([(0, H - 76), (W, H - 76)], fill=(0, 200, 255, 100), width=1)

# Left footer: Primitive Breakdown
draw.text((45, H - 62), "GEOMETRIES:", fill=(0, 210, 255, 230), font=font_mono)
draw.text((185, H - 61), "TorusGeometry (Gimbals)  •  CylinderGeometry (Pedestal/Trunnions/Flywheel)  •  CapsuleGeometry (Core)  •  TubeGeometry (Mount)",
          fill=(230, 235, 245, 240), font=font_med)

draw.text((45, H - 35), "KINEMATICS:", fill=(0, 210, 255, 230), font=font_mono)
draw.text((185, H - 34), "Cardan Hierarchy (X/Y/Z)  •  Rigid Hinge Bearings  •  Flywheel 360 RPM Spin  •  Transformed Normals (M^-1)^T",
          fill=(160, 175, 195, 220), font=font_mono_small)

output_path = os.path.join(ASSETS, "gyro_overlay_1080p.png")
img.save(output_path, "PNG")
print(f"Overlay successfully generated: {output_path}")
