#!/usr/bin/env python3
"""Generate original PNG plates for plan_optical_studio; requires Pillow."""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_optical_studio_assets.py OUTPUT_DIR FONT_TTF")
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    font_path = sys.argv[2]
    font = lambda size: ImageFont.truetype(font_path, size)

    # A quiet navy gradient with depth cues; no external imagery.
    background = Image.new("RGB", (1920, 1080))
    pixels = background.load()
    for y in range(1080):
        for x in range(1920):
            glow = math.exp(-(((x - 1080) / 760) ** 2 + ((y - 430) / 600) ** 2))
            pixels[x, y] = (int(10 + 9 * glow), int(17 + 22 * glow), int(31 + 29 * glow))
    d = ImageDraw.Draw(background, "RGBA")
    for x in range(0, 1921, 120):
        d.line((x, 0, x, 1080), fill=(149, 195, 224, 15), width=1)
    for y in range(0, 1081, 120):
        d.line((0, y, 1920, y), fill=(149, 195, 224, 15), width=1)
    d.rounded_rectangle((85, 82, 548, 146), 30, fill=(111, 190, 205, 40), outline=(135, 221, 229, 80), width=2)
    d.text((112, 99), "CHRONON   /   OPTICAL STUDY", font=font(24), fill=(200, 232, 235, 235))
    d.text((88, 908), "THREE PLANES.  ONE MOVING CAMERA.", font=font(25), fill=(181, 213, 222, 190))
    d.line((90, 965, 1830, 965), fill=(157, 202, 215, 75), width=2)
    d.text((88, 985), "MOTION  /  PARALLAX  /  FOCUS", font=font(19), fill=(157, 202, 215, 160))
    d.text((1560, 985), "01  —  03", font=font(19), fill=(157, 202, 215, 160))
    background.save(output / "background.png")

    cards = [
        ("near", "01", "NEAR", "FOREGROUND", (239, 161, 105), (255, 218, 174)),
        ("middle", "02", "MID", "INTERMEDIATE", (105, 199, 195), (182, 241, 226)),
        ("far", "03", "FAR", "BACKGROUND", (139, 157, 234), (205, 215, 255)),
    ]
    for name, number, title, subtitle, accent, pale in cards:
        image = Image.new("RGBA", (730, 830), (0, 0, 0, 0))
        d = ImageDraw.Draw(image, "RGBA")
        d.rounded_rectangle((12, 12, 718, 818), 36, fill=(15, 26, 42, 242), outline=(*accent, 205), width=3)
        d.rounded_rectangle((32, 32, 698, 798), 27, outline=(208, 234, 236, 35), width=2)
        d.text((64, 61), "OPTICAL STUDY", font=font(25), fill=(*pale, 220))
        d.text((582, 52), number, font=font(43), fill=(*accent, 255))
        d.line((64, 116, 666, 116), fill=(*accent, 115), width=2)
        cx, cy = 365, 335
        for r in (174, 133, 88, 42):
            d.ellipse((cx-r, cy-r, cx+r, cy+r), outline=(*accent, 210 if r == 174 else 125), width=3)
        d.line((cx-202, cy, cx+202, cy), fill=(*pale, 130), width=2)
        d.line((cx, cy-202, cx, cy+202), fill=(*pale, 130), width=2)
        d.ellipse((cx-12, cy-12, cx+12, cy+12), fill=(*accent, 255))
        for i in range(11):
            xx = 85 + i * 55
            d.line((xx, 571, xx, 571 + (23 if i % 5 == 0 else 11)), fill=(*pale, 190), width=2)
        d.line((85, 571, 635, 571), fill=(*pale, 190), width=2)
        d.text((64, 625), title, font=font(85), fill=(245, 248, 244, 255))
        d.text((68, 744), subtitle, font=font(25), fill=(*pale, 215))
        image.save(output / f"{name}.png")
    print(f"generated optical studio plates in {output}")


if __name__ == "__main__":
    main()
