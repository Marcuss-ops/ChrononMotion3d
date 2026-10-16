#!/usr/bin/env python3
"""Seam check and backplate fix for the Character V2.1 multilayer assets.

The head layer rotates around the neck pivot. Any pixel of the head's rest
silhouette that is vacated by the rotation and not covered by another layer
shows the backdrop: that is the rectangular "slit" seen in V2.0.

Commands:
  check  Sweep head rotations and count exposed pixels (must be 0).
  fix    Fill the torso alpha behind the head with an inpainted backplate.
         The previous torso layer is kept as layer_torso_prev.png.

Usage (from ChrononMotion3D/render/spike):
  python3 character_v2_1_seams.py check
  python3 character_v2_1_seams.py fix
"""
import argparse
import os
import shutil

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
LAYERS_DIR = os.path.join(HERE, "assets", "layers_v2_1")
NAMES = ["torso", "head", "upper_arm", "forearm", "hand"]

# Neck pivot from creator_character_v2_1.cpp (scene units, plane 2.45 high).
PLANE_H = 2.45
IMG_W, IMG_H = 1376, 768
PLANE_W = PLANE_H * IMG_W / IMG_H
NECK_PIVOT_SCENE = (-0.057 * PLANE_W, 0.076 * PLANE_H)


def pivot_px():
    x, y = NECK_PIVOT_SCENE
    return ((x / PLANE_W + 0.5) * IMG_W, (0.5 - y / PLANE_H) * IMG_H)


def load_alpha(name, directory=LAYERS_DIR):
    bgra = cv2.imread(os.path.join(directory, f"layer_{name}.png"), cv2.IMREAD_UNCHANGED)
    return cv2.cvtColor(bgra, cv2.COLOR_BGRA2RGBA)


def rotate_mask(mask, angle_deg):
    cx, cy = pivot_px()
    m = cv2.getRotationMatrix2D((cx, cy), angle_deg, 1.0)
    return cv2.warpAffine(mask, m, (IMG_W, IMG_H), flags=cv2.INTER_NEAREST, borderValue=0)


def exposed_pixels(layers, angle_deg):
    """Pixels of the head's rest silhouette exposed by a rotation of the head.

    Other layers are taken at rest pose. A pixel is exposed when the rest head
    covers it, the rotated head does not, and no other layer covers it.
    """
    head_rest = layers["head"][..., 3] > 0
    head_rot = rotate_mask((layers["head"][..., 3] > 0).astype(np.uint8) * 255, angle_deg) > 0
    other = np.zeros_like(head_rest)
    for n in NAMES:
        if n != "head":
            other |= layers[n][..., 3] > 0
    return int((head_rest & ~head_rot & ~other).sum())


def check(directory):
    layers = {n: load_alpha(n, directory) for n in NAMES}
    print(f"[{directory}] exposed head-rest pixels per head rotation (rad->deg):")
    worst = 0
    for deg in [0, 5, 10, 15, 20, 25, 30, 40, 50, 60, -5, -10, -15, -20, -25, -30, -40, -50, -60]:
        e = exposed_pixels(layers, deg)
        worst = max(worst, e)
        print(f"  {deg:+4d} deg : {e}")
    print(f"worst exposed pixels: {worst}")
    return worst


def fix():
    torso_path = os.path.join(LAYERS_DIR, "layer_torso.png")
    backup = os.path.join(LAYERS_DIR, "layer_torso_prev.png")
    if not os.path.exists(backup):
        shutil.copy(torso_path, backup)

    layers = {n: load_alpha(n) for n in NAMES}
    # Start from the original torso: the current file may already be fixed.
    torso = cv2.cvtColor(
        cv2.imread(backup, cv2.IMREAD_UNCHANGED), cv2.COLOR_BGRA2RGBA)
    head_alpha = layers["head"][..., 3] > 0

    # Area of the head silhouette (and a sweep margin) with no torso behind it.
    margin = np.ones((15, 15), np.uint8)
    # Every pixel the head can vacate lies inside its rest silhouette, so the
    # rest silhouette (plus margin) is enough for any rotation.
    # No dilation: a dilated fill shows as a warm halo around the hair.
    sweep = head_alpha.astype(np.uint8) * 255
    backplate_mask = ((sweep > 0) & (torso[..., 3] == 0)).astype(np.uint8) * 255

    # Inpaint colour from the surrounding torso/backdrop pixels.
    # Transparent pixels carry green-screen RGB (~60/79/68); inpainting pulls that
    # colour into the backplate. Replace it with the mean colour of the opaque torso.
    rgb_src = torso[..., :3].copy()
    opaque_mean = rgb_src[torso[..., 3] == 255].mean(axis=0)
    rgb_src[torso[..., 3] == 0] = opaque_mean.astype(np.uint8)
    bgr = cv2.cvtColor(np.ascontiguousarray(rgb_src), cv2.COLOR_RGB2BGR)
    filled = cv2.inpaint(bgr, backplate_mask, 9, cv2.INPAINT_TELEA)
    rgb_filled = cv2.cvtColor(filled, cv2.COLOR_BGR2RGB)

    out = torso.copy()
    out[..., :3][backplate_mask > 0] = rgb_filled[backplate_mask > 0]
    out[..., 3][backplate_mask > 0] = 255

    # Write BGRA for cv2.imwrite.
    out_bgra = cv2.cvtColor(out, cv2.COLOR_RGBA2BGRA)
    cv2.imwrite(torso_path, out_bgra)
    print(f"backplate pixels filled: {int((backplate_mask > 0).sum())}")
    print(f"wrote {torso_path} (previous saved as {backup})")


def despill():
    """Remove green-screen spill from every layer's colour.

    Green is clamped to max(R, B): pixels where green dominates lose the
    fringe, neutral and skin tones are untouched.
    """
    for n in NAMES:
        path = os.path.join(LAYERS_DIR, f"layer_{n}.png")
        backup = os.path.join(LAYERS_DIR, f"layer_{n}_predespill.png")
        if not os.path.exists(backup):
            shutil.copy(path, backup)
        rgba = cv2.cvtColor(cv2.imread(path, cv2.IMREAD_UNCHANGED), cv2.COLOR_BGRA2RGBA)
        r, g, b = rgba[..., 0], rgba[..., 1], rgba[..., 2]
        cap = np.maximum(r, b)
        spilled = (g > cap + 4) & (rgba[..., 3] > 0)
        g2 = g.copy()
        g2[spilled] = cap[spilled]
        rgba[..., 1] = g2
        cv2.imwrite(path, cv2.cvtColor(rgba, cv2.COLOR_RGBA2BGRA))
        print(f"{n}: despilled {int(spilled.sum())} px")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["check", "fix", "despill"])
    parser.add_argument("--dir", default=LAYERS_DIR, help="layer directory for check")
    args = parser.parse_args()
    if args.command == "check":
        worst = check(args.dir)
        raise SystemExit(0 if worst == 0 else 1)
    if args.command == "despill":
        despill()
    else:
        fix()


if __name__ == "__main__":
    main()
