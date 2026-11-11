#!/usr/bin/env python3
"""Generate a high-end photographic studio cyclorama background (1920x1080)."""

import os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
os.makedirs(ASSETS, exist_ok=True)

W, H = 1920, 1080

# Create radial + vertical studio gradient
y = np.linspace(0, 1, H)[:, None]
x = np.linspace(-1, 1, W)[None, :]

# Radial falloff from center-top spotlight
dist = np.sqrt(x**2 + ((y - 0.35) * 1.5)**2)
spotlight = np.exp(-dist * 1.8)

# Vertical cyclorama curve (transition from wall to floor around y=0.75)
wall_light = 0.42 + 0.18 * spotlight - 0.15 * (y - 0.35)**2
floor_dark = 0.22 + 0.10 * spotlight

# Smooth step at horizon y=0.72
horizon = 0.72
blend = 1.0 / (1.0 + np.exp(-(y - horizon) * 35.0))
luminance = wall_light * (1.0 - blend) + floor_dark * blend

# Add slight cool studio tint: R=0.92, G=0.96, B=1.04
r = np.clip(luminance * 255 * 0.90, 0, 255).astype(np.uint8)
g = np.clip(luminance * 255 * 0.95, 0, 255).astype(np.uint8)
b = np.clip(luminance * 255 * 1.04, 0, 255).astype(np.uint8)

rgb = np.stack([r, g, b], axis=-1)
img = Image.fromarray(rgb, "RGB")
draw = ImageDraw.Draw(img)

# Add subtle horizon contact line
draw.line([(0, int(H * horizon)), (W, int(H * horizon))], fill=(50, 56, 68), width=1)

out_path = os.path.join(ASSETS, "studio_cyclorama.png")
img.save(out_path, "PNG")
print(f"Studio cyclorama generated: {out_path} ({W}x{H})")
