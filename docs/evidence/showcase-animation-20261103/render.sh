#!/usr/bin/env bash
# Render the showcase plans to MP4 through the raw video sink.
#
# Why not `-o file.mp4`? This Chronon3D build links an avcodec without encoders
# ("avcodec_find_encoder_by_name('libx264') failed"), so the CLI cannot mux
# H.264 itself here. `--video-sink raw` is the documented pixel-truth surface
# anyway (TICKET-TEXT-QUALITY-V2 P0-0b): the engine writes RGBA frames with no
# encoder or container, and a boring system ffmpeg does the encode. The frames
# the engine produced are untouched in between.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
CLI="${CLI:-$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli}"
# Authored canvas carries a 96 x 48 overscan that the camera move needs; the
# delivered frames are the 960x540 window inside it.
W="${W:-1056}"
H="${H:-588}"
CROP="${CROP:-960:540:48:24}"
DEST_W="${DEST_W:-960}"
DEST_H="${DEST_H:-540}"
# 60 fps over 4 s: the software frame costs ~0.16 s, so the extra sampling is
# affordable and the camera/track motion reads smooth instead of stuttery.
FPS="${FPS:-60}"
FRAMES="${FRAMES:-240}"
OUT="$HERE/out"
RAW="$OUT/raw"
mkdir -p "$RAW"

render_one() {
  local plan="$1" name="$2" frames="$3"
  local last=$(( frames - 1 ))
  echo "== $name ($frames frames, ${W}x${H})"
  "$CLI" render --plan "$HERE/plans/$plan" --assets-root "$HERE" --backend software \
    --start-frame 0 --end-frame "$last" --video-sink raw \
    --output "$RAW/$name.rgba" -v warn
  local bytes
  bytes=$(stat -c %s "$RAW/$name.rgba")
  if [ "$bytes" -ne $((W * H * 4 * frames)) ]; then
    echo "  ! raw size $bytes != $((W * H * 4 * frames))" >&2
    exit 1
  fi
  # Every rendered frame is delivered: the engine's first frame is no longer
  # a partially-rendered warmup frame, so there is nothing to trim.
  ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s "${W}x${H}" -r "$FPS" \
    -i "$RAW/$name.rgba" -vf "crop=${CROP}" \
    -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p \
    -movflags +faststart "$OUT/${name}.mp4"
  ffprobe -v error -select_streams v:0 -count_frames \
    -show_entries stream=nb_read_frames,width,height,r_frame_rate,duration \
    -of default=noprint_wrappers=1 "$OUT/${name}.mp4"
}

render_one "01-oggetto-sotto-luce.plan.json" "01-oggetto-sotto-luce" "$FRAMES"
render_one "02-materiali.plan.json" "02-materiali" "$FRAMES"
render_one "03-gltf-animato.plan.json" "03-gltf-animato" "$FRAMES"
echo "done -> $OUT"
