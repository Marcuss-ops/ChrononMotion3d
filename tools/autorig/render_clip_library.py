#!/usr/bin/env python3
"""Render every animation clip of a skinned GLB to its own MP4, headless.

One renderer process per clip: inside a process the camera is computed once, so
a clip never gets re-framed mid-sequence. Frames are written as PPM, encoded
with ffmpeg, then the frames are deleted unless --keep-frames is given.

Outputs:
  <out>/<clip>.mp4        one video per clip
  <out>/library.json      manifest: clip, duration, frames, fps, size, path

Exit code is non-zero when any clip fails to render or encode.
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_RENDERER = os.path.join(REPO_ROOT, "build", "dev-render", "render",
                                "chrononmotion_render_glb")

CLIP_LINE = re.compile(r"^(\d+): (.+) \(([0-9.]+)s\)$")


def list_clips(renderer, glb):
    result = subprocess.run([renderer, "--list-anims", glb],
                            capture_output=True, text=True)
    clips = []
    for line in result.stdout.splitlines():
        match = CLIP_LINE.match(line.strip())
        if match:
            clips.append({"index": int(match.group(1)), "name": match.group(2),
                          "duration": float(match.group(3))})
    if not clips:
        sys.stderr.write("error: no clips reported by the renderer for %s\n" % glb)
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(2)
    return clips


def parse_camera(text):
    """Read the renderer's `glb camera:` probe line."""
    for line in text.splitlines():
        if line.startswith("glb camera:"):
            values = {}
            for part in line.split(":", 1)[1].split():
                key, _, value = part.partition("=")
                numbers = [float(v) for v in value.split(",")] if "," in value else float(value)
                values[key] = numbers
            return values
    return None


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(v):
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / length for c in v)


def solve_camera(probe, frames, args):
    """Pick a camera that centers the observed silhouette at the target fill.

    threepp's bounds for a SkinnedMesh can be far from the rendered geometry
    (the low-poly man measures 0.21 tall while drawing 1.7 tall), so the frame
    is solved from pixels instead of from the loader's numbers.
    """
    boxes = []
    for frame in frames:
        width, height, pixels = read_ppm(frame)
        box = silhouette(pixels, width, height)
        if box:
            boxes.append(box)
    if not boxes:
        return None
    x0 = min(box[0] for box in boxes)
    x1 = max(box[1] for box in boxes)
    y0 = min(box[2] for box in boxes)
    y1 = max(box[3] for box in boxes)
    width, height = read_ppm(frames[0])[0], read_ppm(frames[0])[1]

    position = probe["pos"]
    target = probe["target"]
    fov = float(probe["fov"]) if not isinstance(probe["fov"], list) else probe["fov"][0]
    distance = float(probe["distance"]) if not isinstance(probe["distance"], list) else probe["distance"][0]
    aspect = width / float(height)
    forward = normalize(tuple(target[i] - position[i] for i in range(3)))
    right = normalize(cross(forward, (0.0, 1.0, 0.0))) if abs(forward[1]) < 0.99 else (1.0, 0.0, 0.0)
    up = cross(right, forward)
    visible_h = 2.0 * distance * math.tan(math.radians(fov) * 0.5)
    visible_w = visible_h * aspect

    centre_x = 0.5 * (x0 + x1)
    centre_y = 0.5 * (y0 + y1)
    ndc_x = (centre_x / width) * 2.0 - 1.0
    ndc_y = 1.0 - (centre_y / height) * 2.0
    world_centre = tuple(target[i] + right[i] * ndc_x * visible_w * 0.5
                         + up[i] * ndc_y * visible_h * 0.5 for i in range(3))

    subject_h = ((y1 - y0) / float(height)) * visible_h
    subject_w = ((x1 - x0) / float(width)) * visible_w
    required = max(subject_h, subject_w / aspect)
    new_distance = required / (2.0 * math.tan(math.radians(fov) * 0.5) * args.fit)
    new_position = tuple(world_centre[i] - forward[i] * new_distance for i in range(3))
    return {"pos": new_position, "target": world_centre, "fov": fov,
            "fill": (y1 - y0) / float(height)}


