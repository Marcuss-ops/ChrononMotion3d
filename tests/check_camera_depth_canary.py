#!/usr/bin/env python3
"""Run the Motion→RenderPlan→C3D 2.5D Z-order acceptance canary."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def write_solid_png(path: Path, rgba: tuple[int, int, int, int]) -> None:
    width = height = 128
    raw = b"".join(b"\0" + bytes(rgba) * width for _ in range(height))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    data = (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--motion-authoring", required=True, type=Path)
    parser.add_argument("--chronon3d-cli", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="chronon-camera-z-") as temp:
        root = Path(temp)
        assets = root / "assets"
        assets.mkdir()
        write_solid_png(assets / "blue.png", (20, 70, 240, 255))
        write_solid_png(assets / "red.png", (240, 40, 20, 255))
        plan = root / "camera-f3-depth.plan.json"
        env = os.environ.copy()
        env["CHRONONMOTION_CAMERA_DEPTH_CANARY"] = str(plan)
        subprocess.run([str(args.motion_authoring)], env=env, check=True,
                       stdout=subprocess.DEVNULL)

        decoded = json.loads(plan.read_text())
        red = next(layer for layer in decoded["layers"] if layer["asset"] == "red.png")
        z = [sample["position"][2] for sample in red["transform_samples"]]
        if z != [20.0, 150.0]:
            raise SystemExit(f"Motion Z lowering mismatch: expected [20, 150], got {z}")

        frame_hashes: dict[int, str] = {}
        for frame, expected in ((0, bytes((240, 40, 20, 255))),
                                (1, bytes((20, 70, 240, 255)))):
            output = root / f"frame-{frame}.rgba"
            log = root / f"frame-{frame}.log"
            command = [
                str(args.chronon3d_cli), "render", "--plan", str(plan),
                "--assets-root", str(assets), "--backend", "software",
                "--video-sink", "raw", "--pipe-pixfmt", "rgba",
                "--encoder-backend", "pipe", "--start-frame", str(frame),
                "--end-frame", str(frame), "-o", str(output),
            ]
            with log.open("wb") as stderr:
                subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=stderr)
            pixels = output.read_bytes()
            expected_size = 320 * 180 * 4
            if len(pixels) != expected_size:
                raise SystemExit(f"frame {frame}: expected {expected_size} raw bytes, got {len(pixels)}")
            center = pixels[(90 * 320 + 160) * 4:(90 * 320 + 160) * 4 + 4]
            if center != expected:
                raise SystemExit(
                    f"frame {frame}: expected center RGBA {list(expected)}, got {list(center)}")
            frame_hashes[frame] = hashlib.sha256(pixels).hexdigest()

        print("PASS F3 camera-backed Z-order: red is foreground at frame 0; blue is foreground at frame 1")
        print(f"frame 0 raw SHA-256: {frame_hashes[0]}")
        print(f"frame 1 raw SHA-256: {frame_hashes[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
