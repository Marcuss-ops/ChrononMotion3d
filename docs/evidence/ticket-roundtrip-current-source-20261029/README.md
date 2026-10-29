# Current-source ChrononMotion3D → Chronon3D probes — 2026-10-29

## Provenance

- ChrononMotion3D `HEAD`: `ea8982dc6d870852f42030154ff2a816d7686af7` (dirty).
- Chronon3D `HEAD`: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty).
- Chronon3D CLI: `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`, rebuilt from this checkout with target `chronon3d_cli` and `-j7`.
- Host: Linux x86_64, NVIDIA RTX A4000. Software raw probes used `--backend software`; the NVENC probes used the H.264 NVENC encoder.
- The source trees had existing uncommitted changes. These are local current-tree results, not clean-SHA CI or a strict-native Vulkan certification.

## Animated glTF software raw probe

Command (exit 0):

```sh
CHRONON3D_VIDEO_PROGRAM_ROUTE=0 Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
  --assets-root "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf" \
  --backend software --start-frame 0 --end-frame 30 --video-sink raw \
  --output /tmp/chrononmotion-gltf-current-source.rgba -v info
```

- Raw RGBA: 31 frames × 640 × 360 × 4 bytes; SHA-256 `a8d12571e3cc657b7a847dfc05a8888c766b6d9658aecbae3a18c65c88fe2648`.
- 31/31 per-frame SHA-256 hashes are distinct. Each frame contains 1,642 nontransparent pixels with the textured alpha-cutout mesh.
- Alpha bounds: frame 0 `[320,118,377,172]`, frame 15 `[390,83,447,137]`, frame 30 `[460,48,517,102]`.
- Current-source `validate` returns `status=PASS, valid=true`, content digest `f9e92b50f47259c927c4e31f75220b45fd7efc75f60cf944e330a7417a975c25`.

This verifies the producer plan through the current-source Chronon3D **software raw** route. It does not certify Vulkan mesh drawing or encoded-video pixels.

## HDRI software raw probe

Current-source `validate` returns `status=PASS, valid=true` for the HDRI plan, content digest `66dddcd4c2690ccff58aaa700b5e83928b138462ca053257daa469cc9802972a`. The mounted `HDRIENV1` asset SHA-256 is `d299fb1c6e4012f84951ff00a39d92e25857bc4db9e27a524e7c4e3bfc9fa04a`.

The two software raw commands used the same current-source CLI, same asset root, frames 0–3 and `--video-sink raw`; one used `hdri/hdri.plan.json`, the control used `hdri/without-environment.plan.json`:

```sh
CHRONON3D_VIDEO_PROGRAM_ROUTE=0 chronon3d_cli render --plan "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri/hdri.plan.json" --assets-root "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri" --backend software --start-frame 0 --end-frame 3 --video-sink raw --output /tmp/chrononmotion-hdri-current-source.rgba -v info
CHRONON3D_VIDEO_PROGRAM_ROUTE=0 chronon3d_cli render --plan "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri/without-environment.plan.json" --assets-root "$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri" --backend software --start-frame 0 --end-frame 3 --video-sink raw --output /tmp/chrononmotion-hdri-noenv-current-source.rgba -v info
```

- With environment: 4 frames, SHA-256 `a17f7a4221a4f538f5bf72d0ef37eefe9b9e85360c5c4053ddc60c8bf3857ef8`.
- Without environment: 4 frames, SHA-256 `f4c5248a14500aed3616654ea272a0cc6b7e546e3adbced4673165ac559509e6`.
- All 20,000 opaque mesh pixels change in every frame. Pixel `(320,180)` changes from `(6,71,32,255)` to `(25,71,32,255)` with the red SH probe.

This confirms a visible diffuse SH contribution in the software raw route. No quantitative specular mip golden or Vulkan parity was run.

## Current-source encoded NVENC check — initial failure, superseded below

Both runs used the current-source CLI, `--backend software --profile preview --hardware nvenc --codec h264_nvenc`, and 31 input frames. One used the default program route; the other set `CHRONON3D_VIDEO_PROGRAM_ROUTE=0` to force the legacy composition route. Both MP4 files decode as 640×360 H.264 at 30 fps with 31 frames.

- Default program route: decoded frame SHA-256 `98e9acd5ac91df4388769ba44b7019f150abb1d6cf89876cc4c84487fa763dad`; all 31 decoded frames are identical and show a flat green half-frame instead of the animated textured subject.
- Legacy composition route: decoded RGBA SHA-256 `544a672e39a4d52ec6fbea99a103a6694bbee9c5b399ab53d4138a7dee785246`; 31 frame hashes differ, but the image is still corrupted (about 116k nonblack pixels per frame versus 1,642 nontransparent subject pixels in the software raw reference; RGB RMSE 39.293 against black-composited raw).
- This is a current-source encode failure, unlike the prior stale-binary result. Native Vulkan drawing and native encoded output remain open.

## NV12 staging layout fix — 2026-10-29

The failure above was traced to a format mismatch in Chronon3D's host staging
path: it converted to planar YUV420P, while the CUDA frames context uses NV12.
The CPU staging frame and conversion request now use NV12 before
`av_hwframe_transfer_data`. Chronon3D rebuilt successfully with
`cmake --build build/chronon/linux-video-release --target chronon3d_cli -- -j7`.

Re-rendering the same current-source fixture through both the default route
(override unset) and the canonical composition route (`CHRONON3D_VIDEO_PROGRAM_ROUTE=0`)
produced identical NVENC MP4 bytes. Each output decodes to 31/31 distinct
640×360 H.264 frames. Against the software RGBA reference composited over
black, decoded RGB RMSE is 0.7673 and MAE is 0.0361 on the 0–255 scale. The
raw RGBA decoder output SHA-256 is
`9d2e9f37ce567b69c3b524d8f33193422c51873f0a2e78ce01dd683ee67aa9c6`; the MP4
SHA-256 is `35530c11cc166c755883f98d58b1fe1a4a09928c69b2ca3af696e1d9b6177cf7`.
Detailed command and method: [Chronon3D NV12 layout evidence](../../../Chronon3d/docs/evidence/native-av-nv12-layout-20261029/README.md).

The actual Chronon3D NVENC export and a zoomed derivative are available in
the requested Drive folder: [full frame](https://drive.google.com/file/d/1WLNb7-Ckv3f08xIxmXDYpmftENK6iWvw/view?usp=drivesdk),
[zoom](https://drive.google.com/file/d/1NPlBUfDuJxITh8DEnK7FXt2XJKyR3ghe/view?usp=drivesdk).

This resolves encoded-output fidelity for the tested software-render → NVENC
configuration. Strict-native Vulkan drawing, GPU-render parity and clean-SHA
CI remain open.

## Visual review files

Two 3.1-second H.264 previews were created with system FFmpeg from the verified software raw frames and uploaded to the user-provided Drive folder. They are looped visual references, **not** Chronon3D NVENC output:

- [Full-frame software reference preview](https://drive.google.com/file/d/1Zqiy3VvRQ0xTKDqTdcPOSdLAl_-AVLfw/view?usp=drivesdk)
- [Zoomed software reference preview](https://drive.google.com/file/d/1VgV8E70QeNOyjKFxMazK2CRWaPzR2ATQ/view?usp=drivesdk)
