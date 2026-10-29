# Visible Motion3D → Chronon3D software probes — 2026-10-29

## Provenance

- ChrononMotion3D `HEAD`: `ea8982dc6d870852f42030154ff2a816d7686af7` (dirty).
- Chronon3D source `HEAD`: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty).
- Host: Linux x86_64; NVIDIA RTX A4000 visible.
- Motion build: `ChrononMotion3D/build/release-fast`.
- The available Release CLI under `Chronon3d/build/chronon/linux-video-release`
  reports build commit `67fd33f7f`, so its output is treated as diagnostic only;
  it is stale relative to the source checkout. A current-source CLI build was
  already running under `Chronon3d/.tmp/chronon-builds/linux-fast-dev` when
  this evidence was captured. No same-SHA CI or strict Vulkan result is claimed.

## Producer fixture and tests

The optional producer harness emits the synthetic animated glTF and constant
red HDRI cases. The glTF is only 2 model units wide, so the CLI acceptance
fixture now places it at canvas center and scales it by 70. The HDRI probe uses
the same canvas position with scale 100. These are test-fixture transforms,
not changes to the producer's baked geometry.

```sh
cmake --build ChrononMotion3D/build/release-fast \
  --target chrononmotion_authoring -j1
CHRONONMOTION_TICKET_GLTF_PLAN="$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
CHRONONMOTION_TICKET_HDRI_PLAN="$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri/hdri.plan.json" \
  ChrononMotion3D/build/release-fast/chrononmotion_authoring
ctest --test-dir ChrononMotion3D/build/release-fast --output-on-failure \
  -R '^chrononmotion_authoring$'
```

Build exit 0. The authoring executable reports 349 checks, 0 failures; CTest
passes 1/1. Logs and emitted plans/assets are beside this file.

## Software raw pixel truth

Using `/home/pierone/src/go-master/projects/Pyt/VeloxEditing/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli` (the stale Release CLI noted above):

```sh
/home/pierone/src/go-master/projects/Pyt/VeloxEditing/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  render --plan "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
  --assets-root "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf" \
  --backend software --start-frame 0 --end-frame 30 --video-sink raw \
  --output /tmp/chrononmotion-gltf-scaled-final.rgba
/home/pierone/src/go-master/projects/Pyt/VeloxEditing/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  render --plan "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri/hdri.plan.json" \
  --assets-root "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri" \
  --backend software --start-frame 0 --end-frame 3 --video-sink raw \
  --output /tmp/chrononmotion-hdri-scaled-final.rgba
```

The RGBA results have lossless sample PNGs and hashes in `gltf/raw-samples/`,
`hdri/raw-samples/`, and [`pixel-report.json`](pixel-report.json).

- glTF: 31 frames, all 31 frame SHA-256 hashes distinct, 1,642 opaque pixels
  per sample. Alpha bounds move from `[320,118,376,171]` at frame 0 through
  `[390,83,446,136]` at frame 15 to `[460,48,516,101]` at frame 30. The raw
  pixels include the synthetic orange/cream texture and transparent texels.
- HDRI: 4 identical frames with a visible 20,000-pixel mesh. An otherwise
  identical control plan with `environment_light` removed changes all 20,000
  opaque pixels. At `(320,180)`, RGBA changes from `(6,71,32,255)` without the
  probe to `(25,71,32,255)` with its red SH contribution.

This proves the software raw-sink geometry, texture/alpha, baked motion, and
diffuse SH contribution on the current producer fixtures, subject to a rerun
with a CLI built from the current Chronon3D checkout.

## Encoded-video caveat

The stale Release CLI wrote the glTF/NVENC MP4 with 31 decodable frames and 31
distinct decoded hashes, but its decoded image is dominated by black and green
pixels instead of matching the raw RGBA content. A system-FFmpeg `libx264`
reference encoded from the raw frames decodes to the expected black backdrop
and moving textured triangle, but this external reference is not the canonical
Chronon3D export route. The application MP4 route therefore remains
unverified/failed for pixel fidelity; the current-source CLI build must be
used to determine whether the defect remains.

External reference command (supplemental only):

```sh
ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgba \
  -video_size 640x360 -framerate 30 \
  -i /tmp/chrononmotion-gltf-scaled-final.rgba \
  -vf format=yuv420p -c:v libx264 -crf 18 -preset veryfast \
  ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/software-reference-libx264.mp4
```

Strict-native Vulkan mesh/IBL sampling and parity, hardware certification, and
same-SHA CI remain open for their owning tickets.
