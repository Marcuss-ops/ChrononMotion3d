#!/usr/bin/env python3
"""Measure T13 software/Vulkan parity on the model and Chronon overlay ROIs."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def load_rgb(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def metrics(a: np.ndarray, b: np.ndarray) -> dict:
    delta = a.astype(np.float64) - b.astype(np.float64)
    mse = float(np.mean(delta * delta))
    luminance = lambda image: (0.2126 * image[:, :, 0] + 0.7152 * image[:, :, 1] + 0.0722 * image[:, :, 2])
    lum_a = luminance(a)
    lum_b = luminance(b)
    return {
        "mae_rgb": float(np.mean(np.abs(delta))),
        "psnr_db": 99.0 if mse == 0 else float(10.0 * np.log10((255.0 * 255.0) / mse)),
        "pixels_changed_gt_8": int(np.sum(np.max(np.abs(delta), axis=2) > 8)),
        "pixel_count": int(delta.shape[0] * delta.shape[1]),
        "mean_luminance_a": float(np.mean(lum_a)),
        "mean_luminance_b": float(np.mean(lum_b)),
        "mean_luminance_delta_b_minus_a": float(np.mean(lum_b - lum_a)),
    }


def crop(image: np.ndarray, box: tuple[int, int, int, int]) -> np.ndarray:
    x0, y0, x1, y1 = box
    return image[y0:y1, x0:x1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--software", type=Path, required=True)
    parser.add_argument("--vulkan", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True, help="threepp source frame before Chronon composition")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frame", type=int, required=True, help="decoded frame number")
    args = parser.parse_args()

    software, vulkan, source = map(load_rgb, (args.software, args.vulkan, args.source))
    if software.shape != vulkan.shape or software.shape != source.shape:
        raise SystemExit("T13 inputs must all have the same dimensions")
    height, width, _ = software.shape
    if (width, height) != (1920, 1080):
        raise SystemExit(f"expected 1920x1080 inputs, got {width}x{height}")

    # The GLB occupies the upper image. Threshold only finds its visible bounds;
    # parity metrics cover the complete resulting bounding rectangle.
    model_mask = np.max(software[:830], axis=2) > 20
    ys, xs = np.where(model_mask)
    if not len(xs):
        raise SystemExit("could not find the rendered model in the top 830 rows")
    model_box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    rois = {
        "vehicle_bbox": model_box,
        "headline": (730, 870, 1280, 928),
        "subhead": (690, 936, 1300, 990),
        "marker": (565, 875, 600, 985),
        "overlay_union": (540, 840, 1360, 1010),
    }
    report = {
        "test_id": "T13",
        "frame": args.frame,
        "dimensions": [width, height],
        "vehicle_bbox_xyxy": list(model_box),
        "software_vs_vulkan": {
            name: metrics(crop(software, box), crop(vulkan, box))
            for name, box in rois.items()
        },
        "chronon_composition_vs_threepp_source": {
            name: metrics(crop(software, box), crop(source, box))
            for name, box in rois.items()
            if name in ("headline", "subhead", "marker")
        },
        "criteria": {
            "vehicle_roi_mae_rgb_max": 1.0,
            "vehicle_roi_mean_luminance_delta_max": 1.0,
            "overlay_roi_reported_separately": True,
            "chronon_text_change_pixels_gt_8_min_per_roi": 1000,
        },
    }
    vehicle = report["software_vs_vulkan"]["vehicle_bbox"]
    model_pass = vehicle["mae_rgb"] <= 1.0 and abs(vehicle["mean_luminance_delta_b_minus_a"]) <= 1.0
    text_checks = report["chronon_composition_vs_threepp_source"]
    text_pass = all(item["pixels_changed_gt_8"] >= 1000 for item in text_checks.values())
    report["result"] = "PASS" if model_pass and text_pass else "FAIL"
    report["model_roi_parity_pass"] = model_pass
    report["chronon_overlay_changed_source_pass"] = text_pass

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if report["result"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
