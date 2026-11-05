# ChrononMotion3D ticket status

Updated 2026-11-01 from local source and recorded Release test evidence.

## Camera checkpoint (2026-10-31)

- The shared camera capability contract is integrated at Motion's RenderPlan
  lowering boundary (`a67934e`); unsupported iris/highlight optics fail with a
  typed diagnostic. Camera and layer transforms are baked against the shared
  shutter calendar, and explicit camera samples must cover that calendar
  (`24758af`). Camera cloning now preserves projection state for perspective
  and orthographic cameras (`e2d0213`), with upstream regression coverage.
- The associated Chronon3D camera-closure ticket remains PARTIAL: local
  implementation checkpoints landed, while full end-to-end camera/video
  parity and same-SHA certification remain open. See
  `../../Chronon3d/docs/tickets/archive/TICKET-CAMERA-DEFINITIVE-CLOSURE.md`.

## Complete feature tickets

- `TICKET-LIGHT-RIG-V1`: archived at
  `docs/tickets/archive/TICKET-LIGHT-RIG-V1.md`. LightRig V1, three presets,
  deterministic serialization, V3 light-layer bake, producer validation, and
  showcase case are implemented. The checked-in 63-case manifest matches two
  consecutive dumps. Focused tests `chrononmotion_light_rig` and
  `chrononmotion_authoring` pass.

## Partial feature tickets

- `TICKET-GLTF-ANIMATED-V1`: motion-only JSON import, deterministic skin /
  POSITION/NORMAL morph bake, CUBICSPLINE TRS/quaternion/morph-weight
  interpolation, and the named `GltfMotionClipLibrary` runtime authority are
  implemented. Tangent, normal-deformation, and clip-selection fixtures pass
  `chrononmotion_rig_pipeline`; `chrononmotion_authoring` and
  `chrononmotion_gltf` pass. The fixed-topology positions/optional-normals V3
  contract carries static UV, texture, material, and alpha state across frames.
  The motion-only reader now reports unsupported JSON/root fields with the
  public `glTF: ` diagnostic prefix and rejects disabled/unsupported imports
  before buffer resolution; the rebuilt focused test passes 47/47 checks.
  The clean pushed C3D source now passes strict-native Vulkan/NVENC rendering
  of the textured animated fixture (31/31 frames) and local GPU parity. The
  2026-10-29 focused Release run passed all six targets listed
  in the ticket. A refreshed producer fixture and software raw-sink probe show
  a textured, alpha-cutout mesh moving across 31 frames (31 distinct raw frame
  hashes; 1,642 opaque pixels per sample). A current-source Release rebuild
  fixed the YUV420P/NV12 host staging mismatch in H.264/NVENC: the MP4 decodes
  to 31 distinct frames with RGB RMSE 0.7673 against the software reference
  composited over black. The latest clean-source strict-native render matches
  the prior 31-frame MP4 SHA and meets the CAM-04 foreground parity gate.
  Clean-SHA CI and broader production-material coverage remain unverified.
- `TICKET-HDRI-ENV-V1`: bounded Radiance RGBE input, cubemap+mips, Lambert SH,
  deterministic `HDRIENV1` bytes, malformed/budget tests, and showcase golden
  are implemented. The byte layout is agreed in the Chronon3D IBL ticket.
  V3 RenderPlan emission, path/range validation, Chronon3D decoding, and
  focused tests are implemented. Chronon3D validates the asset, accepts the
  plan in compilation, and evaluates the diffuse SH probe in software shading.
  The refreshed software raw-sink probe confirms mounted asset resolution and
  visible diffuse-SH contribution: removing the environment changes all 20,000
  opaque mesh pixels in the fixture control. The stale CLI's encoded output is
  diagnostic, not accepted pixel proof. Software specular mip lookup passes
  the Chronon3D renderer core test. The
  `attachHdriEnvironmentToRenderPlan` helper copies the decoded probe's
  reference, intensity, rotation and SH together.
  The producer-side quantitative specular golden is committed:
  `tests/hdri_specular_golden.json` pins the payload digest, the 27 SH
  coefficients, cubemap radiance probes on all six faces and the mip tail, and
  the mean radiance of every mip level; the test also verifies the SH against
  the closed form of the analytic linear-gradient fixture, so the golden is
  checked for correctness, not only for stability. Visible CLI rendering
  against it remains open.
