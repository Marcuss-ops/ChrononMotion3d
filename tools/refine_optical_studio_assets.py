#!/usr/bin/env python3
"""Refine the optical-studio plates without touching the camera evidence.

- `middle.png`: the car plate has a baked reflection with a hard cut and no
  contact shadow.  The original is kept as `middle_original.png`; the refined
  plate fades the reflection with a smooth alpha ramp and adds a soft shadow
  under the wheels, so DoF no longer turns the cut into a dark halo.
- `cards/card_glow.png`: warm radial glow placed behind the supercar card.
- `cards/card_backdrop_v2.png`: dark perspective floor + vignette replacing
  the flat black carousel background.

Usage: python3 tools/refine_optical_studio_assets.py ASSETS_DIR
"""
import math
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def _smoothstep(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def refine_car(assets: Path) -> None:
    source = assets / "middle_original.png"
    current = assets / "middle.png"
    if not source.exists():
        shutil.copy2(current, source)
    image = Image.open(source).convert("RGBA")
    width, height = image.size

    # Replace the baked reflection instead of fading it.  The old plate's
    # reflection was a dark rectangle that ended in a hard horizontal cut; a
    # fade alone left a grey band.  Keep the car body, clear everything below
    # the wheels, and mirror the body back with a short, blurred falloff.
    car_bottom = int(0.62 * height)
    body = image.crop((0, 0, width, car_bottom))
    alpha = np.array(image.getchannel("A"), dtype=np.float32) / 255.0
    alpha[car_bottom:, :] = 0.0
    image.putalpha(Image.fromarray(np.clip(alpha * 255.0, 0, 255).astype(np.uint8)))

    reflection_height = int(0.24 * height)
    reflection = body.transpose(Image.FLIP_TOP_BOTTOM).resize(
        (width, reflection_height), Image.LANCZOS)
    reflection_alpha = np.array(reflection.getchannel("A"), dtype=np.float32) / 255.0
    for y in range(reflection_height):
        t = y / reflection_height
        fade_in = min(1.0, t / 0.18)
        reflection_alpha[y, :] *= 0.50 * fade_in * (1.0 - t) ** 1.6
    reflection.putalpha(Image.fromarray(
        np.clip(reflection_alpha * 255.0, 0, 255).astype(np.uint8)))
    reflection = reflection.filter(ImageFilter.GaussianBlur(2.5))
    image.alpha_composite(reflection, (0, car_bottom))

    # The reflection itself fades in under the body, so no separate contact
    # ellipse is needed: a blurred ellipse here read as a hard dark slab.
    image.save(current)


def make_glow(assets: Path, name: str = "card_glow.png") -> None:
    width, height = 1600, 1000
    yy, xx = np.mgrid[0:height, 0:width]
    nx = (xx - width / 2.0) / (width / 2.0)
    ny = (yy - height / 2.0) / (height / 2.0)
    radius = np.sqrt(nx * nx + ny * ny)
    alpha = np.clip(1.0 - radius, 0.0, 1.0) ** 2.2
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    rgba[..., 0] = 255
    rgba[..., 1] = 150
    rgba[..., 2] = 90
    rgba[..., 3] = (alpha * 45.0).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(assets / "cards" / name)


def make_backdrop(assets: Path, name: str = "card_backdrop_v2.png") -> None:
    width, height = 3840, 2160
    # Pure black stage.  The renderer lifts low values (linear -> sRGB) and
    # mipmaps the far plate, so any grey gradient in the source turns into a
    # flat mid-grey wall.  Keep the background at zero and draw only the grid.
    image = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    draw = ImageDraw.Draw(image, "RGBA")
    horizon = int(height * 0.55)
    vpx, vpy = width // 2, horizon
    for x in range(-width, width * 2, 120):
        draw.line((x, height, vpx, vpy), fill=(2, 2, 2, 255), width=4)
    for i in range(1, 22):
        t = (i / 22.0) ** 2.1
        y = int(vpy + (height - vpy) * t)
        draw.line((0, y, width, y), fill=(2, 2, 2, 255), width=4)
    image.convert("RGB").save(assets / "cards" / name)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: refine_optical_studio_assets.py ASSETS_DIR")
    assets = Path(sys.argv[1])
    refine_car(assets)
    make_glow(assets)
    make_backdrop(assets)
    print(f"refined optical-studio assets in {assets}")


if __name__ == "__main__":
    main()