def silhouette(pixels, width, height):
    background = (pixels[0], pixels[1], pixels[2])
    xs = []
    ys = []
    for y in range(0, height, 2):
        for x in range(0, width, 2):
            offset = (y * width + x) * 3
            if (abs(pixels[offset] - background[0]) + abs(pixels[offset + 1] - background[1])
                    + abs(pixels[offset + 2] - background[2])) > 24:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (min(xs), max(xs), min(ys), max(ys))


def border_touched(pixels, width, height, band=3):
    """Full-resolution test of the outer band, where a crop would show first.

    The silhouette search subsamples, which can miss a thin fingertip; the
    border band is only a few thousand pixels, so it is scanned every pixel.
    """
    background = (pixels[0], pixels[1], pixels[2])
    rows = list(range(band)) + list(range(max(band, height - band), height))
    for y in rows:
        base = y * width * 3
        for x in range(width):
            offset = base + x * 3
            if (abs(pixels[offset] - background[0]) + abs(pixels[offset + 1] - background[1])
                    + abs(pixels[offset + 2] - background[2])) > 24:
                return True
    for y in range(band, max(band, height - band)):
        for x in list(range(band)) + list(range(max(band, width - band), width)):
            offset = (y * width + x) * 3
            if (abs(pixels[offset] - background[0]) + abs(pixels[offset + 1] - background[1])
                    + abs(pixels[offset + 2] - background[2])) > 24:
                return True
    return False


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    parts = data.split(b"\n", 3)
    width, height = map(int, parts[1].split())
    return width, height, parts[3]


def subject_coverage(frame):
    width, height, pixels = frame
    background = (pixels[0], pixels[1], pixels[2])
    covered = total = 0
    for index in range(0, width * height, 7):
        offset = index * 3
        total += 1
        if (abs(pixels[offset] - background[0]) + abs(pixels[offset + 1] - background[1])
                + abs(pixels[offset + 2] - background[2])) > 24:
            covered += 1
    return covered / float(max(total, 1))


def mean_difference(first, second):
    if first[0] != second[0] or first[1] != second[1]:
        return 0.0
    total = count = 0
    for index in range(0, len(first[2]), 3 * 11):
        total += abs(first[2][index] - second[2][index])
        count += 1
    return total / float(max(count, 1))


def motion_metric(frames):
    """Largest change between the first frame and any later sampled frame.

    Comparing only against the middle frame hides clips that pass through rest
    there (a nod or a head shake returns to neutral mid-way), so sample several
    frames and keep the maximum.
    """
    if len(frames) < 2:
        return 0.0
    reference = frames[0]
    return max(mean_difference(reference, frame) for frame in frames[1:])


def frame_extents(work, written):
    """Union box, worst fill ratio and border contact over every rendered frame."""
    fill = 0.0
    touches = False
    margin = 1.0
    box = None
    width = height = 0
    for name in written:
        width, height, pixels = read_ppm(os.path.join(work, name))
        current = silhouette(pixels, width, height)
        if not current:
            continue
        box = current if box is None else (min(box[0], current[0]), max(box[1], current[1]),
                                           min(box[2], current[2]), max(box[3], current[3]))
        fill = max(fill, (current[1] - current[0]) / float(width),
                   (current[3] - current[2]) / float(height))
        # Keep a real safety band, not just "not touching": H.264 chroma
        # subsampling can bleed a couple of pixels outwards, and a one-frame
        # crop is still a crop.
        edge = min(current[0], width - 1 - current[1], current[2], height - 1 - current[3])
        margin = min(margin, edge / float(width))
        if edge <= 2 or border_touched(pixels, width, height):
            touches = True
    return {"fill": fill, "touches": touches, "margin": margin,
            "box": box, "width": width, "height": height}


def silhouette_offset(extents):
    """How far the union box centre sits from the frame centre, in NDC units."""
    box = extents["box"]
    if not box or not extents["width"]:
        return 1.0
    ndc_x = ((box[0] + box[1]) * 0.5 / extents["width"]) * 2.0 - 1.0
    ndc_y = 1.0 - ((box[2] + box[3]) * 0.5 / extents["height"]) * 2.0
    return max(abs(ndc_x), abs(ndc_y))


