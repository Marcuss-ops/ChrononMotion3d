# RenderPlan + anchored-highlight acceptance checklist

## Goal and bounded scope

Complete the existing ChrononMotion3D → Chronon3D V3 producer bridge for layer masks, track mattes, and effect parameter tracks, and certify it using Chronon3D's canonical validator/decoder. Keep unsupported or consumer-incompatible data fail-closed. This does not create a parallel renderer or approximate text geometry.

The two reference videos are not deliverable artifacts in this checkout: no source footage/assets, named output paths, or visual goldens were supplied. No full recipe/composer or rendered clip is claimed here.

## Acceptance criteria

- [x] Inspect both engines before adding primitives; preserve ownership (Chronon3D owns text shaping/layout/rendering; Motion owns time/space).
- [x] Confirm V3 text spans, semantic selectors, and text animators are existing contracts; do not estimate glyph bounds from character counts.
- [x] Emit legacy path mask and ordered masks (including mask parameter tracks), track matte, and effect parameter tracks once, preserving typed values and deterministic array/key order.
- [x] Reject malformed mask/effect-track payloads and V2-incompatible fields before serialization.
- [x] Add a focused Chronon3D adapter regression for emitted V3 JSON, canonical validation/decoding, resolved and missing matte references, determinism, malformed masks, unsupported effect parameters, and V2 rejection.
- [x] Keep transitions fail-closed: this checkout's V3 declaration is not aligned with the Chronon3D validator/consumer projection, which removes transition fields. Do not emit plans that the downstream consumer cannot certify.
- [x] Rebuild and run the focused V3 bridge regression through Chronon3D's canonical build and test executable. The fresh `chronon3d_motion_v3_bridge_tests` build completed and the test passed (1/1 CTest; 22 assertions). This certifies bridge emission, canonical V3 validation and typed decoding; it does not certify pixels.
- [x] Record unreachable proposal items: text-range-to-glyph-rectangle geometry has no contract in the existing text/layout interface; reference media and output paths are absent; transition handling is blocked by validator/consumer mismatch.
- [x] Rig-to-render bridge acceptance: copy one baked `RigFrameGeometry` into a V3 inline mesh only when topology matches; retain UV/material, regenerate deformed normals, and reject invalid or over-budget values. A separate RenderPlan per frame is possible for offline frame rendering; schema V3 has no animated inline-mesh vertex track.
- [x] Declare Alpha/texture boundary: current `InlineMeshPlan` carries positions/normals/UV/material only and ChrononMotion mesh attachment carries no texture image or alpha surface. This bridge does not claim textured subject compositing, alpha-mask deformation, automatic segmentation, or temporal tracking. Existing video/image layer masks are separate renderer operations, not bound to RigProgram geometry.
- [ ] Full animated textured/alpha subject render remains blocked on a coordinated Chronon3D contract/runtime change: textured inline mesh payload or deformable raster surface, vertex/UV/alpha deformation semantics, and a per-frame geometry/skin animation contract. Do not claim it complete from static mesh-frame output.
- [x] Software rasterizer canary using the exact decoded vertex/UV/index payload emitted by the Motion bridge: author the inline mesh through Chronon3D's public scene builder and assert framebuffer coverage. The passing canary setup produced 4096 covered pixels at 64x64. This verifies rasterization of the payload; it does not certify RenderPlan scene materialization or the CLI's still-PNG route.
- [ ] RenderPlan-to-scene pixel proof remains blocked: direct invocation of the configured RenderPlan scene materializer with the decoded mesh and both explicit and implicit cameras produced 0 covered pixels at 64x64, while the same decoded mesh payload is visible via the ordinary scene-builder canary. The cause is in the materialization/camera contract and needs focused Chronon3D investigation; do not claim the software canary verifies RenderPlan pixel output.
- [ ] CLI pixel proof for this RenderPlan route remains blocked: `render --plan ... -o still.png` uses the repository's explicitly non-canonical still-image lane (see Chronon3D `TICKET-IMAGE-PATH-PIXEL-TRUTH`); `--video-sink raw` currently fails earlier with `Video RenderJob requires the legacy compiled export view`. The transparent still PNG therefore is not pixel truth and this failure is not evidence of a mesh raster defect.

