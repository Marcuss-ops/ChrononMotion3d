#!/usr/bin/env python3
"""Deterministic scene layout for the `political_speech` template.

Reads a template JSON and emits concrete world-space positions for the speaker,
stage and crowd. No AI, no randomness: the same input always gives the same
layout, so renders stay reproducible.

Usage:
    python3 layout.py template.json            # print resolved layout as JSON
    python3 layout.py template.json --check    # validate only, exit 0/1
"""

from __future__ import annotations

import argparse
import json
import math
import sys

REQUIRED_KEYS = ("template", "speaker", "stage", "crowd_count")
SUPPORTED_TEMPLATES = ("political_speech",)


class LayoutError(ValueError):
    pass


def validate(spec: dict) -> None:
    for key in REQUIRED_KEYS:
        if key not in spec:
            raise LayoutError(f"missing key: {key}")
    if spec["template"] not in SUPPORTED_TEMPLATES:
        raise LayoutError(f"unsupported template: {spec['template']}")
    count = spec["crowd_count"]
    if not isinstance(count, int) or count < 0 or count > 500:
        raise LayoutError("crowd_count must be an integer in [0, 500]")
    stage = spec["stage"]
    if float(stage.get("width", 0)) <= 0 or float(stage.get("depth", 0)) <= 0:
        raise LayoutError("stage width and depth must be > 0")


def crowd_positions(count: int, width: float, depth: float, start_z: float) -> list[list[float]]:
    """Grid fill in front of the stage, rows of equal width, centred on x=0."""
    if count == 0:
        return []
    cols = max(1, math.ceil(math.sqrt(count * width / depth)))
    rows = math.ceil(count / cols)
    positions: list[list[float]] = []
    for i in range(count):
        row, col = divmod(i, cols)
        x = (col + 0.5) / cols * width - width / 2.0
        z = start_z + (row + 0.5) / rows * depth
        positions.append([round(x, 4), 0.0, round(z, 4)])
    return positions


def resolve(spec: dict) -> dict:
    validate(spec)
    stage = spec["stage"]
    width = float(stage["width"])
    depth = float(stage["depth"])
    height = float(stage.get("height", 0.5))

    # Stage sits at the origin; speaker stands centre-stage on top of it.
    speaker_pos = [0.0, height, 0.0]
    # Crowd starts just past the stage front edge (+z toward camera).
    crowd_start_z = depth / 2.0 + 0.5
    crowd_depth = float(spec.get("crowd_depth", 4.0))
    crowd_width = float(spec.get("crowd_width", 8.0))

    return {
        "template": spec["template"],
        "stage": {"position": [0.0, 0.0, 0.0], "width": width, "depth": depth, "height": height},
        "speaker": {"position": speaker_pos, "clip_sequence": spec["speaker"].get("clips", [])},
        "crowd": {
            "count": spec["crowd_count"],
            "positions": crowd_positions(spec["crowd_count"], crowd_width, crowd_depth, crowd_start_z),
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("template")
    parser.add_argument("--check", action="store_true", help="validate only")
    args = parser.parse_args(argv)
    try:
        with open(args.template, encoding="utf-8") as fh:
            spec = json.load(fh)
        layout = resolve(spec)
    except (OSError, json.JSONDecodeError, LayoutError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.check:
        print(f"ok: {args.template} ({layout['crowd']['count']} crowd)")
        return 0
    json.dump(layout, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