- `TICKET-PBR-MATERIALS-V1`: static `loadGltf` imports metallic-roughness
  factors, base/emissive color, alpha and double-sided state into
  `PbrMaterial`; an external base-color image URI passes through
  `meshAttachmentFromGltf` into V3 base-color and inline metallic-roughness
  slots. Malformed factors, unsafe image paths and unsupported samplers fail
  closed. Motion emits PBR and alpha-cutoff values to RenderPlan V3, and
  Chronon3D validates, fingerprints and samples base color plus metallic-
  roughness G/B channels in software shading for prepared and inline meshes.
- The current showcase manifest has 63 cases and matches two independent
  `--dump-golden` runs; the targeted Release tests for rig pipeline, HDRI,
  LightRig, authoring bridge, glTF, and all showcase cases pass 6/6.

## Open feature tickets

- `TICKET-HDRI-ENV-V1`: reader and deterministic producer/consumer schema
  bridge implemented, and the quantitative specular golden is now closed
  locally (`tests/hdri_specular_golden.json`: SH, cubemap radiance per texel
  and per mip, mean energy per level, verified against the closed form of an
  analytic fixture by `chrononmotion_hdri`, 244 checks). Remaining: the
  visible CLI round-trip rendered against that golden and GPU parity.
- No remaining local PBR V1 implementation item; embedded images and
  unsupported sampler modes remain intentionally rejected.

## Camera pipeline — sensor gate, default FOV, DoF admission (2026-11-01)

- `CameraRig` authors a physical sensor gate: `setSensorHeight` accepts
  [1, 100] mm and rejects non-finite values; the height reaches both
  `camera.sensor_height_mm` and every `camera_samples[*].camera` in the
  RenderPlan. Pinned in `tests/motion_authoring_renderplan.cpp` (setter range
  and non-finite rejection, both gate endpoints, default FOV).
- The default vertical FOV is **50°**, not 60°: `CameraPose::fov`,
  `CameraRig::fov_` and the `CameraRig` constructor parameter were split
  between the two values and are now 50 everywhere. Showcase goldens do not
  move because every case pins its own FOV.
- DoF admission follows the shared shaped-bokeh predicate, which now fails
  closed on `iris_blades` only. `dof_shape_unsupported()` accepts
  `dof.highlight_gain` because both the software and the Vulkan DOF kernels
  implement highlight weighting, so Motion calls that same predicate in
  `lowerCameraPose` and in `emissionCameraProblem` and carries the authored
  gain through; `iris_blades` still fails in authoring. `applyFocusSequence`
  applies the same rule (gain passes, blades reject), and the FocusSequence
  default keeps the gain unauthored instead of arming it. The rule keeps a
  single seat in the shared header.
- Local evidence: `cmake --build build/release-fast -j4` clean, then CTest
  **66/66 PASS** (66 registered in this configuration), `chrononmotion_authoring`
  381 checks / 0 failures.

## Resolved frame plan — ADR-CAMERA-PIPELINE-OWNERSHIP gate 1 (2026-11-01)

- The versioned resolved frame plan exists as a value type:
  `chrononmotion::plan::ResolvedFramePlan`, produced by one function
  (`resolveFramePlan`) from a scene and a camera rig. It carries canvas, a
  rational frame rate, the single timeline, the shutter window in sub-frame
  units, one camera sample per frame and one world matrix per layer per frame.
  No pointer to a `MotionScene`, no track, no callback and no policy crosses it.
- Calendar: `frame` (may be negative) plus integer `sub_frame_units` in
  millionths of a frame; `seconds` is derived and verified against
  `(frame + units/1e6) * den/num`. Evaluation uses the same `(frame, fraction)`
  pair, so a whole-frame plan is bit-for-bit `scene.evaluate(N)`.
- Contract: `docs/RESOLVED_FRAME_PLAN.md`,
  `contracts/chrononmotion.resolved-frame-plan.v1.json`, golden
  `tests/frame_plan_golden.json` (regenerated with `--dump-golden`).
- Fail-closed verifier with 25 typed rejection codes; composed opacity outside
  [0, 1] is a rejection, not a clamp, and a layer past its lifetime stays in the
  plan as `visible: false`. The producer holds itself to the consumer's rules
  and the JSON writer refuses an invalid plan.
- Local evidence: `chrononmotion_frame_plan` **179 checks / 0 failures**,
  including bit-exact frame-grid comparison against `scene.evaluate(N)`,
  row-major matrix layout verified independently, shutter endpoints, byte-level
  determinism, a tampered-golden check and 29 negative cases.
- Gates 2-6 (thin C3D consumer, alpha/depth compositing with DoF, GPU shutter,
  product acceptance at 1080p with same-SHA CI, legacy removal) remain open and
  are not claimed here.

GPU and CI certification are external gates and are not reported as locally
verified in this status file.
