#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <adapter-enabled-chrononmotion_authoring> <chronon3d_cli> [output-dir]" >&2
    exit 2
fi

motion_authoring=$(realpath "$1")
chronon3d_cli=$(realpath "$2")
if [[ $# -eq 3 ]]; then
    output_dir=$(realpath -m "$3")
    mkdir -p "$output_dir"
    cleanup_output=false
else
    output_dir=$(mktemp -d "${TMPDIR:-/tmp}/chronon-renderplan-canary.XXXXXX")
    cleanup_output=true
fi

cleanup() {
    if [[ "$cleanup_output" == true ]]; then
        rm -rf -- "$output_dir"
    fi
}
trap cleanup EXIT

plan="$output_dir/motion-consumer-canary.plan.json"
video_plan="$output_dir/motion-consumer-canary-video.plan.json"
video="$output_dir/motion-consumer-canary.mp4"
validation="$output_dir/validation.json"
frame_hashes="$output_dir/frame-hashes.txt"

CHRONONMOTION_RENDER_PLAN_CANARY="$plan" "$motion_authoring"
"$chronon3d_cli" validate --plan "$plan" > "$validation"
python3 - "$validation" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    output = stream.read()
start = output.find("{")
if start < 0:
    raise SystemExit(f"Chronon3D validate produced no JSON result: {output}")
result = json.loads(output[start:])
if result.get("status") != "PASS" or result.get("valid") is not True:
    raise SystemExit(f"Chronon3D rejected Motion's RenderPlan: {result}")
PY

python3 - "$plan" "$video_plan" "$video" <<'PY'
import json
import sys

source, destination, output = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    plan = json.load(stream)
plan["output"] = {"path": output, "format": "mp4"}
with open(destination, "w", encoding="utf-8") as stream:
    json.dump(plan, stream, indent=2)
    stream.write("\n")
PY

"$chronon3d_cli" render --plan "$video_plan" --backend software \
    --profile preview --hardware none
ffprobe -v error -select_streams v:0 \
    -show_entries stream=width,height,nb_frames,r_frame_rate \
    -of json "$video" > "$output_dir/probe.json"
ffmpeg -v error -i "$video" -f framemd5 "$frame_hashes"
python3 - "$output_dir/probe.json" "$frame_hashes" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    probe = json.load(stream)
video = probe["streams"][0]
if (int(video["width"]), int(video["height"]), int(video["nb_frames"])) != (640, 360, 13):
    raise SystemExit(f"unexpected consumer-canary video stream: {video}")
with open(sys.argv[2], encoding="utf-8") as stream:
    hashes = [line.rsplit(",", 1)[-1].strip() for line in stream
              if line.strip() and not line.lstrip().startswith("#")]
if len(hashes) != 13 or len(set(hashes)) < 2:
    raise SystemExit(f"expected 13 decoded, changing frames; got {len(hashes)}")
print("RenderPlan consumer canary PASS: schema accepted; 13 changing frames at 640x360/30fps")
PY
