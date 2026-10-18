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
- Linux is the only supported target for this certification; Apple and Windows
  build targets are out of scope. Headless GL uses NVIDIA EGL, and metadata records
  the hardware renderer. VRAM and GPU-utilization fields stay null when the backend does
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

- [x] **Linux-only GPU gate for every certification:** each GPU-quality PASS must record
  the actual hardware renderer/adapter and hardware encoder, and show no
  software renderer, software encode, fallback, or downgrade. Record CPU
  readback explicitly: GPU rendering with a CPU handoff is hardware-backed,
  and fails the full-GPU acceptance gate. Do not promote a CPU-handoff,
  software-only, or llvmpipe run to PASS. Shared `gpu_certification_gate.hpp`
  now makes supported runners fail fast unless NVIDIA GL and NVENC are selected.
  T01/T02/T03/T04/T05/T06/T07/T08/T09/T10/T11/T12/T14 encode with GL PBO → CUDA →
  NVENC; T15 uses strict Chronon NVDEC → Vulkan → NVENC. Diagnostic readbacks
  are performed in separately disclosed passes.
  T07/T08/T10 now use the same GPU-resident encode path. Their PARTIAL status
  reflects remaining test coverage and presentation quality, not CPU encoding.
  Certification targets and build instructions are
  Linux-only; Apple and Windows targets are out of scope.
- [x] **T02 UV, texture and mip filtering proof:** the generated six-face numbered atlas passes `GLTFLoader → MotionSceneBridge → NVIDIA GL`; six unique atlas-cell UV ranges validate. Default linear/trilinear mip sampler metadata matches the GLB. On the minified GPU render, disabling mipmaps changes 1,697 pixels; nearest versus linear changes 806. The 150-frame NVIDIA EGL/PBO/CUDA/NVENC clip is uploaded. Keep PARTIAL until an orientation golden and cross-driver tolerance are added.
- [x] **T01/T09 camera and depth proof:** both Linux-only runners produce 150
  frames at 1920×1080/30 through NVIDIA GL PBO → CUDA → NVENC. T01 checks six
  cube-face color palettes, coverage and random-access frame 77. T09 checks
  near/mid/far depth ordering, object masks, orbit/dolly and random access. Their
  clips are uploaded; keep PARTIAL until trusted camera/clipping goldens and
  production-quality compositions exist.
- [ ] **T03/T04/T07 visual refinement:** T03 and T04 already have NVIDIA/NVENC
  runs; T03 still needs a trusted PBR golden. T07 now has an NVIDIA/NVENC run,
  but nested precomp timing and clip blending remain open. Enlarge subjects
  where needed and inspect complete 1920×1080 playback before upgrading quality
  or integration status.
- [x] **T11 full-GPU encode rerun:** bloom now renders through NVIDIA RTX A4000
  and encodes through GL PBO → CUDA → NVENC. All 150 frames are submitted and
  muxed; bloom changes 662,469 diagnostic pixels, frame 77 is deterministic,
  and 153 CPU reads are isolated to a separate diagnostics pass. Keep PARTIAL
  for production-composition and visual-quality coverage, not encoder fallback.
