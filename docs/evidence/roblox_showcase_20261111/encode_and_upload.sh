#!/usr/bin/env bash
set -euo pipefail

HERE="/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/docs/evidence/roblox_showcase_20261111"
UPLOAD_BIN="/home/pierone/src/go-master/projects/Pyt/VeloxEditing/RenderingGen/bin/drive-upload"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)/ops/lib/common.sh"
FOLDER_ID="$VELOX_DRIVE_OVERLAY_ROOT"

RAW_RGBA="$HERE/out/chronon_gyroscope_1080p.rgba"
CLEAN_MP4="$HERE/out/10_chronon_procedural_gyroscope_clean_1080p.mp4"
TECH_MP4="$HERE/out/11_chronon_procedural_gyroscope_technical_1080p.mp4"
OVERLAY_PNG="$HERE/assets/gyro_overlay_1080p.png"

echo "== [1/4] Encoding clean 1080p @ 60fps video..."
ffmpeg -y \
  -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i "$RAW_RGBA" \
  -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p -r 60 -movflags +faststart \
  "$CLEAN_MP4"

echo "== [2/4] Encoding technical telemetry overlay 1080p @ 60fps video..."
ffmpeg -y \
  -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i "$RAW_RGBA" \
  -i "$OVERLAY_PNG" \
  -filter_complex "[0:v][1:v]overlay=0:0[outv]" \
  -map "[outv]" \
  -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p -r 60 -movflags +faststart \
  "$TECH_MP4"

echo "== [3/4] Uploading to Google Drive folder $FOLDER_ID..."
"$UPLOAD_BIN" -folder "$FOLDER_ID" -file "$CLEAN_MP4"
"$UPLOAD_BIN" -folder "$FOLDER_ID" -file "$TECH_MP4"

echo "== [4/4] Cleaning up raw video & large plan to preserve disk..."
rm -f "$RAW_RGBA"
rm -f "$HERE/plans/chronon_gyroscope_1080p.plan.json"

echo "== SUCCESS: All steps completed successfully!"
