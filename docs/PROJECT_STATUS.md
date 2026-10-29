# ChrononMotion3D ticket status

Updated 2026-10-29 from local source and Release test evidence.

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
  Remaining follow-up is C3D strict-native rendering and GPU parity. The 2026-10-29 focused Release run passed all six targets listed
  in the ticket. A refreshed producer fixture and software raw-sink probe show
  a textured, alpha-cutout mesh moving across 31 frames (31 distinct raw frame
  hashes; 1,642 opaque pixels per sample). A current-source Release rebuild
  fixed the YUV420P/NV12 host staging mismatch in H.264/NVENC: the MP4 decodes
  to 31 distinct frames with RGB RMSE 0.7673 against the software reference
  composited over black. Strict-native Vulkan drawing, GPU-render parity, and
  clean-SHA CI remain unverified.
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
  bridge implemented; local visible CLI output remains.
- No remaining local PBR V1 implementation item; embedded images and
  unsupported sampler modes remain intentionally rejected.

GPU and CI certification are external gates and are not reported as locally
verified in this status file.