- [x] **T05 full-GPU encode rerun:** shadow mapping now renders on NVIDIA RTX
  A4000 and encodes through GL PBO → CUDA → NVENC, with 150/150 frames submitted
  and muxed. Shadow masks, moving centroid and frame-77 checks pass; 157 CPU
  reads are confined to a separate diagnostics pass.
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
  peak 6%, encoder peak 17%, whole-device VRAM peak 1,938 MiB). Encoding uses GL PBO → CUDA → NVENC;
  diagnostic CPU readbacks are isolated to a separate pass. [Drive evidence](https://drive.google.com/file/d/1uZO4zQxX0YcYTaIG0ag1cJAu-CN-VStP/view?usp=drivesdk)
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
  replacement clips use GL PBO → CUDA → NVENC for encoding. Diagnostic readbacks
  are separate; T03 remains PARTIAL pending a trusted PBR golden.
- [ ] **T03/T04/T07/T08/T10 presentation quality:** enlarge and frame the
  subjects so PBR response, lighting, skeleton deformation, morph silhouette
  changes and rack focus are easy to judge in complete 1920x1080 playback.
- [ ] **All demonstrations — review the actual videos:** inspect every full
   1920×1080, 30 fps clip at playback size; use sampled frames as evidence, not
   substitutes for the video. Preserve scene configs, logs, metrics, and honest
   status scope with each incremental Drive upload.
- [x] **Final proof — 3-word camera FOV + lens rack-focus (DoF) animation (`1920×1080` MP4) with the new `ChrononMotion3D` camera:**
   recreate the reference animation from [Drive folder `18Tck0Y_JNB02ruVmtuzxxaAQV9iKoTHV`](https://drive.google.com/drive/folders/18Tck0Y_JNB02ruVmtuzxxaAQV9iKoTHV) (`frame_0001.png`–`frame_0029.png`) using the newly updated `ChrononMotion3D` `CameraRig` + V3 `lowerToRenderPlan` camera tracks (`PositionX/Y/Z`, `RotationX/Y/Z`, `FovDeg`, `FocusDistance`, `Aperture`, `MaxBlur`, `FocalLengthMM`, `FStop`, `temporalMotionBlur`): a background image plate, three foreground words (`"food"`, `"houses"`, `"see"`) with yellow lower-third highlight bars placed in 3D space, and an animated camera panning/adjusting FOV across each word while shifting the lens focal point (Bokeh/DoF rack focus) onto the active word in `1920×1080` MP4.

## Test matrix

| ID | Feature | Status | Production path / evidence | Output | Remaining blocker |
|---|---|---|---|---|---|
| T01 | Geometry & camera | PARTIAL (Linux full-GPU encode verified) | `camera_certification.cpp` creates six colored cube faces through `MotionSceneBridge`; NVIDIA GL PBO → CUDA → NVENC, 150/150 frames, face-palette hit counts, coverage, and random-access frame 77 pass. Diagnostic readbacks are separate. | [MP4 su Drive](https://drive.google.com/file/d/1fTixa-hMQ2e3kOryB_LdiI1bbr9ZAPqj/view?usp=drivesdk), local `out/threepp_certification/T01/` | Face ordering/clipping golden and production-quality scene remain open. |
| T02 | UV & textures | PARTIAL (Linux full-GPU encode and sampler variants verified) | Generated `NumberedUVCube.glb`: six textured faces, six distinct 3×2 atlas cells, valid normalized UVs. GLTF min/mag filters and mipmap flag match the fixture; production GPU renders compare default mipmapping against linear-no-mip (1,697 changed pixels) and nearest (806 changed pixels). NVIDIA RTX A4000 GL PBO → CUDA → NVENC; 150/150 frames, random frame 77 identical, no CPU framebuffer handoff in encode. | [MP4 su Drive](https://drive.google.com/file/d/1J0DDdVUC8eayGt6zmufrwpA1j8JnT_8E/view?usp=drivesdk), local [MP4](../out/threepp_certification/T02/T02_uv_texture_mipmaps_1080p30.mp4), [scene](../out/threepp_certification/T02/scene.json), [metadata](../out/threepp_certification/T02/metadata.json), [UV/sampler validation](../out/threepp_certification/T02/t02-validation.json), [frame 75](../out/threepp_certification/T02/frame-75.png) | Full-frame trusted UV orientation golden, filtering tolerance across GPU drivers, and professional-quality camera framing remain open. |
| T03 | PBR materials | PARTIAL (GPU encode verified; encoder-drift baseline fixed) | Chronon roughness, metalness and emission map to threepp `MeshStandardMaterial`. Four enlarged spheres under deterministic HDR occupy 37.2% of frame height. The normal-map toggle changes 86,195 pixels. NVIDIA RTX A4000; GL PBO → CUDA → NVENC submitted and muxed 150/150 frames. Frame-77 random access matches; separate diagnostics use 152 CPU readback passes. | [MP4 su Drive](https://drive.google.com/file/d/1RvJk4k8lqJf7QO6Fap___4YEQDrh01B9/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T03/T03_pbr_1080p30.mp4), [frame 75](../out/threepp_certification/nvidia/T03/frame-75.png), [metadata](../out/threepp_certification/nvidia/T03/metadata.json), [golden baseline](../out/threepp_certification/nvidia/T03/golden-tolerance-baseline.json) | Encoder-drift baseline measured (`frame-75.png` vs decoded NVENC output: MAE 5.49 RGB, max 224, 2026-10-17); future T03 revisits compare against this envelope. A trusted external-material golden (Khronos sample GLBs) remains open. |
| T04 | Lighting | PARTIAL (GPU encode verified; presentation review pending) | Independent MotionScene with a fixed Lambert sphere, low ambient fill and an orbiting point light. NVIDIA RTX A4000; GL PBO → CUDA → NVENC submitted and muxed 150/150 frames. Brightness centroid follows left → top → right → bottom → return; temporal pairs change 306,214, 306,495 and 203,389 pixels; frame 77 matches. Separate diagnostics use 151 CPU readback passes. | [MP4 su Drive](https://drive.google.com/file/d/1TwsvVLaUnr_QTskacLEaA9DhY764Nb85/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T04/T04_lighting_1080p30.mp4), [all-frame metadata](../out/threepp_certification/nvidia/T04/metadata.json), [frame 75](../out/threepp_certification/nvidia/T04/frame-75.png) | Basic light orbit only; no production-quality lighting or shadows. |
| T05 | Shadow mapping | PARTIAL (GPU encode verified; quality/integration pending) | Motion-authored directional light track, shadow caster/receiver flags and bias/radius flow through `MotionScene → MotionSceneBridge → NVIDIA GLShadowMap (PFCSoft) → GLRenderer → GL PBO → CUDA → NVENC`. NVIDIA RTX A4000; masks contain 10,240/2,353/10,144 pixels at frames 0/75/149, symmetric difference 9,739, centroid motion 85.27 pixels, frame 77 random access matches. All 150 frames are submitted and muxed; 157 CPU readbacks are in a separate diagnostics pass. | [MP4 su Drive](https://drive.google.com/file/d/1GMawUdiJ7b98wNHcjeSgraYdqzaGD0E5/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T05/T05_dynamic_shadows_1080p30.mp4), [frame 0](../out/threepp_certification/nvidia/T05/frame-0.png), [frame 75](../out/threepp_certification/nvidia/T05/frame-75.png), [metadata](../out/threepp_certification/nvidia/T05/metadata.json) | Basic moving-shadow demonstration; tune light/shadow quality and integrate into Chronon3D production composition. |
| T06 | Full GLB import | PARTIAL (GPU encode verified; golden pending) | Pinned Khronos CesiumMilkTruck with five meshes loads through `GLTFLoader` into a Motion asset layer; bridge hierarchy/bounds, UVs, five standard materials and three maps are checked. NVIDIA RTX A4000, GL PBO → CUDA → NVENC; 150/150 frames submitted/muxed, frame 77 random access exact. Separate diagnostic pass verifies image coverage and geometry. | [MP4 su Drive](https://drive.google.com/file/d/14At3rPbBx16FWWf_WTOlSKGMua2IjBUw/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T06/T06_gltf_pbr_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T06/metadata.json), [frame 75](../out/threepp_certification/nvidia/T06/frame-75.png) | Trusted orientation, texture sampling and PBR golden comparison remain open; Chronon3D re-decode/compositing still needs an end-to-end GPU proof. |
| T07 | Skeleton animation | PARTIAL (GPU encode verified) | `GLTFLoader` Fox `Run` clip binds to a top-level Motion mesh layer; bridge samples absolute `frame/fps`; cloned `SkinnedMesh` joints remap. NVIDIA RTX A4000; GL PBO → CUDA → NVENC submitted and muxed 150/150 frames, subject height 53.43%, bone matrices change 891.593, frame 77 matches. Diagnostic snapshots use a separate 151-readback pass. | [MP4 su Drive](https://drive.google.com/file/d/1ANxIh877VGfmU1gOwlFbs-3GFsFblAbF/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T07/T07_animation_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T07/metadata.json), [scene](../out/threepp_certification/nvidia/T07/scene.json) | Nested precomp timing remains open; the two-clip crossfade is locally demonstrated below. |
| T07-BLEND | Fox Walk → Survey → Run | PARTIAL (clip-sequence behavior verified; composition review pending) | Follow-up fix (2026-10-17): Run now crossfades in from 4.5 s over 1 s, so it is already at weight 0.5 at the former pause frame 150; Survey/Run weights are 0.467/0.533 by frame 151. The camera is closer to make the gait legible. Frame-to-frame image-change means at frames 149–152 are now 3.51, 3.45, 3.78 and 3.63 (previous framing/timing: 0.021, 0.020, 0.101 and 0.118). The rebuilt 240-frame output passes the five-frame random-access determinism check. | [MP4 on Drive](https://drive.google.com/file/d/1X7-dvenlmS7qC5lsyCMpqAqvUVslqR6O/view?usp=drivesdk), [transition contact sheet](https://drive.google.com/file/d/1ion-6Y1RfKl1Wgk0u6-8N55_GkWiHA90/view?usp=drivesdk), [local MP4](../out/threepp_certification/T07-BLEND/T07_clip_blend_1280x720p30.mp4), [run log](../out/threepp_certification/T07-BLEND/run.log), source: `render/spike/clip_sequence_demo.cpp` | Full playback and professional-quality art direction remain outside this technical clip-sequence check. |
| T07-HUMANOID-SHADOW | Humanoid skinned character with shadow | PARTIAL (technical gates passed; visual judgement pending) | Pinned Khronos CesiumMan (CC BY 4.0, attribution in fixtures README) through `GLTFLoader → MotionSceneBridge` onto a shadow-receiving floor with a shadow-casting directional light. Follow-up visual diagnosis (2026-10-17): a direct `GLTFLoader + AnimationMixer` render reproduces the same deformation and diagnostic metrics; GLB joint indices are within [0,18] for 19 joints and every vertex's four weights sum to 1 within 1e-6. The waist “stitch” was caused by the frontal camera compressing near/far limbs and by the horizon crossing the torso; a side-on camera removes the false seam. A separate export defect caused the MP4 to appear vertically mirrored/cropped: the encoder vertically blitted into the same default framebuffer, which is undefined when source and destination overlap. It now flips into a dedicated RGBA framebuffer before the GL PBO → CUDA → NVENC readback; decoded MP4 frame 75 visually matches the upright diagnostic PNG. Shadow/off comparison identifies the dark floor shape as the character's cast shadow; raising the key angle and increasing ambient fill shortens/lightens it. Gates: bone matrix delta 58.494, shadow pixels at frames 0/75/149 (28,253/31,431/28,015), subject occupies 72.13% of frame height, random-access and shadow-toggle determinism pass. Local output: `ChrononMotion3D/out/threepp_certification/T07-HUMANOID-SHADOW/` (`T07_humanoid_shadow_1080p30.mp4`, H.264 1920×1080 30 fps 150 frames, ffprobe-verified). Source: `render/spike/humanoid_shadow_certification.cpp`. | [MP4 on Drive](https://drive.google.com/file/d/1V2Th24XM3y_Nj5uF3o47IYgBwa9yadZh/view?usp=drivesdk), [frame 75 on Drive](https://drive.google.com/file/d/1e_0bBJSoXBnUTV7qnggT9eFMdKzcDKwN/view?usp=drivesdk), local MP4 and metadata in the output folder above | Review full playback and material/shadow quality before aesthetic benchmarking. |
| T08 | Morph targets | PARTIAL (GPU encode verified) | Khronos AnimatedMorphCube `Square` clip samples through the Chronon timeline; bridge preserves weights and GLTFLoader enables morph shader variants. NVIDIA RTX A4000; GL PBO → CUDA → NVENC submitted and muxed 150/150 frames; morph weight delta 1.0, 625,018 pixels change between frames 0/75, subject occupies 63.98% of frame height, frame 77 matches. Diagnostic snapshots use a separate 151-readback pass. | [MP4 su Drive](https://drive.google.com/file/d/1YmGH0IZ4aEvt6OkMYbaegke9BpOU33VV/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T08/T08_animation_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T08/metadata.json), [scene](../out/threepp_certification/nvidia/T08/scene.json) | Single morph-only cube and one clip; mixed skin+morph, broader target validation, and a more expressive authored asset remain open. |
| T09 | Camera & depth | PARTIAL (Linux full-GPU encode verified) | `camera_certification.cpp` renders near/mid/far objects through `MotionSceneBridge`; depth ordering, per-object masks, orbit/dolly, and random-access frame 77 pass. NVIDIA GL PBO → CUDA → NVENC, 150/150 frames; diagnostics use a separate readback pass. | [MP4 su Drive](https://drive.google.com/file/d/1ZpreEXDQ-rKMOaq0J27d7b7VpjPaxtwN/view?usp=drivesdk), local `out/threepp_certification/T09/` | No trusted projected-camera/clipping golden or production composition yet. |
| T10 | Depth of field | PARTIAL (GPU encode verified) | Pinned Fox, Avocado and CesiumMilkTruck GLBs render through `GLTFLoader → MotionSceneBridge → EffectComposer → BokehPass`. NVIDIA RTX A4000; GL PBO → CUDA → NVENC submitted/muxed 150/150 frames. Focus follows subject depth, frame 77 is deterministic; diagnostic snapshots use a separate 151-readback pass. | [MP4 su Drive](https://drive.google.com/file/d/1Sg9vcu_S9o6klzMLY3y12rvlzh2QPDJu/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T10/T10_dof_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T10/metadata.json), [scene](../out/threepp_certification/nvidia/T10/scene.json), [encoded frame 75](../out/threepp_certification/nvidia/T10/frame-75.png) | Fix (2026-10-17): the BokehPass blur was saturated for the whole 1.5-unit subject spacing (aperture 0.06 with maxblur 0.025 clamps beyond ~0.42 units), so out-of-focus subjects looked equally blurred. Aperture 0.008 / maxblur 0.02 restores a depth ramp. Measured ROI sharpness at the focused subject vs. the other two focus settings: Fox 3.73 vs 2.38/1.94 (was 2.27 vs 1.77/1.76), Avocado 2.73 vs 1.16/1.18 (was 1.72 vs 1.12/1.12), Truck 6.47 vs 2.81/3.04 (was 5.20 vs 2.82/2.82). Presentation quality needs larger subjects and full playback review; Chronon3D composite integration remains open. |
| T11 | Post-processing | PARTIAL (GPU encode verified; integration/quality pending) | `MotionScene → MotionSceneBridge → NVIDIA GLRenderer → EffectComposer → UnrealBloomPass → GL PBO → CUDA → NVENC`; 150 frames at 1920×1080/30, bloom toggle changes 662,469 pixels, random-access frame 77 is identical. NVIDIA RTX A4000; 150/150 frames submitted and muxed. 153 CPU framebuffer reads are limited to a separate diagnostics pass. | [MP4 su Drive](https://drive.google.com/file/d/1rm-W4ENZVgi-1J-66g5N3d52xJk42LYQ/view?usp=drivesdk), [locale](../out/threepp_certification/nvidia/T11/T11_bloom_1080p30.mp4), [metadata](../out/threepp_certification/nvidia/T11/metadata.json), [frame 75](../out/threepp_certification/nvidia/T11/frame-75.png) | Basic bloom demonstration only; integrate post-processing in Chronon3D production composition and review visual quality. |
| T12 | Documentary 3D composition | PARTIAL (Linux full-GPU encode verified) | `glb_certification.cpp` T12 mode imports the pinned CesiumMilkTruck via GLTFLoader, stages it with Motion-authored evidence folders/photos/files, and animates a CameraRig. NVIDIA RTX A4000 EGL → GL PBO → CUDA → NVENC, 150/150 packets, zero encode-path CPU framebuffer reads; random-access frame 77 matches. | [MP4 su Drive](https://drive.google.com/file/d/1U86KHZ7D72uzWzg7hPCfl1WeihkXA6T0/view?usp=drivesdk), local [MP4](../out/threepp_certification/T12/T12_documentary_composition_1080p30.mp4), [scene](../out/threepp_certification/T12/scene.json), [metadata](../out/threepp_certification/T12/metadata.json), [frame 75](../out/threepp_certification/T12/frame-75.png) | Primitive evidence props and lighting are a technical layout proof only; improve art direction, texture detail, full-sequence review and integrate with richer Chronon graphics. |
| T13 | 3D + Chronon compositing | PASS (opaque full-GPU threepp→Chronon); PASS (Chronon Vulkan alpha-PNG composite); PARTIAL (animated alpha video) | Current T06 NVIDIA GL PBO → CUDA → NVENC output feeds Chronon NVDEC → Vulkan full graph → NVENC with `require_gpu_native`. Log confirms native surface input, 150 packets, zero conversion/staging-copy bytes; strict graph validation and the 150-frame 1920×1080 overlay/vehicle ROI gate pass. Vehicle ROI MAE mean is 0.85 RGB; all headline/subhead/marker ROIs exceed the per-frame minimum. Alpha image lane: six-layer Chronon composition at 1920×1080/30, native GPU handles, zero conversion/staging-copy bytes. | [Opaque full-GPU MP4 su Drive](https://drive.google.com/file/d/13g49B_K3SxDjW4BQRcK6FeN7-jb8rmW2/view?usp=drivesdk), [local MP4](../out/threepp_certification/T13/T13_full_gpu_NVIDIA_NVDEC_Vulkan_NVENC_certified.mp4), [run log](../out/threepp_certification/T13/T13_full_gpu_NVIDIA_NVDEC_Vulkan_NVENC_certified.log), [150-frame validation](../out/threepp_certification/T13/full-gpu-hardware-validation-certified.json), [alpha composition MP4](https://drive.google.com/file/d/1-ba876wrbR-WLFfuBNhITDi0sXnurimt/view?usp=drivesdk), [alpha scene](../out/threepp_certification/T13-alpha/scene-alpha-chronon-composition.json), [alpha GPU log](../out/threepp_certification/T13-alpha/T13_alpha_Chronon_composite_full_gpu_1080p30.log) | Continuous GPU-native alpha video decode is still missing. |
| T14 | Headless GPU rendering | PASS | 150-frame 1920×1080/30 turntable through NVIDIA EGL pbuffer → threepp GL → pixel-pack buffer → same-device CUDA frame → NVENC. 150/150 frames submitted and packets muxed; `ffprobe` confirms H.264, 1920×1080, 30 fps, 150 frames. Random-access frame 77 matches exactly. Video encoding never reads a framebuffer into CPU memory; PNG and numeric diagnostics run in a separate second render pass. | [MP4 su Drive](https://drive.google.com/file/d/1n5Oh2qedEY2aq1pHrSmqZSzOTKJREvJM/view?usp=drivesdk), [MP4 locale](../out/threepp_certification/T14-gl-cuda/T14_gltf_pbr_1080p30.mp4), [frame 0](../out/threepp_certification/T14-gl-cuda/frame-0.png), [frame 75](../out/threepp_certification/T14-gl-cuda/frame-75.png), [frame 149](../out/threepp_certification/T14-gl-cuda/frame-149.png), [metadata](../out/threepp_certification/T14-gl-cuda/metadata.json) | NVIDIA RTX A4000 EGL context is display-free. GPU handoff makes one device-to-device copy per frame. VRAM and utilization are not attributed per process. |
| T15 | Production integration | PARTIAL (strict full-GPU path, receipt lane and repeat stability verified) | The T12 threepp render feeds a Chronon3D RenderPlan with Chronon text/shape overlays: NVDEC → Vulkan full graph → NVENC; required native mode, `host_frame=false`, native surface encoder feed, zero conversion/staging bytes and 150/150 packets. Receipt now records `backend=vulkan`, `render_lane=full_graph_vulkan`, `lane_reason=required_gpu_native_full_graph`. Three independent renders decode to identical hashes for all 150 frames. GPU sampling: 26 samples, GPU peak 12%, encoder peak 42%, device memory peak 2,597 MiB. | [MP4 su Drive](https://drive.google.com/file/d/1-7e3YTzRFmqZwWExaQpz3ms-550OoEyE/view?usp=drivesdk), local [MP4](../out/threepp_certification/T15/T15_documentary_chronon_full_gpu_sampled.mp4), [scene plan](../out/threepp_certification/T15/scene-full-gpu.json), [latest strict run log](../out/threepp_certification/T15/sampled-render-lane-fix.log), [validation](../out/threepp_certification/T15/full-gpu-validation.json), [repeat stability](../out/threepp_certification/T15/stability-validation.json), [GPU samples](../out/threepp_certification/T15/gpu-samples.csv), [frame 75](../out/threepp_certification/T15/frame-75.png), [process/leak audit](../out/threepp_certification/T15/process-leak-audit.json) | T12 is a pre-rendered source clip, not direct GLB ingestion inside a single Chronon job. Per-process CPU/RAM and repeated-job leak evidence is now collected: two strict full-GPU reruns report per-process `rss_peak_mb`/`cpu_user_ms`/`cpu_sys_ms` from the timing sidecar's `job.process` section (1123→1122 MB peak, delta −1 MB, both runs `full_graph_vulkan`, 150/150 frames); see the audit JSON above. |

T01, T03, T04, T05, T06, T07, T08, T09, T10, T11 and T14 now have GL PBO → CUDA → NVENC
video paths with no CPU framebuffer handoff during encoding; CPU readbacks are
isolated to separate diagnostic passes. T13's strict Chronon pipeline also
passes with native GPU surfaces. T12 and T15 now have tracked proof scenes and
full-GPU videos; professional scene quality, in-job GLB ingestion and animated
GPU-native alpha video remain open. Render and encode certification is Linux-only
and requires a native GPU path; a software-only result cannot satisfy a test.

## T13 run result

The initial T13 attempt used a software readback/encode lane and is retained
here as historical evidence only. The current opaque-source route renders the
GLB with NVIDIA EGL/OpenGL through a CUDA-registered PBO into NVENC, then uses
NVDEC, Chronon3D's Vulkan render graph and NVENC for the final output.
Chronon3D's strict `require_gpu_native` run processed 150/150 native GPU surfaces at
1920×1080/30 fps with zero CPU/GPU readbacks, software encode frames,
fallback nodes, or execution downgrades in the Chronon video graph. Vulkan
allocation stayed at 982,089,728 bytes from frames 25–150; whole-device peak
VRAM was 3,036 MB. All 150 decoded frames passed dimensions and overlay ROI
checks. The refreshed run uses the newly generated T06 GPU-encode source and
passes the 150-frame source/overlay ROI verifier. Mean vehicle ROI MAE is 0.85
RGB. The threepp encode pass and full Chronon decode/composite/encode graph
contain no CPU framebuffer handoff.

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

The NVIDIA EGL backend now creates a display-free pbuffer on the RTX A4000. The
encoded pass renders 150 frames at 1920×1080/30 and hands each resolved GL frame
through a pixel-pack buffer registered with CUDA, then copies device-to-device
into an FFmpeg CUDA frame for NVENC. CUDA GL device discovery binds the GL
adapter and encoder to the same GPU. The runner records 150 frames submitted,
150 packets muxed, zero CPU framebuffer readbacks on the encode path, and one
GPU device copy per encoded frame. A second deterministic render pass produces
the PNGs and numeric checks; frame 77 matches byte-for-byte. `ffprobe` confirms
H.264, 1920×1080, 30 fps and 150 frames. Metadata, frames and MP4 are retained
under `out/threepp_certification/T14-gl-cuda/`.

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
peak 17%, whole-device VRAM peak 1,555 MiB). Encoding uses GL PBO → CUDA →
NVENC; diagnostic readbacks are separate. T08 renders the Khronos
`AnimatedMorphCube` `Square` clip on NVIDIA RTX A4000/NVENC. Morph-weight delta
is 1.0, 626,152 pixels change from frame 0 to 75, subject height is 64.07%, and
frame 77 matches sequential rendering. A test-only orange material tint and
three-quarter camera improve silhouette readability; scene.json records these
settings. Twenty-five telemetry samples show GPU peak 5%, encoder peak 17%, and
whole-device VRAM peak 1,545 MiB. Encoding uses GL PBO → CUDA → NVENC;
diagnostic readbacks occur in a separate pass. This is still a single morph-only fixture, not a mixed skin+morph or
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
matched on random access. The rebuilt T04 MP4 differs from T03. It now renders
on the NVIDIA RTX A4000 and encodes through GL PBO → CUDA → NVENC, with 150/150
frames submitted and muxed. The independent brightness-centroid and temporal
difference checks pass; 151 framebuffer readbacks occur only in the separate
diagnostic pass. This is a basic moving-light proof and does not certify
shadows or professional lighting quality.

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
86,195 pixels when disabled at frame 75. The material values are checked and
frame 77 matches random-access rendering. Its video uses NVIDIA GL PBO → CUDA
→ NVENC with 150/150 frames submitted and muxed; 152 CPU readbacks are confined
to a separate diagnostics pass. The test stays PARTIAL until it has a trusted
reference render. Chronon3D's inline RenderPlan material remains a compact
Phong-like approximation rather than a full PBR path.

## T06 run result

The pinned Khronos `CesiumMilkTruck.glb` passes through `GLTFLoader → MotionScene
asset layer → MotionSceneBridge → GLRenderer`. Its five meshes, hierarchy
signature, and source/clone bounds match; all five meshes have finite,
count-matched UVs; five standard materials and three base-color maps survive
cloning. A Chronon-authored turntable and `CameraRig` dolly produced 150 frames
at 1920×1080, 30 fps and 5.0 s. Its NVIDIA RTX A4000 encode uses GL PBO → CUDA
→ NVENC; all 150 frames were submitted and muxed without CPU readback in the
encode pass. Coverage was 151,757–286,575 pixels, first/last
frames differed by 59,144 pixels, and random-access frame 77 matched
byte-for-byte. The sampled frame was visually inspected. This remains PARTIAL
because texture/UV/material response has no trusted numerical golden render.

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
VRAM peak 1,938 MiB. The encoded pass transfers GL PBO → CUDA → NVENC without
CPU framebuffer readback; diagnostic image reads are a separate pass. The actual frame confirms focus
changes, while the subjects remain small; production composition and visual
refinement are open. See `out/threepp_certification/nvidia/T10/` for the scene,
MP4, snapshots, metadata, telemetry, and run log.

## T11 run result

The initial llvmpipe result remains under `out/threepp_certification/T11/` as
historical evidence. The replacement in
`out/threepp_certification/nvidia/T11/` renders on NVIDIA RTX A4000 and sends
GL pixels through a pixel-pack buffer and same-device CUDA transfer into
NVENC. It produces 150 frames at 1920×1080/30 fps. Coverage is
816,972–936,852 pixels, bloom on/off changes 662,469 pixels, and random-access
frame 77 is byte-identical to sequential rendering. The encoded pass has no
CPU framebuffer readback; 153 CPU reads occur in the separate diagnostics pass.
Chronon3D composition remains outstanding.

## Current provenance and license notes

The restored OpenGL post-processing and animation runtime sources are from the
pinned threepp revision `e2b165ece3904012d97a08029e0bd6cd5252892a`; they are MIT
licensed under the existing threepp/three.js notices. The render target links
the sources without adding a dependency or changing the renderer-neutral core.

External Khronos fixtures are pinned to asset repository commit
`edc7c9e67c639d230715049ee31f9a96a6babbbe`; per-model attribution and hashes
are recorded beside the binaries.

## Remaining integration gaps

1. Complete visual/numeric PBR and GLB texture baselines: T02 now verifies
   six atlas regions and real GPU sampler/mipmap response, but a trusted UV
   orientation golden, cross-driver tolerance, normal-map baseline and readable
   material scale remain open.
2. Extend deterministic clip sampling to nested precomps and blended animation
   clips; the T07 single-clip close-up now visibly demonstrates the Fox run.
3. Tune T05 shadow bias/softness and integrate the hardware-verified T10 focus
   rack in a larger Chronon3D composition.
4. Keep the all-frame T13 software/Vulkan ROI comparison as a regression gate;
   prove overlay provenance and transparent media handoff end-to-end in
   Chronon3D. The native software decoder alpha conversion now has a passing
   YUVA444 regression, but the existing opaque H.264 demo does not exercise it.
5. **T15 direct GLB integration is still open.** T06/T12 load GLB through
   threepp `GLTFLoader → MotionSceneBridge` and render it on NVIDIA GL; T15
   currently hands Chronon3D a pre-encoded clip (`NVDEC → Vulkan → NVENC`).
   Chronon3D's RenderPlan mesh decoder requires inline geometry and rejects
   mesh asset/source references. Its separate `MeshLoader` prepares static
   GLBs, but the mesh consumer located in `software_mesh_processor.cpp` is a
   CPU renderer; no Vulkan mesh executor was found in the current path. Merely
   connecting `MeshLoader` to RenderPlan would therefore miss the full-GPU
   requirement. threepp's pinned Vulkan renderer is available upstream but is
   excluded from this render target and is swapchain-only: its API explicitly
   does not support offscreen render targets. Keep the existing EGL headless
   GLTFLoader/GL renderer for 3D, and connect its CUDA-registered GL PBO to
   Chronon3D's exported Vulkan surface using the existing CUDA/Vulkan memory
   and semaphore bridge. This would remove the intermediate H.264/NVDEC cycle
   without a second GLB parser or per-frame CPU readback. The remaining work is
   to host the bridge in one render job, preserve exact frame/time authority in
   ChrononMotion, and prove the full 1920×1080/30 render and encode stay
   GPU-resident. Collect process-scoped CPU/RAM and repeated-job leak evidence
   alongside that integration. Linux is the sole target; software-only results
   cannot pass certification.

   Update 2026-10-17: the CUDA→Vulkan half of that handoff now has GPU proof.
   `tools/hwlab/cuda_vulkan_rgba8_copy_probe.cpp` (Chronon3d, built from
   `e7e970d17`) copies a device-resident pattern through
   `CudaVulkanSurfaceBridge::copy_rgba8_from_cuda` into an exported Vulkan
   surface — a real Vulkan queue submit signals the vulkan-to-CUDA semaphore
   before each copy wait, the source uses a padded pitch, and the mapped array
   readback reproduces 16384/16384 bytes exactly, twice (second copy is a row
   shift, so position is proven too). Short source pitch and a float surface
   are rejected instead of corrupting data. Ownership protocol pinned: the
   CUDA side must import an unsignaled binary semaphore, and every CUDA wait
   consumes exactly one prior Vulkan signal. Evidence:
   `Chronon3d/docs/evidence/rgba8-cuda-vulkan-copy-probe-e7e970d17.{log,json}`
   (5/5 runs exit 0 on NVIDIA RTX A4000). Hosting the full render job around
   this primitive remains open.
