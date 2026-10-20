#!/usr/bin/env python3
"""Verify a rendered clip library against its manifest, from the MP4s themselves.

Checks per clip:
  * frame count and duration reported by ffprobe match the manifest
  * the subject never touches the frame border (nothing got cropped)
  * vertical fill stays inside a sane band
  * the picture actually changes over the clip unless the source clip is static

  python3 tools/autorig/verify_clip_library.py ../models/output_mrbeast/library_gestures
"""

import argparse
import json
import os
import subprocess
import sys

WIDTH = 540
THRESHOLD = 24


def frames_of(path, width, height):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    size = width * height * 3
    return [raw[i:i + size] for i in range(0, len(raw) - size + 1, size)]


def bbox(frame, width, height):
    background = (frame[0], frame[1], frame[2])
    xs = []
    ys = []
    for y in range(0, height, 3):
        row = y * width
        for x in range(0, width, 3):
            offset = (row + x) * 3
            if (abs(frame[offset] - background[0]) + abs(frame[offset + 1] - background[1])
                    + abs(frame[offset + 2] - background[2])) > THRESHOLD:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (min(xs), max(xs), min(ys), max(ys))


def motion(frames):
    if len(frames) < 2:
        return 0.0
    reference = frames[0]
    best = 0.0
    for frame in frames[1:]:
        total = 0
        count = 0
        for offset in range(0, len(frame), 3 * 11):
            total += abs(frame[offset] - reference[offset])
            count += 1
        best = max(best, total / float(max(count, 1)))
    return best


def verify(directory, static_ok):
    manifest = json.load(open(os.path.join(directory, "library.json")))
    width = int(manifest.get("width", WIDTH))
    height = int(manifest.get("height", WIDTH))
    failures = 0
    for clip in manifest["clips"]:
        name = clip["name"]
        if "error" in clip:
            print("FAIL %-24s manifest error: %s" % (name, clip["error"]))
            failures += 1
            continue
        path = os.path.join(directory, clip["mp4"])
        if not os.path.isfile(path):
            print("FAIL %-24s missing %s" % (name, clip["mp4"]))
            failures += 1
            continue
        probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                                "-show_entries", "stream=nb_frames,r_frame_rate",
                                "-of", "json", path], capture_output=True, text=True)
        info = json.loads(probe.stdout or "{}")["streams"][0]
        count = int(info["nb_frames"])
        frames = frames_of(path, width, height)
        problems = []
        if len(frames) != clip["frames"]:
            problems.append("decoded %d frames, manifest says %d" % (len(frames), clip["frames"]))
        if count != clip["frames"]:
            problems.append("container reports %d frames" % count)
        worst_fill = 0.0
        cropped = 0
        tallest = 0.0
        for frame in frames:
            box = bbox(frame, width, height)
            if not box:
                continue
            if (box[0] <= 2 or box[1] >= width - 3
                    or box[2] <= 2 or box[3] >= height - 3):
                cropped += 1
            fill = (box[3] - box[2]) / float(height)
            tallest = max(tallest, fill)
            worst_fill = max(worst_fill, (box[1] - box[0]) / float(width), fill)
        if cropped:
            problems.append("%d frame(s) touch the border" % cropped)
        if not (0.45 <= tallest <= 0.99):
            problems.append("subject height %.0f%% outside the 45-99%% band" % (100 * tallest))
        measured = motion(frames)
        if measured < 0.05 and name not in static_ok:
            problems.append("static picture (motion %.3f)" % measured)
        if problems:
            failures += 1
            print("FAIL %-24s %s" % (name, "; ".join(problems)))
        else:
            print("ok   %-24s %3d frames  %4.2fs  height %3.0f%%  motion %5.2f" % (
                name, len(frames), len(frames) / float(manifest["fps"]),
                100 * tallest, measured))
    print("%d clip(s), %d failure(s) -> %s" % (len(manifest["clips"]), failures, directory))
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Verify rendered clip libraries.")
    parser.add_argument("directories", nargs="+")
    parser.add_argument("--static-ok", default="A_TPose,Pistol_Aim_Down,Pistol_Aim_Neutral,Pistol_Aim_Up",
                        help="clips that are single poses by design")
    args = parser.parse_args(argv)
    static_ok = {name.strip() for name in args.static_ok.split(",") if name.strip()}
    status = 0
    for directory in args.directories:
        status |= verify(directory, static_ok)
    return status


if __name__ == "__main__":
    sys.exit(main())
