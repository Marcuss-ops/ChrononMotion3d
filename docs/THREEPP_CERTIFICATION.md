# Threepp → Chronon visual certification

This is the execution ledger for the 15 user-defined certification cases. A
class or shader being present is not a PASS. PASS requires the production
integration path, image/video inspection, and the matching numeric gates.
Unexecuted items remain `PARTIAL`, `MISSING` or `BLOCKED`; partial code coverage does not imply visual
correctness.

## Acceptance setup

- Reference output: 1920×1080, 30 fps, 150–180 frames (5–6 seconds).
- Each executed case writes its scene/configuration, MP4, frame 0/middle/last
  PNGs, metadata, and a log under `out/threepp_certification/Tnn/`.
- Headless GL uses the available context provider. Metadata records the actual
  GL renderer. VRAM and GPU-utilization fields stay null when the backend does
  not expose them. A GPU-quality PASS requires an end-to-end hardware-resident path from rendering
  through composition to hardware video encode. CPU framebuffer readback,
  software rendering/encoding, CPU-only paths, fallback, or downgrade means the
  case is PARTIAL, even if some stages use hardware. Record the adapter, native
  surface counts, readback/copy counts, encoder, and telemetry. `--backend vulkan`
  plus `require_gpu_native` is used for strict Chronon3D media cases. The separate alpha-source MOV is explicitly
  `SOURCE_ONLY`: GL uses NVIDIA, but readback and qtrle encoding use CPU.
- Threepp renderer cases flow through `MotionSceneBridge` when Motion owns the
  time/space state. T13/T15 additionally require Chronon3D's actual composition
  and video path; a threepp-only MP4 cannot certify those cases.
- Khronos GLB fixtures and license/hash provenance are in
  [`render/spike/assets/threepp-certification/README.md`](../render/spike/assets/threepp-certification/README.md).

## Priority todo from visual review

The latest review correctly separates a technical gate from a quality
demonstration. The first T03/T04 exports were pixel-identical because the T04
runner reused the PBR stream. That defect was fixed: T04 now renders its own
fixed-material sphere and orbiting light, and its video/hash and temporal image
deltas differ from T03. Keep this regression check in the suite so the old
failure cannot return.

### Next actions

- [x] **GPU gate for every certification:** each GPU-quality PASS must record
  the actual hardware renderer/adapter and hardware encoder, and show no
  software renderer, software encode, fallback, or downgrade. Record CPU
  readback explicitly: GPU rendering with a CPU handoff is hardware-backed,
  and fails the full-GPU acceptance gate. Do not promote a CPU-handoff,
  software-only, or llvmpipe run to PASS. Shared `gpu_certification_gate.hpp`
  now makes T03/T04/T05/T06/T07/T08/T10/T11/T14 fail fast unless NVIDIA GL and
  NVENC are selected; software encoder fallbacks were removed from those
  runners. They remain PARTIAL because their frame handoff still reads into
  CPU memory. NVIDIA + NVENC proves hardware stages only where CPU framebuffer
  readback is recorded; those cases do not meet the full-GPU requirement.
- [ ] **T03/T04/T07 visual refinement:** T03 and T04 already have NVIDIA/NVENC
  runs; T03 still needs a trusted PBR golden. T07 now has an NVIDIA/NVENC run,
  but nested precomp timing and clip blending remain open. Enlarge subjects
  where needed and inspect complete 1920×1080 playback before upgrading quality
  or integration status.
- [x] **T11 hardware rerun:** bloom now runs on NVIDIA RTX A4000 and NVENC with
  sampled GPU/encoder/VRAM telemetry. CPU framebuffer readback remains explicit;
  retain as a hardware-stage demo only, not a full-GPU PASS.
- [x] **T05 hardware rerun:** shadow mapping now runs on NVIDIA RTX A4000 with
  NVENC and sampled GPU/encoder/VRAM telemetry. CPU framebuffer readback is
  disclosed; retain as a hardware-stage demo only, not a full-GPU PASS.
- [x] **T04 lighting — verify the orbit:** the rebuilt five-second clip was
  reviewed as a 150-frame contact sheet. An all-frame brightness-centroid trace
  now checks the sphere's left → top → right → bottom → return path and endpoint
  proximity; measured centroids are recorded in metadata. Keep the T03/T04
  frame/hash inequality regression. This remains a basic lighting check, not a
  production-lighting claim.
- [ ] **T03 PBR — establish a trusted reference:** the four spheres fill 37.3%
  of frame height. Visual inspection exposed an RGB buffer declared as RGBA in
  the procedural normal map, which caused the diagonal bands; the fixture now
  sets `Format::RGB`, and the corrected map changes 86,203 pixels when toggled
  off. The materials are distinct, but a trusted PBR golden and hardware
  comparison remain open, so keep PARTIAL.
