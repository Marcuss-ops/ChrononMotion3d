#!/usr/bin/env python3
"""Compare every decoded T13 output frame across software/Vulkan and source."""
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


def start_decoder(path: Path) -> subprocess.Popen:
    return subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-vsync", "0", "-"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def read_frame(decoder: subprocess.Popen) -> np.ndarray | None:
    chunks = bytearray()
    while len(chunks) < FRAME_BYTES:
        piece = decoder.stdout.read(FRAME_BYTES - len(chunks))
        if not piece:
            break
        chunks.extend(piece)
    if not chunks:
        return None
    if len(chunks) != FRAME_BYTES:
        raise RuntimeError(f"short decoded frame: {len(chunks)} bytes")
    return np.frombuffer(chunks, dtype=np.uint8).reshape((H, W, 3))


def bbox_from_model(frame: np.ndarray) -> tuple[int, int, int, int]:
    mask = np.max(frame[:830], axis=2) > 20
    ys, xs = np.where(mask)
    if not len(xs):
        raise RuntimeError("could not find vehicle in top 830 rows")
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def crop(frame: np.ndarray, box: tuple[int, int, int, int]) -> np.ndarray:
    x0, y0, x1, y1 = box
    return frame[y0:y1, x0:x1]


def roi_stats(a: np.ndarray, b: np.ndarray) -> tuple[float, float, float, int]:
    delta = a.astype(np.int16) - b.astype(np.int16)
    mae = float(np.mean(np.abs(delta)))
    mse = float(np.mean(delta.astype(np.float64) ** 2))
    psnr = 99.0 if mse == 0 else float(10.0 * np.log10(255.0 * 255.0 / mse))
    lum = lambda image: 0.2126 * image[:, :, 0] + 0.7152 * image[:, :, 1] + 0.0722 * image[:, :, 2]
    lum_delta = float(np.mean(lum(b).astype(np.float64) - lum(a).astype(np.float64)))
    changed = int(np.sum(np.max(np.abs(delta), axis=2) > 8))
    return mae, psnr, lum_delta, changed


def summarize(values: list[float]) -> dict:
    return {"min": float(min(values)), "mean": float(np.mean(values)), "max": float(max(values))}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--software", type=Path, required=True)
    parser.add_argument("--vulkan", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    decoders = [start_decoder(path) for path in (args.software, args.vulkan, args.source)]
    model_mae, model_psnr, model_luma = [], [], []
    overlay_mae, overlay_psnr, overlay_luma = [], [], []
    source_changes: dict[str, list[int]] = {name: [] for name in OVERLAYS}
    sample_frames = {}
    frame_count = 0
    try:
        while True:
            frames = [read_frame(decoder) for decoder in decoders]
            if all(frame is None for frame in frames):
                break
            if any(frame is None for frame in frames):
                raise RuntimeError(f"input MP4 frame counts differ near frame {frame_count}")
            software, vulkan, source = frames
            if frame_count in (0, 75, 149):
                sample_frames[str(frame_count)] = {}

            box = bbox_from_model(software)
            mae, psnr, luma_delta, _ = roi_stats(crop(software, box), crop(vulkan, box))
            model_mae.append(mae)
            model_psnr.append(psnr)
            model_luma.append(luma_delta)

            overlay_frame_mae, overlay_frame_psnr, overlay_frame_luma = [], [], []
            for name, roi in OVERLAYS.items():
                sroi, vroi, srcroi = crop(software, roi), crop(vulkan, roi), crop(source, roi)
                om, op, ol, _ = roi_stats(sroi, vroi)
                _, _, _, source_changed = roi_stats(sroi, srcroi)
                overlay_mae.append(om)
                overlay_psnr.append(op)
                overlay_luma.append(ol)
                source_changes[name].append(source_changed)
                overlay_frame_mae.append(om)
                overlay_frame_psnr.append(op)
                overlay_frame_luma.append(ol)

            if frame_count in (0, 75, 149):
                sample_frames[str(frame_count)] = {
                    "vehicle_bbox_xyxy": list(box),
                    "vehicle_software_vulkan": {"mae_rgb": mae, "psnr_db": psnr, "mean_luminance_delta": luma_delta},
                    "overlay_software_vulkan": {
                        "mean_mae_rgb": float(np.mean(overlay_frame_mae)),
                        "mean_psnr_db": float(np.mean(overlay_frame_psnr)),
                        "mean_luminance_delta": float(np.mean(overlay_frame_luma)),
                    },
                    "overlay_source_changed_pixels_gt_8": {
                        name: source_changes[name][-1] for name in OVERLAYS
                    },
                }
            frame_count += 1
    finally:
        for decoder in decoders:
            if decoder.stdout:
                decoder.stdout.close()
        return_codes = [decoder.wait() for decoder in decoders]
    if return_codes != [0, 0, 0]:
        raise RuntimeError(f"ffmpeg decoder return codes: {return_codes}")
    if frame_count != 150:
        raise RuntimeError(f"expected 150 frames per video, got {frame_count}")

    model_pass = max(model_mae) <= 1.0 and max(abs(value) for value in model_luma) <= 1.0
    overlay_pass = all(min(values) >= 1000 for values in source_changes.values())
    report = {
        "test_id": "T13",
        "result": "PASS" if model_pass and overlay_pass else "FAIL",
        "frames_compared": frame_count,
        "dimensions": [W, H],
        "software_video": str(args.software),
        "vulkan_video": str(args.vulkan),
        "threepp_source_video": str(args.source),
        "vehicle_roi_software_vs_vulkan": {
            "mae_rgb_per_frame": summarize(model_mae),
            "psnr_db_per_frame": summarize(model_psnr),
            "mean_luminance_delta_per_frame": summarize(model_luma),
            "max_mae_rgb_gate": 1.0,
            "max_absolute_luminance_delta_gate": 1.0,
            "pass": model_pass,
        },
        "overlay_roi_software_vs_vulkan": {
            "mean_mae_rgb_all_overlay_rois_per_frame": summarize(overlay_mae),
            "mean_psnr_db_all_overlay_rois_per_frame": summarize(overlay_psnr),
            "mean_luminance_delta_all_overlay_rois_per_frame": summarize(overlay_luma),
            "note": "Rasterization differs slightly between software and Vulkan; tracked separately from the model ROI.",
        },
        "overlay_changed_vs_threepp_source_pixels_gt_8": {
            name: {"min": min(values), "mean": float(np.mean(values)), "max": max(values), "gate_min": 1000}
            for name, values in source_changes.items()
        },
        "sample_frames": sample_frames,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if report["result"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
