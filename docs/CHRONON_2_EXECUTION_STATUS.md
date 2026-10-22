# Chronon 2.0 — implementation and verification status

Run date: 2026-10-22. This note updates the read-only audit with the executable verification performed for the requested non-specialist-VFX scope.

## Source revisions

| Repository | Revision tested |
|---|---|
| Chronon3d | `08c2ff0b1309f0a65cde9eb5f69c9469dc792395` |
| ChrononMotion3d | `1c8a8671711e84ccf15e2d33b1c17c1f16cb7be5` |
| EffectCraft reference audit | `f37f20ffd07d0f79e163eef1ad703b87db427dc3` |

## Capabilities verified in current Chronon sources

- C3D has `Echo` as an effect, temporal history handling for software and Vulkan paths, and integration tests for composition with other effects.
- C3D has layer `TimeRemap` for speed, reverse, freeze and animated source-frame mapping. Motion blur samples the composition at sub-frame times. These are not optical-flow Timewarp and do not provide arbitrary source-frame access from inside an effect stage.
- C3D has the `FractalNoise` effect and deterministic particle primitives. CM3D owns offline motion/simulation baking and deformation operators; no second simulation engine is warranted for the audited scope.
- Existing ChrononMotion tracks, easing and render-plan contracts are present. The source audit did not demonstrate EffectCraft-compatible roving-key semantics, so no API was added without a precise owner contract and acceptance definition.

## Changes made

Only the CMake boundary-test guard was corrected in ChrononMotion3d. Its scanner removed CMake comments before checking the documented `boundary-guardrail: allowed` exception, making the exception impossible to use for `find_package(fmt)`. It now recognizes the marker before stripping comments. The existing one-line exception documents that `fmt` is needed only by the optional offline RenderPlan adapter, not by the ChrononMotion library.

No effect, renderer, codec or simulation code was copied from EffectCraft. No new Rust dependency or independent renderer/simulation engine was introduced. No push was made.

## Build and test results

### ChrononMotion3d

- `cmake --preset release-fast -DCHRONON3D_INCLUDE_DIR=../Chronon3d/include`: configured.
- `cmake --build --preset release-fast`: passed (179 build steps).
- `ctest --preset release-fast --output-on-failure -j 4`: 60/61 passed.
- Remaining failure: `chrononmotion_showcase_all`, with three stale golden values (`camera_dolly_parallax`, `dolly_zoom`, `same_seed_golden`). The existing `docs/SCOPE_CLOSEOUT.md` already documents the `same_seed_golden` mismatch as pre-existing and explicitly disallows re-recording without owner review. No golden was changed. The boundary test now passes.

### Chronon3d

- `cmake --preset linux-fast-dev`: configured.
- `cmake --build --preset linux-fast-dev`: passed (511 build steps).
- Built the focused core, timeline, scene, particle sampling, Motion bridge, effect, backend, compositor, render-job and render-graph test binaries.
- Focused CTest selection: 8/10 passed. The two failures are existing baseline issues: the core shaping corpus rejects a family ZWJ sequence with zero reported missing glyphs; the render-graph normalized-program golden computes `0x3b196a95fae3f89e` versus recorded `0xaccd5b842ed2c6a2`. Repeated compilation within the golden test is deterministic. The repository ticket already records the ZWJ/shaping issue. Neither failure was masked or re-baselined.

## Scope decision

Do not add Echo, Fractal Noise, particles, layer retiming, sub-frame motion blur, or offline simulation primitives again; the source already contains them. The largest temporal gap is a graph contract for deterministic arbitrary-time source sampling inside an effect stack, followed by Time Displacement and optical-flow Timewarp. Those are VFX/temporal-compositing expansions and remain outside the requested implementation scope. Tracking, keying, EXR/Cryptomatte and roto work also remain unactioned here.

The C3D and CM3D test failures above are visible and must be resolved by their owners before claiming a fully green baseline. They do not justify changing golden values without semantic review.