def refine_camera(camera, extents, aspect, target_fill):
    """Nudge a solved camera so the union box is centred and fills the frame.

    A single measure-then-place step is not enough: moving the camera closer
    magnifies parallax, so the silhouette centre drifts even though the look-at
    point is unchanged. Recompute centre and scale from the frame the camera
    actually produced.
    """
    box = extents["box"]
    if not box:
        return camera, 0.0
    width, height = extents["width"], extents["height"]
    target = camera["target"]
    position = camera["pos"]
    fov = camera["fov"]
    offset = [position[i] - target[i] for i in range(3)]
    distance = math.sqrt(sum(value * value for value in offset))
    forward = [-value / distance for value in offset]
    world_up = [0.0, 1.0, 0.0]
    right = [forward[1] * world_up[2] - forward[2] * world_up[1],
             forward[2] * world_up[0] - forward[0] * world_up[2],
             forward[0] * world_up[1] - forward[1] * world_up[0]]
    length = math.sqrt(sum(value * value for value in right)) or 1.0
    right = [value / length for value in right]
    up = [right[1] * forward[2] - right[2] * forward[1],
          right[2] * forward[0] - right[0] * forward[2],
          right[0] * forward[1] - right[1] * forward[0]]

    ndc_x = ((box[0] + box[1]) * 0.5 / width) * 2.0 - 1.0
    ndc_y = 1.0 - ((box[2] + box[3]) * 0.5 / height) * 2.0
    visible_h = 2.0 * distance * math.tan(math.radians(fov) * 0.5)
    visible_w = visible_h * aspect
    new_target = [target[i] + right[i] * ndc_x * visible_w * 0.5
                  + up[i] * ndc_y * visible_h * 0.5 for i in range(3)]

    widest = max((box[1] - box[0]) / float(width), (box[3] - box[2]) / float(height))
    # When the subject leaves the frame, `widest` saturates at 1.0 and cannot say
    # by how much, so a clipped frame always gets a solid push out instead of a
    # computed one. Everything is clamped: an unclamped correction overshoots and
    # the next iteration pulls it straight back in.
    margin_target = 0.03
    if extents["touches"] or extents.get("margin", 1.0) < margin_target:
        scale = max(1.12, min(1.45, widest / max(target_fill, 1e-3)))
    elif widest > 0.93:
        scale = min(1.25, widest / max(target_fill, 1e-3))
    elif widest < 0.82:
        scale = max(0.92, widest / 0.88)
    else:
        scale = 1.0
    new_distance = distance * scale
    return {"pos": [new_target[i] - forward[i] * new_distance for i in range(3)],
            "target": new_target, "fov": fov}, scale