- [x] **T07 skeleton — show the rig clearly:** switched from the Fox `Survey`
  clip, which mainly moved the head and tail, to its `Run` clip. The full
  150-frame contact sheet shows the gait; at frame 75 the fox fills 53.43% of
  frame height, leg/body deformation changes bone matrices by 891.593, and
  random-access frame 77 is identical. Nested precomp timing and clip blending
  remain open.
- [x] **T10 depth of field — rack focus across real subjects:** the pinned Fox,
  Avocado and CesiumMilkTruck GLBs render at 6, 7.5 and 9 units through
  `GLTFLoader → MotionSceneBridge → EffectComposer → BokehPass`. On NVIDIA RTX
  A4000 with NVENC, same-subject ROI gradients peak at the requested focus and
  frame 77 is identical on random access. Telemetry captured 45 samples (GPU
  peak 6%, encoder peak 17%, whole-device VRAM peak 1,938 MiB). The framebuffer
  is read back to CPU before encoding, so it fails the full-GPU-resident gate. [Drive evidence](https://drive.google.com/file/d/1uZO4zQxX0YcYTaIG0ag1cJAu-CN-VStP/view?usp=drivesdk)
- [ ] **T10 quality/composition:** make the subjects larger and more legible,
  review complete playback, and integrate the focus rack in a Chronon3D scene.
- [x] **T13 alpha still-image composition — preserve transparency on Vulkan:**
  root cause was `SourceNode::can_seed_full_frame` treating layer opacity 1 as
  proof that the decoded PNG itself is opaque. The early-exit pass then dropped
  the background and left its 64×64 transparent placeholder. ImageCache now
  records decoded alpha opacity, and the seed/early-exit check requires an
  actually opaque asset (unknown assets conservatively retain lower layers).
  The full six-layer composition passed 150 frames at 1920×1080/30 through
  strict Vulkan and NVENC: all five `CompositeNode` operations per frame had both inputs as
  1920×1080 GPU handles, 150/150 native output surfaces, zero conversion bytes,
  zero encoder staging-copy bytes, and zero execution failures. Four
  transparent/unoccluded background patches differ from the source backdrop by
  MAE 1.15–1.50 RGB levels after H.264 encoding. [Drive MP4](https://drive.google.com/file/d/1-ba876wrbR-WLFfuBNhITDi0sXnurimt/view?usp=drivesdk)
- [ ] **T13 animated alpha video — keep the remaining gap explicit:** the
  still-image test proves GPU compositing of decoded alpha PNG assets, not
  GPU-native decoding of a continuous alpha video or a moving transparent GLB.
  The existing qtrle source is CPU-readback/CPU-encoded and rejected by the
  strict native decoder. Add a native alpha video decode/encode route before
  claiming animated alpha production support.
- [ ] **T13 compositing — retain backend parity and verify provenance:** all 150
  decoded frames have identical software/Vulkan vehicle ROIs. Overlay pixel
  deltas are recorded, but isolate Chronon3D text output to prove its
  provenance. The Chronon3D software video decoder previously forced alpha to
  opaque; it now preserves decoded alpha and a YUVA444 regression covers
  transparent, fractional and opaque samples. A threepp GLB was rendered on
  NVIDIA to an RGBA qtrle MOV (frame 75: 1,914,612 transparent and 158,988
  opaque pixels). Chronon3D Vulkan + NVENC with `require_gpu_native` rejects
  that alpha codec because no GPU-native decoded surface is available; qtrle
  therefore cannot certify a full-GPU alpha video handoff. A Vulkan image-layer
  attempt also failed at native-surface materialization. Keep alpha handoff
  PARTIAL until a GPU-native alpha-capable source path works; native GLB
  ingestion remains separate and unverified. [Alpha source clip](https://drive.google.com/file/d/1U2o59D9EIdPJKVgNu8bsKnfkgzDQzn3s/view?usp=drivesdk)
- [x] **T03/T04 visual evidence — investigate the identical sampled frames:**
  the original files were duplicates. Both runners now produce independent
  NVIDIA-rendered scenes and NVENC videos; T04 passes its moving-light gate.
  T03 stays PARTIAL pending a trusted material golden. Drive contains the
  replacement clips; their metadata identifies CPU framebuffer readback before
  NVENC, so these are hardware-stage demos and do not meet full-GPU PASS.
- [ ] **T03/T04/T07/T08/T10 presentation quality:** enlarge and frame the
  subjects so PBR response, lighting, skeleton deformation, morph silhouette
  changes and rack focus are easy to judge in complete 1920x1080 playback.
- [ ] **All demonstrations — review the actual videos:** inspect every full
   1920×1080, 30 fps clip at playback size; use sampled frames as evidence, not
   substitutes for the video. Preserve scene configs, logs, metrics, and honest
   status scope with each incremental Drive upload.

## Test matrix

| ID | Feature | Status | Production path / evidence | Output | Remaining blocker |
|---|---|---|---|---|---|
| T01 | Geometry & camera | PARTIAL | `MotionSceneBridge` creates Chronon box/sphere meshes, applies hierarchy and camera poses; existing `render/spike/bridge.cpp` checks coverage and random access. | Existing low-resolution PPM proof only. | 1080p five-second video and face/occlusion golden. |
| T02 | UV & textures | PARTIAL | T06 verifies finite UV attributes on all five imported CesiumMilkTruck meshes and preserves three mapped materials through the bridge. | [T06 MP4](../out/threepp_certification/T06/T06_gltf_pbr_1080p30.mp4), [frame 75](../out/threepp_certification/T06/frame-75.png), [metadata](../out/threepp_certification/T06/metadata.json) | Use a numbered checker fixture and compare UV orientation, sampler filtering and mip transitions to reference pixels. |
| T03 | PBR materials | PARTIAL (hardware stages; CPU readback) | Chronon roughness, metalness and emission map to threepp `MeshStandardMaterial`. Four enlarged spheres under deterministic HDR occupy 37.4% of frame height. A bound procedural normal map changes 86,264 pixels. The refreshed run uses NVIDIA RTX A4000 and NVENC; random-access frame 77 is identical. | [NVIDIA/NVENC MP4](../out/threepp_certification/nvidia/T03/T03_pbr_1080p30.mp4), [frame 75](../out/threepp_certification/nvidia/T03/frame-75.png), [metadata](../out/threepp_certification/nvidia/T03/metadata.json), [run log](../out/threepp_certification/nvidia/T03/run.log) | Establish trusted PBR/normal-map golden tolerances; procedural materials are response tests, not reference assets. |
| T04 | Lighting | PARTIAL (hardware stages; CPU readback) | Independent MotionScene with a fixed Lambert sphere, low ambient fill and an orbiting point light. NVIDIA RTX A4000 rendering plus NVENC; brightness centroid follows left → top → right → bottom → return across all frames. Temporal frame pairs change 306,759, 307,035 and 203,445 pixels; frame 77 matches random access. | [NVIDIA/NVENC MP4](../out/threepp_certification/nvidia/T04/T04_lighting_1080p30.mp4), [all-frame metadata](../out/threepp_certification/nvidia/T04/metadata.json), [frame 75](../out/threepp_certification/nvidia/T04/frame-75.png), [run log](../out/threepp_certification/nvidia/T04/run.log) | Basic light orbit only; no production-quality lighting or shadows. |
| T05 | Shadow mapping | PARTIAL (hardware stages; CPU readback) | Motion-authored directional light track, shadow caster/receiver flags and bias/radius flow through `MotionScene → MotionSceneBridge → NVIDIA GLShadowMap (PFCSoft) → GLRenderer → NVENC`. NVIDIA RTX A4000; masks contain 10,303/2,407/10,207 pixels at frames 0/75/149, symmetric difference 9,804, centroid motion 86.85 pixels, frame 77 random access matches. 22 telemetry samples: GPU peak 6%, encoder peak 27%, whole-device VRAM peak 1,555 MiB. Framebuffer readback to CPU precedes NVENC; not zero-copy. | [Drive MP4](https://drive.google.com/file/d/1QjaE4c12_coBpZ4lNuJpj1TCvyzkc9Eu/view?usp=drivesdk), [local NVIDIA/NVENC MP4](../out/threepp_certification/nvidia/T05/T05_dynamic_shadows_1080p30.mp4), [frame 0](../out/threepp_certification/nvidia/T05/frame-0.png), [frame 75](../out/threepp_certification/nvidia/T05/frame-75.png), [metadata](../out/threepp_certification/nvidia/T05/metadata.json), [GPU telemetry](../out/threepp_certification/nvidia/T05/gpu-telemetry.csv) | Basic moving-shadow demonstration; tune light/shadow quality and integrate into Chronon3D production composition. |
| T06 | Full GLB import | PARTIAL (hardware stages; CPU readback) | Pinned Khronos CesiumMilkTruck with five meshes loads through `GLTFLoader` into a Motion asset layer; bridge hierarchy/bounds, UVs, five standard materials and three maps are checked. Refreshed NVIDIA RTX A4000 GL render plus NVENC encode; frame 77 random access is exact. | [NVIDIA/NVENC MP4](../out/threepp_certification/nvidia/T06/T06_gltf_pbr_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T06/metadata.json), [run log](../out/threepp_certification/nvidia/T06/run.log) | Trusted orientation, texture sampling and PBR golden comparison remain open. threepp reads GL frames back to CPU before NVENC; Chronon re-decodes this video on NVDEC. |
| T07 | Skeleton animation | PARTIAL | `GLTFLoader` Fox `Run` clip binds to a top-level Motion mesh layer; bridge samples absolute `frame/fps`; cloned `SkinnedMesh` joints remap. NVIDIA RTX A4000/NVENC, subject height 53.52%, bone matrices change 891.593, frame 77 random access matches. 26 telemetry samples: GPU peak 7%, encoder peak 17%, whole-device VRAM peak 1,555 MiB. CPU framebuffer readback precedes NVENC; fails the full-GPU-resident acceptance gate. | [Drive MP4](https://drive.google.com/file/d/1DKbNmyJfuKK0YR6oUSq4RzvvMcR71etE/view?usp=drivesdk), [local MP4](../out/threepp_certification/nvidia/T07/T07_animation_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T07/metadata.json), [telemetry](../out/threepp_certification/nvidia/T07/gpu-telemetry.csv), [scene](../out/threepp_certification/nvidia/T07/scene.json) | One top-level clip only; add nested precomp/multi-clip coverage and review full playback. |
| T08 | Morph targets | PARTIAL | Khronos AnimatedMorphCube `Square` clip samples through the Chronon timeline; bridge preserves weights and GLTFLoader enables morph shader variants. NVIDIA RTX A4000/NVENC; morph weight delta 1.0, 626,152 pixels change between frames 0/75, subject occupies 64.1% of frame height, random frame 77 matches. 25 telemetry samples: GPU peak 5%, encoder peak 17%, whole-device VRAM peak 1,545 MiB. A test-only orange tint and three-quarter camera make the deformation easier to see. CPU framebuffer readback precedes NVENC; fails the full-GPU-resident acceptance gate. | [Drive MP4](https://drive.google.com/file/d/1LvIW6HVLeU0GVyepdaulLM_IU-zvs6EY/view?usp=drivesdk), [local MP4](../out/threepp_certification/nvidia/T08/T08_animation_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T08/metadata.json), [scene](../out/threepp_certification/nvidia/T08/scene.json), [telemetry](../out/threepp_certification/nvidia/T08/gpu-telemetry.csv) | Single morph-only cube and one clip; mixed skin+morph, broader target validation, and a more expressive authored asset remain open. |
| T09 | Camera & depth | PARTIAL | CameraRig is sampled through the bridge; existing bridge proof checks changing frames. | Existing low-resolution PPM proof only. | 1080p dolly/orbit clip plus depth/occlusion/clipping checks. |
| T10 | Depth of field | PARTIAL | Pinned Fox, Avocado and CesiumMilkTruck GLBs render through `GLTFLoader → MotionSceneBridge → EffectComposer → BokehPass`. NVIDIA RTX A4000 and NVENC; focus follows subject depth, frame 77 is deterministic. 45 telemetry samples: GPU peak 6%, encoder peak 17%, whole-device VRAM peak 1,938 MiB. CPU framebuffer readback precedes encode; fails the full-GPU-resident acceptance gate. | [Drive MP4](https://drive.google.com/file/d/1uZO4zQxX0YcYTaIG0ag1cJAu-CN-VStP/view?usp=drivesdk), [local MP4](../out/threepp_certification/nvidia/T10/T10_dof_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T10/metadata.json), [telemetry](../out/threepp_certification/nvidia/T10/gpu-telemetry.csv), [scene](../out/threepp_certification/nvidia/T10/scene.json) | Presentation quality needs larger subjects and full playback review; Chronon3D composite integration remains open. |
| T11 | Post-processing | PARTIAL (hardware stages; CPU readback) | `MotionScene → MotionSceneBridge → NVIDIA GLRenderer → EffectComposer → UnrealBloomPass → NVENC`; 150 frames at 1920×1080/30, bloom toggle changes 662,469 pixels, random-access frame 77 is identical. NVIDIA RTX A4000 and NVENC confirmed; 36 GPU telemetry samples show 8% GPU-utilization peak, 18% encoder-utilization peak, 1,699 MiB whole-device VRAM peak. GL framebuffer is read back to CPU before NVENC, so this fails the full-GPU-resident acceptance gate. | [Drive MP4](https://drive.google.com/file/d/1GuJIZg-1pD-Uzwdq-spXQTVWJOaANDNu/view?usp=drivesdk), [local NVIDIA/NVENC MP4](../out/threepp_certification/nvidia/T11/T11_bloom_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T11/metadata.json), [GPU telemetry](../out/threepp_certification/nvidia/T11/gpu-telemetry.csv), [run log](../out/threepp_certification/nvidia/T11/run.log) | Basic bloom demonstration only; integrate post-processing in Chronon3D production composition and retain the CPU readback disclosure. |
| T12 | Documentary 3D composition | PARTIAL | Documentary/character render spikes exist. | Existing showcase outputs, not this test's tracked scene manifest. | Build an explicit documentary scene and certify its composition at 1080p. |
| T13 | 3D + Chronon compositing | PASS (Chronon native-video segment); PASS (Chronon Vulkan alpha-PNG composite); PARTIAL (full threepp→Chronon handoff and animated alpha) | Opaque source: NVIDIA GLB render → NVENC → NVDEC → Vulkan graph → NVENC. The Chronon segment has 150/150 GPU surfaces, zero readbacks/fallbacks/downgrades, overlay ROIs pass, random-access PSNR 54.23 dB. The upstream threepp GL export reads its framebuffer back to CPU, so the complete threepp→Chronon opaque route is PARTIAL under the full-GPU-resident gate. Alpha image lane: full six-layer Chronon composition at 1920×1080/30 for 150 frames, strict Vulkan/NVENC; native GPU handles on all composited surfaces, zero conversion/staging-copy bytes, transparent unoccluded backdrop MAE 1.15–1.50 RGB after H.264. PNG decode/upload is asset preparation; Chronon composition and video encode remain native GPU. This does not certify an animated full-GPU threepp→Chronon alpha source. Continuous GPU-native alpha video decode is still missing. | [Chronon native-video segment (upstream GL readback disclosed)](https://drive.google.com/file/d/1ljUBYHjg1ImCejV4GxZ2zlDw2P0sJSqZ/view?usp=drivesdk), [alpha composition MP4](https://drive.google.com/file/d/1-ba876wrbR-WLFfuBNhITDi0sXnurimt/view?usp=drivesdk), [alpha scene](../out/threepp_certification/T13-alpha/scene-alpha-chronon-composition.json), [alpha GPU log](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_1080p30.log), [timing/telemetry](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_1080p30.mp4.timing.json), [frame 0](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_frame0.png), [frame 75](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_frame75.png), [frame 149](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_frame149.png) | The native Chronon decode/composite/encode segment passes. The complete threepp-to-Chronon route remains PARTIAL because its upstream GL-to-PNG/video producer uses CPU framebuffer readback; GPU-native animated alpha video decode is also missing. Alpha still-image composition inside Chronon is verified. |
| T14 | Headless GPU rendering | PARTIAL | 180-frame turntable at 1920×1080, 30 fps through `GLTFLoader → MotionSceneBridge → GLRenderer → ffmpeg`; hidden context under Xvfb selected `NVIDIA RTX A4000/PCIe/SSE2`, not llvmpipe. Random-access frame 77 matched byte-for-byte; peak RSS 248,024 KiB. | [MP4](../out/threepp_certification/T14/T14_gltf_pbr_1080p30.mp4), [frame 90](../out/threepp_certification/T14/frame-90.png), [metadata](../out/threepp_certification/T14/metadata.json), [run log](../out/threepp_certification/T14/run.log) | It still requires a virtual X display; GLFW Null + NVIDIA EGL could not choose a surfaceless config. `nvidia-smi` readings are whole-device, not process-attributed. |
| T15 | Production integration | PARTIAL | Motion can render through threepp and Chronon3D can encode video, but no same-job production plan→3D asset→composite→MP4 proof is recorded. | None. | One deterministic end-to-end scene, random-access comparison, repeated-render stability and resource metrics. |

T11's NVIDIA/NVENC run now passes the basic GPU bloom gate; this does not
certify T14/T15 or Chronon3D composition.

## T13 run result

The initial T13 attempt used a software readback/encode lane and is retained
here as historical evidence only. A replacement opaque-source run rendered
the GLB with NVIDIA OpenGL, then used NVENC for the source, NVDEC for input,
Chronon3D's Vulkan render graph, and NVENC for the final output. Chronon3D's
strict `require_gpu_native` run processed 150/150 native GPU surfaces at
1920×1080/30 fps with zero CPU/GPU readbacks, software encode frames,
fallback nodes, or execution downgrades in the Chronon video graph. Vulkan
allocation stayed at 982,089,728 bytes from frames 25–150; whole-device peak
VRAM was 3,036 MB. All 150 decoded frames passed dimensions and overlay ROI
checks. Frame 75 random access scored 54.23 dB PSNR against sequential output.
The NVIDIA threepp GL export reads its framebuffer back to CPU before NVENC.
It proves hardware stages but fails the required full-GPU-resident end-to-end
acceptance gate.

The earlier Chronon3D software and Vulkan parity run exported 150 frames at 1920×1080, 30
fps and 5 seconds. Visual inspection confirmed the model, headline, subhead and
marker are present. A one-frame export at source frame 75 compared with the
sequential H.264 frame yielded 47.999 dB PSNR and 0.182 mean absolute RGB
 difference, above the documented 40 dB tolerance. Vulkan random access yielded
48.320 dB PSNR and 0.179 mean absolute RGB. Vulkan telemetry remained at
451,248,128 bytes and 7 live surfaces from frames 25 to 150, with zero
execution downgrades. Compared with the source
threepp frame, 238,179 pixels changed by more than 8 RGB levels; 100,618 of
those changes are in the overlay ROI. The render receipt reports software
readback/encode lane and Vulkan render backend, H.264, 150 frames and 1920×1080.
This is useful evidence for the external video handoff and composition, but it
is PARTIAL against the stricter full-GPU requirement because readback and encode
used the software lane. It does not claim native GLB mesh ingestion or
transparent alpha from the opaque H.264 source. Per-process GPU memory and peak
RSS are unavailable.

The alpha-video source generated from threepp still uses CPU framebuffer
readback and CPU qtrle encoding, and Chronon3D's strict Vulkan native-video
path rejects it because the decoder returns no GPU-native surface. A separate
PNG still-image route now proves alpha compositing through Chronon3D: ImageCache
records whether every decoded pixel is opaque, and `can_seed_full_frame` uses
that asset fact instead of mistaking layer opacity for image opacity. This
prevents the early-exit pass from deleting the background under a transparent
image. The full six-layer scene renders 150 frames at 1920×1080/30 via Vulkan
surfaces and NVENC with zero conversion/staging-copy bytes; the scene, MP4,
frame captures, log, and timing sidecar are linked in the T13 test matrix. The
remaining alpha gap is continuous GPU-native alpha video decode, not still-image
alpha composition.

The all-frame backend comparison is reproducible with
`python3 render/spike/compare_t13_videos.py --software out/threepp_certification/T13/T13_threepp_to_chronon_composite.mp4 --vulkan out/threepp_certification/T13/T13_vulkan_probe.mp4 --source out/threepp_certification/T06/T06_gltf_pbr_1080p30.mp4 --output out/threepp_certification/T13/backend-parity-all-frames.json`.
It decodes and compares all 150 frames at 1920×1080. The vehicle ROI has MAE 0,
PSNR 99 dB identical-frame sentinel, and mean luminance delta 0 for every
software/Vulkan frame pair. The overlay is compared separately: its raster
differs between backends, while headline, subhead, and marker each differ from
the source render in their own ROI on every frame (minimum changed pixels:
30,324 / 32,025 / 3,292). The measurements are in
[`backend-parity-all-frames.json`](../out/threepp_certification/T13/backend-parity-all-frames.json);
[`compare_t13_videos.py`](../render/spike/compare_t13_videos.py) contains the
reproducible decoder and ROI checks. This certifies the external video handoff
and backend parity for the vehicle region; it does not certify alpha or native
GLB ingestion, and overlay pixel differences are expected to require separate
quality tolerances.

## T05 run result

The initial llvmpipe run remains as historical evidence. The replacement uses
a large receiving floor, a box and sphere that cast shadows, ambient fill, and
a directional key whose position and direction tracks move the shadow across
the ground. `MotionSceneBridge` applies the authored cast and
receive flags and transfers bias, normal bias and radius to threepp's shadow
object. The test renders through `GLShadowMap` with a 1024×1024 PFCSoft map at
1920×1080, 30 fps for 150 frames. It toggles the shadow map off at frames 0,
75 and 149, compares floor-region masks, and confirms the shadow centroid
moves 86.85 pixels between frames 0 and 75; the mask changes 9,804 pixels.
Random-access frame 77 matches its sequential render. The replacement was
rendered on NVIDIA RTX A4000 and encoded with NVENC. Twenty-two 250 ms
`nvidia-smi` samples show GPU utilization peak 6%, encoder utilization peak
27%, and whole-device VRAM peak 1,555 MiB. The GL framebuffer is read back to
CPU before NVENC, so this is hardware-stage evidence only and does not qualify
as a full-GPU PASS.
This runner also exposed and fixed the root-target directional-light direction
track being ignored by MotionScene evaluation.

## T14 run result

The same pinned CesiumMilkTruck fixture rendered through a hidden OpenGL context
under Xvfb with
`__GLX_VENDOR_LIBRARY_NAME=nvidia`. The captured GL renderer was
`NVIDIA RTX A4000/PCIe/SSE2`, GL 3.3.0, driver 595.99.02. The 180-frame
turntable completed at 1920×1080, 30 fps and 6 seconds; frame 77 was
byte-identical on random access. Five meshes, five standard materials, three
textured materials, all UVs, hierarchy and bounds survived import and bridge
cloning. Render time was 6.355 s; peak process RSS was 249,892 KiB. Twenty-seven
`nvidia-smi` samples recorded whole-device VRAM use of 1,168–1,334 MB and peak
utilization of 11%; these are system-wide readings, not per-process allocation.
The no-display probe failed: GLFW Null selected OSMesa by default, which is not
installed; forcing EGL reached NVIDIA's surfaceless device but GLFW requires a
window-capable EGL config. Therefore this is PARTIAL against the stricter
no-display acceptance criterion, even though the GPU rendering itself passed.
The sample video, three PNG snapshots, metadata and logs are retained under
`out/threepp_certification/T14/`.

## T07/T08 run results

The GLTF animation mixer/property-binding sources omitted from the render target
were restored from the pinned threepp revision. `MotionSceneBridge` now binds a
clip to a top-level Motion mesh layer and samples it from absolute
`CompiledScene.frame / fps` time on every sync. Asset cloning now preserves
`SkinnedMesh` type and rebinds cloned skeleton joints; mesh cloning preserves
morph weights; `GLTFLoader` enables morph shader variants for primitives with
morph targets.

T07 renders Khronos Fox's `Run` clip. It contains one skinned mesh, produces
a bone-matrix delta of 891.593 and 108,055 changed pixels between frames 0 and
75; frame 77 is identical on random access. The subject occupies 53.52% of frame
height at frame 75; the runner fails if this falls below 50%. The NVIDIA RTX
A4000/NVENC run sampled 26 telemetry points (GPU utilization peak 7%, encoder
peak 17%, whole-device VRAM peak 1,555 MiB). As elsewhere, the GL framebuffer
is read back to CPU before hardware encoding. T08 renders the Khronos
`AnimatedMorphCube` `Square` clip on NVIDIA RTX A4000/NVENC. Morph-weight delta
is 1.0, 626,152 pixels change from frame 0 to 75, subject height is 64.07%, and
frame 77 matches sequential rendering. A test-only orange material tint and
three-quarter camera improve silhouette readability; scene.json records these
settings. Twenty-five telemetry samples show GPU peak 5%, encoder peak 17%, and
whole-device VRAM peak 1,545 MiB. Framebuffer readback to CPU remains before
NVENC. This is still a single morph-only fixture, not a mixed skin+morph or
multi-target authoring proof. T07 proves the single top-level clip path, not
nested precomp remapping or multi-clip blending.

## T04 run result

The original T04 file was not an independent lighting test: the runner wrote
the T03 PBR frame stream into both MP4 encoders, then measured disabled-light
differences against that PBR scene. The T03 and T04 MP4 SHA-256 hashes and PNG
hashes matched exactly. That PASS was invalid and is withdrawn. The runner now
builds a separate MotionScene containing one large fixed Lambert sphere, fixed
low ambient fill and one animated point light. At 1920×1080/30 fps, frame pairs
0/75, 75/149 and 0/149 changed 305,637, 305,940 and 203,264 pixels; frame 77
matched on random access. The rebuilt T04 MP4 differs from T03. This is a basic
moving-light proof on llvmpipe software GL; it does not certify shadows or
professional lighting quality.

## T03 run result

Chronon exposes a PBR material descriptor and the threepp bridge carries
roughness, metalness, emissive color and emissive intensity into
`MeshStandardMaterial`. Four 0.70-radius Motion-authored spheres render under a
deterministic procedural HDR studio environment at 1920×1080, 30 fps for 5
seconds. The frame-75 subject bounding box is 1,740×403 pixels (37.3% of frame
height). `bindMaterialNormalMap` binds a procedural tangent-space normal map to
the polished metal sample. Visual review caught a format mismatch: the
procedural bytes are RGB, but `DataTexture` defaults to RGBA. Declaring
`Format::RGB` removes the diagonal stripe artifact; the corrected map changes
86,203 pixels when disabled at frame 75. The material values are checked and
frame 77 matches random-access rendering. The test stays PARTIAL until it has a
trusted reference render and hardware validation. Chronon3D's inline RenderPlan material remains a compact
Phong-like approximation rather than a full PBR path.

## T06 run result

The pinned Khronos `CesiumMilkTruck.glb` passes through `GLTFLoader → MotionScene
asset layer → MotionSceneBridge → GLRenderer`. Its five meshes, hierarchy
signature, and source/clone bounds match; all five meshes have finite,
count-matched UVs; five standard materials and three base-color maps survive
cloning. A Chronon-authored turntable and `CameraRig` dolly produced 150 frames
at 1920×1080, 30 fps and 5.0 s. Coverage was 151,757–286,575 pixels, first/last
frames differed by 59,144 pixels, and random-access frame 77 matched
byte-for-byte. The sampled frame was visually inspected. This remains PARTIAL
because texture/UV/material response has no trusted numerical golden render.
llvmpipe is software OpenGL; VRAM is unavailable.

## T10 run result

The checker-cube fixture was replaced with pinned Fox, Avocado and
CesiumMilkTruck GLBs. They load through `GLTFLoader`, bind as MotionScene asset
layers, and render through `MotionSceneBridge → EffectComposer → BokehPass` at
front-plane depths 6.0, 7.5 and 9.0. The camera is fixed while the Chronon focus
track moves near → center → far. Same-object mean neighbor-gradient scores
across focus settings are Fox `[2.283, 1.774, 1.773]`, Avocado `[1.139, 1.734,
1.139]`, and truck `[2.823, 2.823, 5.226]`; each object is more detailed at its
own focus setting than at the other two. The 1080p30, 150-frame NVIDIA RTX A4000 clip encoded with NVENC passed,
random-access frame 77 matched exactly, and the measured same-subject ROI
sharpness peaks at each requested focus distance. The run captured 45 GPU
telemetry samples: GPU utilization peak 6%, encoder peak 17%, and whole-device
VRAM peak 1,938 MiB. The GL framebuffer is read back to CPU before NVENC, so it
is hardware-stage evidence but fails the full-GPU-resident acceptance gate. The actual frame confirms focus
changes, while the subjects remain small; production composition and visual
refinement are open. See `out/threepp_certification/nvidia/T10/` for the scene,
MP4, snapshots, metadata, telemetry, and run log.

## T11 run result

The initial llvmpipe result remains under `out/threepp_certification/T11/` as
historical evidence. The replacement in
`out/threepp_certification/nvidia/T11/` uses NVIDIA RTX A4000 OpenGL and
`h264_nvenc`; an opt-in runner guard fails if llvmpipe/software GL is selected.
It produces 150 frames at 1920×1080/30 fps. Coverage is 816,972–936,852 pixels,
bloom on/off changes 662,469 pixels, and random-access frame 77 is byte-identical
to sequential rendering. `nvidia-smi` sampled every 250 ms for 36 samples:
GPU utilization averaged 4.3% and peaked at 8%, encoder utilization peaked at
18%, and whole-device VRAM peaked at 1,699 MiB. These are device-wide readings.
The threepp GL framebuffer is read back to CPU for image checks and NVENC input;
the render and video encode both run on hardware, and the CPU handoff fails the full-GPU-resident acceptance gate. Chronon3D composition remains outstanding.

## Current provenance and license notes

The restored OpenGL post-processing and animation runtime sources are from the
pinned threepp revision `e2b165ece3904012d97a08029e0bd6cd5252892a`; they are MIT
licensed under the existing threepp/three.js notices. The render target links
the sources without adding a dependency or changing the renderer-neutral core.

External Khronos fixtures are pinned to asset repository commit
`edc7c9e67c639d230715049ee31f9a96a6babbbe`; per-model attribution and hashes
are recorded beside the binaries.

## Remaining integration gaps

1. Complete visual/numeric PBR and texture baselines for GLB imports, including
   normal mapping and material scale that is readable in the output.
2. Extend deterministic clip sampling to nested precomps and blended animation
   clips; the T07 single-clip close-up now visibly demonstrates the Fox run.
3. Tune T05 shadow bias/softness and integrate the hardware-verified T10 focus
   rack in a larger Chronon3D composition.
4. Keep the all-frame T13 software/Vulkan ROI comparison as a regression gate;
   prove overlay provenance and transparent media handoff end-to-end in
   Chronon3D. The native software decoder alpha conversion now has a passing
   YUVA444 regression, but the existing opaque H.264 demo does not exercise it.
5. Add a true surfaceless NVIDIA EGL context for T14 and complete T15 on a real
   GPU with repeated-render and full-pipeline resource evidence.
