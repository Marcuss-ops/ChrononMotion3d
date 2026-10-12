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
- [ ] Build and run the focused adapter regression. Attempts to build this official adapter target exceeded repeated 10-minute limits because its link graph pulls a large Chronon3D object-library set; preserve the partial build outputs and report this as unverified rather than passing.
- [x] Record unreachable proposal items: text-range-to-glyph-rectangle geometry has no contract in the existing text/layout interface; reference media and output paths are absent; transition handling is blocked by validator/consumer mismatch.

## Baseline audit and environment

- Existing adapter: `RenderPlanLowering.cpp`, `RenderPlanEmission.cpp`, and `RenderPlanEmissionValidation.cpp`.
- Existing V3 text semantic path uses `LayerPlan::text_spans` / `TextAnimatorPlan`; Chronon3D retains shaping ownership.
- The typed RenderPlan, V3 extension declaration, decoder and compiler contain mask, matte, and effect-parameter structures.
- The Chronon3D submodule has unrelated pre-existing working-tree modifications (typed RenderPlan/schema/compiler/decoder/validator files). Preserve them; do not reset or rewrite the submodule.
- ChrononMotion3D had no configured build directory when verification began. The standalone Motion+adapter target compiles, but linking the added canonical validator/decoder calls there fails because the adapter library does not include Chronon3D runtime objects. The corresponding real integration regression now lives in Chronon3D's existing offline adapter suite.
- At the operator's direction, `-j2` was used for Chronon3D's RenderPlan target. It completed all 37 object compilations. The broader official adapter executable link still exceeded the 10-minute command limit; no target artifact was produced.
- Chronon3D's pre-existing motion-contract bridge CTest passed (1/1); it is adjacent evidence only, not execution of the new regression.

## Completion log

- Capability and dirty-submodule audit completed.
- Removed duplicate `effect_param_tracks` serialization: one key is emitted via the shared canonical writer.
- Added the bridge regression to Chronon3D's existing offline adapter test suite; it exercises deterministic output, canonical schema validation and typed decoding, both valid/missing mattes, animated mask, effect-track transport, malformed-mask rejection, V2 rejection, and unsupported effect parameters.
- Test status: the legacy Chronon3D motion-contract bridge suite passed 1/1. The focused Chronon3D offline adapter target is configured and its RenderPlan object files compiled, but linking its broad object-library dependency graph exceeded repeated 10-minute limits; the new canonical validator/decoder regression remains unexecuted.
- Final standalone verification: `cmake --build .tmp/build-bridge --target chrononmotion_authoring -j1` succeeded and `.tmp/build-bridge/chrononmotion_authoring` passed all 292 checks. This target verifies Motion-side validation/emission only; canonical Chronon3D validation/decoding remains dependent on the unlinked Chronon3D adapter regression.
- ChrononMotion3D `git diff --check` and Chronon3D submodule `git diff --check` passed. The standalone test covers the V3 emission regression; direct Chronon3D validator/decoder calls remain only in the Chronon3D-linked offline adapter suite.
- CTest in the standalone bridge build is not a valid substitute: its registered rig executables are not built in this focused configuration, so verification was performed by running the built `chrononmotion_authoring` executable directly.
- Existing unrelated Chronon3D submodule modifications were preserved; no cleanup/reset was performed.