## Baseline audit and environment

- Existing adapter: `RenderPlanLowering.cpp`, `RenderPlanEmission.cpp`, and `RenderPlanEmissionValidation.cpp`.
- Existing V3 text semantic path uses `LayerPlan::text_spans` / `TextAnimatorPlan`; Chronon3D retains shaping ownership.
- The typed RenderPlan, V3 extension declaration, decoder and compiler contain mask, matte, and effect-parameter structures.
- Chronon3D has unrelated pre-existing working-tree modifications (typed RenderPlan/schema/compiler/decoder/validator files). Preserve them; do not reset or rewrite the submodule. No Chronon3D files were changed for the rig geometry helper; canonical validation/decoding is part of the bridge regression.
- Existing ChrononMotion3D Release adapter and Chronon3D `linux-video-test` build directories were reused. The focused bridge executable was rebuilt from current Motion adapter/test sources via `ninja ... chronon3d_motion_v3_bridge_tests -j24`; its canonical CTest passed 1/1. The shared build required 563 build actions after regeneration; the high-parallel rebuild completed.
- The first direct run exposed that the fixture used unsupported `color.exposure`; it now uses the supported catalog `color.contrast` effect and its animatable `value` parameter.
- The bridge regression source currently exercises canonical serialization/validation/decoding; rigged mesh frame transport is covered by Motion authoring tests. A prior stricter probe of the actual RenderPlan scene materializer failed with 0 covered pixels for explicit perspective, explicit orthographic, and implicit cameras. The available Chronon3D executable is stale and failed its old materializer-pixel assertion (30/31 assertions). The fresh rebuild stalled at step 343/563 and is not claimed as passing behavior. The pixel route remains unresolved.

## Completion log

- Capability and dirty-submodule audit completed.
- Removed duplicate `effect_param_tracks` serialization: one key is emitted via the shared canonical writer.
- Added the bridge regression to Chronon3D's existing offline adapter test suite; it exercises deterministic output, canonical schema validation and typed decoding, both valid/missing mattes, animated mask, effect-track transport, malformed-mask rejection, V2 rejection, and unsupported effect parameters.
- Test status: Motion authoring passed 308/308 checks and `chrononmotion_rig_program` passed 1729/1729 checks; their focused CTest selection passed 2/2. Chronon3D's freshly rebuilt V3 bridge CTest passed 1/1 (22 assertions). The inline-mesh C ABI cases passed 6/6 (75 assertions), and the software mesh rasterizer canary passed 5/5. The RenderPlan scene materializer pixel route remains unresolved (0 covered pixels in prior direct probes); this separate limitation is not hidden by the passing bridge and scene-builder canary.
- CLI acceptance: Chronon3D `render --plan --dry-run` accepted a one-frame deformed-mesh V3 plan. The still-PNG CLI render exited successfully but wrote a fully transparent 64x64 PNG (zero covered pixels); the repo documents this still-image output lane as non-canonical for pixel truth. A raw-sink retry failed before rendering with `Video RenderJob requires the legacy compiled export view`. The existing software inline-mesh renderer canary passes 5/5; no renderer correction is justified by the still-PNG observation alone.
- Final standalone verification: `cmake --build .tmp/build-bridge --target chrononmotion_authoring chrononmotion_rig_program -j2` succeeded; `.tmp/build-bridge/chrononmotion_authoring` passed 308 checks and `chrononmotion_rig_program` passed 1729 checks. These verify Motion-side bake/emission and the static geometry payload; they do not certify the RenderPlan materializer's pixels.
- `git diff --check` passed in both ChrononMotion3D and Chronon3D. The current focused tests were run directly and through CTest as listed above; the full project CTest suite was not run. A broader combined CTest invocation exceeded its 180-second window, so only the focused CTest selections are claimed.
- Existing unrelated Chronon3D submodule modifications were preserved; no cleanup/reset was performed.
