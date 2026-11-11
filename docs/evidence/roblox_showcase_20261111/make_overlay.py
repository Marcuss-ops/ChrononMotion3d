#!/usr/bin/env python3
"""Generate stylish 2D animated overlays for the Roblox Short."""
import os
import math
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")

def generate_overlays(w=1920, h=1080):
    # Static top banner
    img_top = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img_top)
    
    # Try finding a bold font, fallback to default
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    if not os.path.exists(font_path):
        font_path = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"
        
    try:
        font_title = ImageFont.truetype(font_path, 54)
        font_badge = ImageFont.truetype(font_path, 28)
        font_sub = ImageFont.truetype(font_path, 42)
    except Exception:
        font_title = font_badge = font_sub = ImageFont.load_default()

    # Top Gaming Badge
    badge_text = "ROBLOX 3D ENGINE • CHRONON"
    bw, bh = 460, 48
    bx0, by0 = (w - bw) // 2, 40
    draw.rounded_rectangle([bx0, by0, bx0 + bw, by0 + bh], radius=24, fill=(18, 18, 22, 220), outline=(255, 215, 0, 255), width=3)
    draw.text((w // 2, by0 + bh // 2), badge_text, font=font_badge, fill=(255, 225, 50, 255), anchor="mm")

    # Main Viral Title (Yellow with thick black outline)
    title_text = "POV: IL TUO PRIMO GIORNO SU ROBLOX"
    tx, ty = w // 2, 140
    # Black outline
    for dx in range(-4, 5):
        for dy in range(-4, 5):
            if dx*dx + dy*dy > 0:
                draw.text((tx + dx, ty + dy), title_text, font=font_title, fill=(0, 0, 0, 255), anchor="mm")
    # Yellow fill
    draw.text((tx, ty), title_text, font=font_title, fill=(255, 235, 30, 255), anchor="mm")

    # Bottom Speech Bubble (Classic Roblox chat bubble)
    chat_text = "OOF! CIAO A TUTTI! 👋"
    cw, ch = 540, 80
    cx0, cy0 = (w - cw) // 2, h - 160
    # Shadow
    draw.rounded_rectangle([cx0 + 4, cy0 + 6, cx0 + cw + 4, cy0 + ch + 6], radius=20, fill=(0, 0, 0, 100))
    # Bubble
    draw.rounded_rectangle([cx0, cy0, cx0 + cw, cy0 + ch], radius=20, fill=(255, 255, 255, 245), outline=(30, 30, 35, 255), width=4)
    # Speech arrow
    arrow_x = cx0 + cw // 2
    draw.polygon([(arrow_x - 15, cy0 - 2), (arrow_x + 15, cy0 - 2), (arrow_x, cy0 - 18)], fill=(255, 255, 255, 245))
    draw.line([(arrow_x - 15, cy0 - 2), (arrow_x, cy0 - 18)], fill=(30, 30, 35, 255), width=4)
    draw.line([(arrow_x + 15, cy0 - 2), (arrow_x, cy0 - 18)], fill=(30, 30, 35, 255), width=4)

    draw.text((w // 2, cy0 + ch // 2), chat_text, font=font_sub, fill=(20, 20, 25, 255), anchor="mm")

    out_path = os.path.join(ASSETS, "roblox_overlay_1080p.png")
    img_top.save(out_path)
    print(f"Overlay written to {out_path}")

if __name__ == "__main__":
    generate_overlays()