def render_clip(renderer, glb, clip, out_dir, args):
    frames = max(2, int(math.ceil(min(clip["duration"], args.max_seconds) * args.fps)))
    if args.frames:
        frames = args.frames
    work = tempfile.mkdtemp(prefix="clip_", dir=args.tmp_dir)
    probe = tempfile.mkdtemp(prefix="probe_", dir=args.tmp_dir)
    try:
        camera = {"pos": None, "target": None, "fov": None}
        framing = "fit"
        if args.auto_frame:
            # Spread the probe across the whole clip: nine frames at a low fps
            # sample the full motion, so the solved camera accounts for the
            # widest pose and not just the first tenth of a second.
            probe_fps = 9.0 / max(clip["duration"], 1e-3)
            probe_command = [renderer] + clip["anim_flags"] + [
                             "--anim-fps=%g" % probe_fps, "--no-proof", "--print-bounds",
                             "--background=%s" % args.background]
            probe_command += [glb, probe, "9", str(args.width), str(args.height)]
            probed = subprocess.run(probe_command, capture_output=True, text=True)
            measured = parse_camera((probed.stdout or "") + (probed.stderr or ""))
            probe_frames = [os.path.join(probe, "frame_%06d.ppm" % i) for i in range(9)]
            if measured and all(os.path.isfile(path) for path in probe_frames):
                solved = solve_camera(measured, probe_frames, args)
                if solved:
                    camera = solved
                    framing = "solved"

        def run(fit):
            command = [renderer] + clip["anim_flags"] + [
                       "--anim-fps=%g" % args.fps,
                       "--exposure=%g" % args.exposure,
                       "--background=%s" % args.background,
                       "--no-proof"]
            if camera["pos"]:
                command.append("--camera-pos=%.5f,%.5f,%.5f" % tuple(camera["pos"]))
                command.append("--camera-target=%.5f,%.5f,%.5f" % tuple(camera["target"]))
                command.append("--camera-fov=%g" % camera["fov"])
            elif fit:
                command.append("--fit=%g" % fit)
            if args.loop:
                command.append("--anim-loop")
            command += [glb, work, str(frames), str(args.width), str(args.height)]
            completed = subprocess.run(command, capture_output=True, text=True)
            written = sorted(os.listdir(work)) if os.path.isdir(work) else []
            if completed.returncode != 0 or len(written) != frames:
                return None, "render failed (exit %d, %d/%d frames): %s" % (
                    completed.returncode, len(written), frames,
                    (completed.stderr or completed.stdout).strip().splitlines()[-1:] or "")
            return written, None

        written, error = run(args.fit)
        if error:
            return None, error

        # Clips with root motion, or arms thrown out to the side, can leave the
        # box the probe measured, and perspective moves the silhouette centre as
        # the camera closes in. Re-solve against the frames the camera actually
        # produced, up to a few times. Every frame is inspected, not a handful: a
        # gesture touches the border for a few frames only, and those frames are
        # exactly the ones a 4-frame sample misses.
        extents = frame_extents(work, written)
        refined = 0
        while camera["pos"] and refined < 5:
            if (not extents["touches"] and 0.78 <= extents["fill"] <= 0.96
                    and abs(silhouette_offset(extents)) <= 0.02
                    and extents.get("margin", 1.0) >= 0.02):
                break
            camera, scale = refine_camera(camera, extents, 1.0, args.fit * 0.98)
            framing = "solved+refined"
            refined += 1
            shutil.rmtree(work, ignore_errors=True)
            os.makedirs(work)
            written, error = run(args.fit)
            if error:
                return None, error
            extents = frame_extents(work, written)

        fill = extents["fill"]
        sample = sorted({0, len(written) // 4, len(written) // 2, (3 * len(written)) // 4})
        samples = [read_ppm(os.path.join(work, written[index])) for index in sample]
        first = samples[0]
        coverage = subject_coverage(first)
        if coverage < 0.02:
            # Nothing usable came out of the solved camera: retry with the
            # renderer's own proof framing rather than shipping a blank clip.
            shutil.rmtree(work, ignore_errors=True)
            os.makedirs(work)
            camera = {"pos": None, "target": None, "fov": None}
            written, error = run(0.0)
            if error:
                return None, error
            sample = sorted({0, len(written) // 4, len(written) // 2, (3 * len(written)) // 4})
            samples = [read_ppm(os.path.join(work, written[index])) for index in sample]
            first = samples[0]
            coverage = subject_coverage(first)
            framing = "default"
        if coverage < 0.005:
            return None, "empty frame: subject covers %.2f%% of the image" % (100 * coverage)
        motion = motion_metric(samples)
        target = os.path.join(out_dir, "%s.mp4" % clip["name"])
        encode = subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "%g" % args.fps,
             "-i", os.path.join(work, "frame_%06d.ppm"),
             "-c:v", "libx264", "-preset", "medium", "-crf", "18",
             "-pix_fmt", "yuv420p", "-movflags", "+faststart", target],
            capture_output=True, text=True)
        if encode.returncode != 0:
            return None, "encode failed: %s" % encode.stderr.strip()
        if args.keep_frames:
            shutil.copytree(work, os.path.join(out_dir, "%s_frames" % clip["name"]),
                            dirs_exist_ok=True)
        return {"frames": frames, "path": target, "coverage": round(coverage, 4),
                "fill": round(fill, 3), "motion": round(motion, 3), "framing": framing,
                "bytes": os.path.getsize(target)}, None
    finally:
        shutil.rmtree(probe, ignore_errors=True)
        if not args.keep_frames:
            shutil.rmtree(work, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Render a skinned GLB clip library to MP4, headless.")
    parser.add_argument("glb", help="skinned GLB with animation clips")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--clips", default="", help="comma separated clip names (default: all)")
    parser.add_argument("--sequence", default="",
                        help="render one chained clip instead of the library: \"Idle,G_Point,Idle\"")
    parser.add_argument("--blend", type=float, default=0.2,
                        help="crossfade seconds between chained clips")
    parser.add_argument("--width", type=int, default=540)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--max-seconds", type=float, default=3.0,
                        help="cap per clip; loops are sampled inside the clip length")
    parser.add_argument("--frames", type=int, default=0,
                        help="force an exact frame count instead of duration*fps")
    parser.add_argument("--fit", type=float, default=0.92)
    parser.add_argument("--exposure", type=float, default=0.45)
    parser.add_argument("--background", default="0x9d9d9d",
                        help="studio clear colour (hex) so the silhouette stays measurable")
    parser.add_argument("--loop", action="store_true", default=True,
                        help="wrap sampling inside the clip (default on)")
    parser.add_argument("--no-loop", dest="loop", action="store_false")
    parser.add_argument("--keep-frames", action="store_true",
                        help="keep the rendered PPM frames")
    parser.add_argument("--no-auto-frame", dest="auto_frame", action="store_false",
                        help="skip the pixel-based camera solve")
    parser.add_argument("--renderer", default=DEFAULT_RENDERER)
    parser.add_argument("--tmp-dir", default=None)
    args = parser.parse_args(argv)

    if not os.path.isfile(args.renderer):
        raise SystemExit("error: renderer not found: %s" % args.renderer)
    if not os.path.isfile(args.glb):
        raise SystemExit("error: GLB not found: %s" % args.glb)
    os.makedirs(args.out, exist_ok=True)

    clips = list_clips(args.renderer, args.glb)
    for clip in clips:
        clip["anim_flags"] = ["--anim=%s" % clip["name"]]
    if args.sequence.strip():
        names = [name.strip() for name in args.sequence.split(",") if name.strip()]
        known = {clip["name"]: clip["duration"] for clip in clips}
        missing = [name for name in names if name not in known]
        if missing:
            raise SystemExit("error: unknown clip(s): %s" % ", ".join(missing))
        blend = max(0.0, args.blend)
        if len(names) > 1:
            blend = min(blend, 0.5 * min(known[name] for name in names))
        duration = sum(known[name] for name in names) - blend * max(len(names) - 1, 0)
        flags = ["--anim-sequence=%s" % ",".join(names), "--anim-blend=%g" % blend]
        clips = [{"name": "SEQ_" + "+".join(names), "duration": duration,
                  "anim_flags": flags, "sequence": names, "blend": blend}]
    else:
        wanted = [name.strip() for name in args.clips.split(",") if name.strip()]
        if wanted:
            missing = [name for name in wanted if name not in {c["name"] for c in clips}]
            if missing:
                raise SystemExit("error: unknown clip(s): %s" % ", ".join(missing))
            clips = [clip for clip in clips if clip["name"] in wanted]

    manifest = {"source": os.path.abspath(args.glb), "renderer": args.renderer,
                "width": args.width, "height": args.height, "fps": args.fps,
                "fit": args.fit, "exposure": args.exposure, "clips": []}
    if args.sequence.strip():
        manifest["sequence"] = clips[0]["sequence"]
        manifest["blend"] = clips[0]["blend"]
    failures = 0
    for clip in clips:
        info, error = render_clip(args.renderer, args.glb, clip, args.out, args)
        entry = {"name": clip["name"], "duration": clip["duration"]}
        if error:
            failures += 1
            entry["error"] = error
            print("FAIL %-24s %s" % (clip["name"], error))
        else:
            entry.update(info)
            entry.pop("path", None)
            entry["mp4"] = os.path.relpath(info["path"], args.out)
            print("ok   %-24s %3d frames  %6.1f KB  fill %3.0f%%  motion %5.2f  %s" % (
                clip["name"], info["frames"], info["bytes"] / 1024.0,
                100 * info.get("fill", 0.0), info["motion"], info["framing"]))
        manifest["clips"].append(entry)

    with open(os.path.join(args.out, "library.json"), "w") as handle:
        json.dump(manifest, handle, indent=2)
    print("%d clip(s), %d failure(s) -> %s" % (len(clips), failures, args.out))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
