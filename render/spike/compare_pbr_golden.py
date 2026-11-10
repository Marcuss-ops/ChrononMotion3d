#!/usr/bin/env python3
"""Compare a rendered frame against a committed PBR golden.

The golden is a regression baseline produced by the certified NVIDIA path from
pinned Khronos glTF sample assets. It is not a reference from an independent
glTF viewer: it proves the renderer still produces the same pixels, not that
the pixels match Khronos' own reference.

Tolerances are explicit and recorded in docs/THREEPP_CERTIFICATION.md:
  --max-mae 2.0            mean absolute RGB error (0..255)
  --max-delta 48           worst single channel difference
  --max-over-fraction 0.005  fraction of pixels with any channel over 16

Exit code 0 means PASS, 1 means the measured error exceeds tolerance, 2 means
the inputs could not be read.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover - optional fast path
    np = None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_rgb(path: Path):
    from PIL import Image

    with Image.open(path) as image:
        return image.convert("RGB")


def measure_fast(golden, actual, over_threshold: int):
    a = np.asarray(golden, dtype=np.int16)
    b = np.asarray(actual, dtype=np.int16)
    diff = np.abs(a - b)
    mae = float(diff.mean())
    max_delta = int(diff.max())
    over = int((diff > over_threshold).any(axis=2).sum())
    mse = float((diff.astype(np.float64) ** 2).mean())
    psnr = 99.0 if mse == 0.0 else 10.0 * __import__("math").log10(255.0 ** 2 / mse)
    return mae, max_delta, over, psnr


def measure_slow(golden, actual, over_threshold: int):
    a = golden.tobytes()
    b = actual.tobytes()
    if len(a) != len(b):
        raise ValueError("frame dimensions differ")
    total = 0
    max_delta = 0
    over = 0
    squared = 0
    pixels = len(a) // 3
    for i in range(0, len(a), 3):
        pixel_over = False
        for c in range(3):
            d = abs(a[i + c] - b[i + c])
            total += d
            squared += d * d
            if d > max_delta:
                max_delta = d
            if d > over_threshold:
                pixel_over = True
        if pixel_over:
            over += 1
    mae = total / (pixels * 3)
    mse = squared / (pixels * 3)
    psnr = 99.0 if mse == 0.0 else 10.0 * __import__("math").log10(255.0 ** 2 / mse)
    return mae, max_delta, over, psnr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-mae", type=float, default=2.0)
    parser.add_argument("--max-delta", type=int, default=48)
    parser.add_argument("--max-over-fraction", type=float, default=0.005)
    parser.add_argument("--over-threshold", type=int, default=16)
    args = parser.parse_args()

    try:
        golden = load_rgb(args.golden)
        actual = load_rgb(args.actual)
    except Exception as error:  # noqa: BLE001 - report the exact read failure
        print(f"GOLDEN_READ_FAIL {error}", file=sys.stderr)
        return 2

    if golden.size != actual.size:
        print(f"GOLDEN_SIZE_MISMATCH golden={golden.size} actual={actual.size}", file=sys.stderr)
        return 1

    if np is not None:
        mae, max_delta, over, psnr = measure_fast(golden, actual, args.over_threshold)
    else:
        mae, max_delta, over, psnr = measure_slow(golden, actual, args.over_threshold)

    pixels = golden.size[0] * golden.size[1]
    over_fraction = over / pixels
    passed = (
        mae <= args.max_mae
        and max_delta <= args.max_delta
        and over_fraction <= args.max_over_fraction
    )

    report = {
        "schema": "chrononmotion.pbr-golden-comparison.v1",
        "status": "PASS" if passed else "FAIL",
        "width": golden.size[0],
        "height": golden.size[1],
        "golden": {"path": str(args.golden), "sha256": sha256(args.golden)},
        "actual": {"path": str(args.actual), "sha256": sha256(args.actual)},
        "exact_match": sha256(args.golden) == sha256(args.actual),
        "measured": {
            "mae_rgb": round(mae, 6),
            "max_channel_delta": max_delta,
            "pixels_over_threshold": over,
            "pixels_over_threshold_fraction": round(over_fraction, 8),
            "psnr_db": round(psnr, 4),
        },
        "tolerance": {
            "max_mae_rgb": args.max_mae,
            "max_channel_delta": args.max_delta,
            "max_pixels_over_threshold_fraction": args.max_over_fraction,
            "over_threshold": args.over_threshold,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        f"GOLDEN_{'PASS' if passed else 'FAIL'} mae={mae:.6f} max_delta={max_delta} "
        f"over_fraction={over_fraction:.8f} psnr={psnr:.3f} exact={report['exact_match']}"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
