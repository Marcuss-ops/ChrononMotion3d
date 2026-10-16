#!/usr/bin/env python3
"""Validate the full-GPU T13 video output against its threepp source."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

W, H = 1920, 1080
FRAME_BYTES = W * H * 3
OVERLAYS = {
    "headline": (730, 870, 1280, 928),
    "subhead": (690, 936, 1300, 990),
    "marker": (565, 875, 600, 985),
}


def frames(path: Path):
    proc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-vsync", "0", "-"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert proc.stdout is not None
    try:
        while True:
            data = proc.stdout.read(FRAME_BYTES)
            if not data:
                break
            if len(data) != FRAME_BYTES:
                raise RuntimeError(f"truncated frame from {path}: {len(data)} bytes")
            yield np.frombuffer(data, dtype=np.uint8).reshape(H, W, 3)
    finally:
        proc.stdout.close()
        status = proc.wait()
        if status != 0:
            raise RuntimeError(f"ffmpeg decode failed for {path}: {status}")


def summarize(values: list[float]) -> dict:
    return {"min": float(min(values)), "mean": float(np.mean(values)), "max": float(max(values))}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    source_iter, output_iter = iter(frames(args.source)), iter(frames(args.output))
    changed = {name: [] for name in OVERLAYS}
    vehicle_mae, vehicle_psnr = [], []
    count = 0
    while True:
        try:
            src = next(source_iter)
        except StopIteration:
            src = None
        try:
            out = next(output_iter)
        except StopIteration:
            out = None
        if src is None and out is None:
            break
        if src is None or out is None:
            raise RuntimeError(f"source/output frame count differs at frame {count}")

        mask = np.max(src[:830], axis=2) > 20
        ys, xs = np.where(mask)
        if not len(xs):
            raise RuntimeError(f"source vehicle not found in frame {count}")
        x0, x1 = int(xs.min()), int(xs.max()) + 1
        y0, y1 = int(ys.min()), int(ys.max()) + 1
        a, b = src[y0:y1, x0:x1].astype(np.int16), out[y0:y1, x0:x1].astype(np.int16)
        delta = a - b
        mse = float(np.mean(delta.astype(np.float64) ** 2))
        vehicle_mae.append(float(np.mean(np.abs(delta))))
        vehicle_psnr.append(99.0 if mse == 0 else float(10.0 * np.log10(255.0 * 255.0 / mse)))

        for name, (ox0, oy0, ox1, oy1) in OVERLAYS.items():
            sroi, oroi = src[oy0:oy1, ox0:ox1], out[oy0:oy1, ox0:ox1]
            changed[name].append(int(np.sum(np.max(np.abs(sroi.astype(np.int16) - oroi.astype(np.int16)), axis=2) > 8)))
        count += 1

    passed = count == 150 and all(min(values) >= 1000 for values in changed.values())
    report = {
        "test_id": "T13-full-gpu-opaque",
        "status": "PASS" if passed else "FAIL",
        "frames": count,
        "dimensions": [W, H],
        "output": str(args.output),
        "threepp_source": str(args.source),
        "vehicle_roi_vs_source": {
            "mae_rgb": summarize(vehicle_mae),
            "psnr_db": summarize(vehicle_psnr),
            "note": "source-to-output image difference is measured; no software-backend parity is implied",
        },
        "overlay_changed_pixels_gt_8_per_frame": {
            name: {**summarize(values), "minimum_gate": 1000}
            for name, values in changed.items()
        },
        "pass_criteria": "all 150 frames decode at 1920x1080 and each Chronon overlay ROI changes at least 1000 pixels in every frame",
        "gpu_execution_evidence": str(args.output) + ".timing.json",
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
