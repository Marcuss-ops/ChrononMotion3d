# P0-04 — mixed skin/morph, camera orbit, 150-frame acceptance

Local producer-to-renderer evidence captured 2026-11-04. This closes the
P0-04 fixture gate locally; it is not a release certification because these
worktrees are dirty and same-SHA CI / release review are separate gates.

Source revisions at capture: CM3D `b7137ee0094e7f750a287305ac30b0273c583b22`
and C3D `b1b21cbed1d8927de8929a33924c1f4d097955c4`; both worktrees were dirty.
The producer test executable SHA-256 was
`27dbff0c14f031f1841c9fa7024b0b251047936b617b2af8732cf4cd6ccfa0f9`. The
software CLI was `980bcb42d42af67b80abe2e0a029735bbaabc4e206bbf31a3ebe382a56d627d1`;
its stripped copy used on `gpu-node` was
`fd5066566ead9df6e2690e043ed3c503a1957d485a6236a968c82c10e26739e3`.

## Fixture and producer checks

The fixture is self-contained and synthetic. `tests/gltf_motion_fixture.hpp`
encodes a valid glTF JSON asset with one triangle, one skin joint, one POSITION
morph target, a joint-translation channel, and a morph-weight channel. The
producer imports the asset with `loadGltfMotion`, maps it to `RigProgram`,
retimes both authored channels over frames 0–149, evaluates skin and morph,
and lowers all 150 geometry samples to RenderPlan V3 `inline_mesh.animation`.
There is no renderer-side rig or alternate execution path.

The camera has five resolved samples over frames 0, 37, 74, 112, and 149. Its
position and yaw trace an 80-degree orbit around the triangle; pitch also
changes. The triangle is rendered flat red on transparent black for clear
coverage measurements.

Producer command and result:

```bash
ninja -C build/release-fast -j12 chrononmotion_authoring
CHRONONMOTION_TICKET_GLTF_CAMERA_PLAN=docs/evidence/camera-mesh-yaw-isolation-20261104/skinned-morph-camera-orbit.plan.json \
  build/release-fast/chrononmotion_authoring
```

`chrononmotion_authoring`: **386 checks, 0 failures**. The new checks verify
150 baked frames, the joint translation at frame 149, the morph weight at
frame 74, and emitted geometry plus a changing camera position. Plan SHA-256:
`4132b79a04cdd6a92e1bd01ac59a196469a4829a9b0ca0d0c3a1a2d3e9c6f8a5`.

Building this producer target against the current C3D worktree also exposed
and removed two obsolete calls to a superseded DoF helper signature. Both
paths already validate through C3D's shared `camera_contract_problem`, which
checks iris shape and highlight gain separately. The adapter and producer
suite now build/pass against the current C3D headers; a declared release
version matrix and same-SHA CI are still required for P0-03.

## Renderer results

The same plan was rendered over frames 0–149 at 640×360 with the C3D CLI,
first on software and then on the remote NVIDIA Vulkan backend. Each run wrote
150 RGBA frames (138,240,000 bytes); neither output contains an empty frame,
and all 150 frame hashes are distinct.

| Backend | Alpha-covered pixels per frame | Union bounds | Raw stream SHA-256 |
|---|---:|---|---|
| Software | 510–950 | x=320–429, y=83–188 | `0652d91254deaba764344e5c6c6abc15c24a45539985f45361f15c9c957f0756` |
| Vulkan, RTX A4000 | 540–969 | x=320–429, y=83–188 | `be47058373d4083edf5cf2449dfea052a3cc71d830c0228a5c7539eb8fb0415f` |

Raw frame alpha coverage is a geometric no-collapse check, not a universal
pixel-parity claim. Raw streams are retained as Zstandard files with their
uncompressed hashes in the render logs. Per-frame bounds, coverage and RGBA
hashes are in the two CSV files. PNGs at frames 0, 37, 74, 112, and 149 show
the camera path and deforming silhouette for each backend.

Software invocation:

```bash
../Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli \
  render-plan --input docs/evidence/camera-mesh-yaw-isolation-20261104/skinned-morph-camera-orbit.plan.json \
  --output /tmp/p004-software.rgba --backend software --video-sink raw \
  --start-frame 0 --end-frame 149
```

For Vulkan, the same plan was copied to `/tmp/chronon2-p004-camera-20261104/plan.json`
on `gpu-node`, with the stripped CLI and vcpkg Vulkan loader bundle. The exact
loader/backend invocation was:

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
/tmp/chronon3d_gpu_bundle_20261103/lib/ld-linux-x86-64.so.2 \
  --library-path /tmp/chronon3d_gpu_bundle_20261103/lib:/tmp/chronon2-p004-camera-20261104/lib \
  /tmp/chronon2-p004-camera-20261104/chronon3d_cli render-plan \
  --input /tmp/chronon2-p004-camera-20261104/plan.json \
  --output /tmp/chronon2-p004-camera-20261104/vulkan.rgba \
  --backend vulkan --gpu-hot-path-mode auto --video-sink raw \
  --start-frame 0 --end-frame 149
```

The GPU run used `gpu-node` (RTX A4000, driver 595.99.02), the NVIDIA ICD,
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, and the C3D binary with SHA-256
`fd5066566ead9df6e2690e043ed3c503a1957d485a6236a968c82c10e26739e3`. The
loader probe confirms that the Khronos validation layer was inserted. The
150-frame log contains no `Validation Error`, VUID, or `[error]` matches. This
is a Vulkan render with host raw readback (`--gpu-hot-path-mode auto`), not a
strict-native video-export test.

## Artifacts

- `skinned-morph-camera-orbit.plan.json` — exact V3 handoff plan
- `software-render.log`, `software-timing.json`, `software.rgba.zst`,
  `software-metrics.csv`, and five `software-frame-*.png` samples
- `vulkan-render.log`, `vulkan-layer-load-probe.log`, `vulkan-timing.json`,
  `vulkan.rgba.zst`, `vulkan-metrics.csv`, and five `vulkan-frame-*.png` samples

## Scope and disposition

**P0-04: PASS on the declared synthetic 150-frame fixture locally.** The
camera translates/orbits/rotates while a skin joint and morph target both
deform the same glTF primitive; software and Vulkan show no collapse and stay
inside the recorded bounds. This does not claim acceptance of a production
character asset, clean-checkout CI, same-SHA cross-repository CI, or human
review of the full sequence. Those release-wide gates remain open in C3D's
Chronon 2.0 tracker.
