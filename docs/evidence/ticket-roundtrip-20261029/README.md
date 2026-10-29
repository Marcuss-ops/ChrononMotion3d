# Motion glTF / HDRI downstream CLI investigation — 2026-10-29

This evidence is from dirty working trees. ChrononMotion3D HEAD before edits:
`ea8982dc6d870852f42030154ff2a816d7686af7`. Chronon3D HEAD:
`014eeb4dfd1703820d49b6382d127ab253bf1449`. Host: Linux x86_64. Chronon3D
used the existing Release binary at
`Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`.
Software rendering used NVENC `h264_nvenc` for video output. No strict Vulkan
run or CI run is claimed.

## Producer and consumer validation

```sh
cmake --build ChrononMotion3D/build/release-fast --target chrononmotion_authoring -j4
CHRONONMOTION_TICKET_GLTF_PLAN="$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-20261029/gltf/animated-gltf.plan.json" \
CHRONONMOTION_TICKET_HDRI_PLAN="$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-20261029/hdri/hdri.plan.json" \
  ChrononMotion3D/build/release-fast/chrononmotion_authoring
```

Result: build PASS; authoring executable reports 349 checks, 0 failures. The
test harness emits the skin+morph RenderPlan and HDRI RenderPlan plus binary
`HDRIENV1`. Its video fixtures explicitly declare `output.format=mp4` (without
that field, requesting `--output *.mp4` selects the PNG still route during
preparation). Synthetic base-color and ORM PNGs are mounted for the glTF case.
HDRI asset SHA-256:
`d299fb1c6e4012f84951ff00a39d92e25857bc4db9e27a524e7c4e3bfc9fa04a`.
`ctest --test-dir ChrononMotion3D/build/release-fast --output-on-failure -R
'^chrononmotion_authoring$'` passes 1/1. The build also exposed and fixed an
extra closing parenthesis in `RenderPlanEmissionValidateText.cpp` that
prevented the adapter from compiling.

`chronon3d_cli validate --plan <plan> --assets-root <fixture-root>` returns
`status=PASS, valid=true` for both plans. HDRI content digest:
`bfed6f705dc0ce019d5dfcae129dc03e9dfca463dc7bc6a93c556e5420a318ba`.

## CLI video observations

Commands used the canonical `chronon3d_cli render --plan ... --assets-root
... --backend software --profile preview --hardware nvenc --codec h264_nvenc
--output ...` route.

- glTF output: MP4 written successfully, 640×360, 30 fps, 31/31 frames
  encoded. SHA-256:
  `48d14f308cd7b8b1aa6ab24deb063430aa84322a7851c945ca77b11f3305817c`.
  Decoded frames 0, 15, and 30 are byte-identical; all 31 framemd5 entries
  have the same checksum. The output is half black and half flat dark green
  (RGB mean 15.83333/255). It does not visibly demonstrate the baked skin/morph
  motion or the supplied orange/alpha texture. This is a FAIL for the ticket's
  visible animated pixel proof, despite successful plan validation and encode.
- HDRI output: MP4 written successfully, 640×360, 30 fps, 4/4 frames encoded.
  SHA-256:
  `3cf6a001d1d99e27c72ba516855b404956317a99a1802a01ac9b771cddd6a951`.
  Frames are stable, but the decoded image is also half black / half flat dark
  green and exactly matches the glTF decoded frame. The fixture therefore does
  not visibly isolate the red constant HDRI contribution; this is not a valid
  SH/specular golden or parity proof.

PNG sequence probes also trigger Chronon3D's warning that the image lane is
not pixel-truth; they are retained only as diagnosis. Artifacts, logs, plans,
assets, probes and checksums are stored under this directory.

## Acceptance status

- Motion producer tests, bounded input, deterministic bytes, malformed-input
  rejection and bridge contract: covered by the focused tests already linked in
  each ticket.
- RenderPlan decoding, mounted-asset resolution and software CLI video encode:
  PASS for the generated fixtures.
- Visible animated textured/alpha glTF output: FAIL (31 encoded frames are
  identical and do not show the intended subject).
- Visible HDRI contribution and comparison against SH/specular golden: FAIL / no
  discriminating golden; output does not differ from the glTF control.
- Strict Vulkan mesh / IBL sampling and parity: NOT RUN; downstream GPU paths
  remain unimplemented in Chronon3D.
- CI same-SHA: NOT RUN.
