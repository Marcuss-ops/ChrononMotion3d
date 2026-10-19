# ChrononMotion3d — Scope Closeout V1

Date: 2026-10-19. Status: recorded decisions + enforced gates. This file does
not add features; it closes open scope questions so the tree stops growing by
accumulation. Each item names its authority, its gate, and its exit condition.

Two boundaries, stated once:

1. **Core (`include/`, `src/`, `tests/`): content-blind.** Chronon owns content
   and pixels; Motion owns space and time. Enforced by
   `tests/check_motion_boundary.cmake` (`chrononmotion_boundary`).
2. **`render/`: look-dev and proof harness, never a production renderer.**
   It turns Motion-baked scenes into pixels for preview and certification.
   Production pixels stay in Chronon3D. Enforced by
   `tests/check_render_scope.cmake` (`chrononmotion_render_scope`).

## Closed (with authority)

- **No `MeshLayer`, no graph unification.** `Object3D` stays the authority for
  retained threepp object graphs; `MotionScene`/`Layer` stays the authority for
  authored/evaluated motion; no auto-conversion either way. Authority:
  `docs/ADR-MESH-LAYER-OBJECT3D.md`. Revisit only with owner approval per that
  ADR. Gate: existing tests + boundary guardrail.
- **Rig serialization: done, not missing.** `chrononmotion.rig.v1` (graph round
  trip, byte-for-byte) and `chrononmotion.rig.program.v1` (graph + controllers/
  drivers + poses + geometry) are implemented and certified. Authority:
  `docs/RIGGING_FINAL_V1.md` §5 + `chrononmotion_rig_pipeline` (36 checks).
  No second serialization format is approved; a new one would be a boundary
  violation, not a feature.
- **Narrow glTF contracts: done.** Static geometry import: `docs/GLTF_READER.md`
  + `chrononmotion_gltf`. Motion import (TRS hierarchy, skins with explicit
  inverse binds, JOINTS_0/WEIGHTS_0, morphs, LINEAR/STEP clips):
  `docs/RIGGING_FINAL_V1.md` §5. Widening either contract needs new fail-closed
  tests first, not after.
- **Upstream test coverage: done as a scoped port.**
  `docs/THREEPP_TEST_COMPATIBILITY.md` + `chrononmotion_upstream_regressions`.
  AnimationMixer, Layers, Raycaster and renderer-only cases stay excluded.

## Deliberate exclusions (kept out, with exit conditions)

- **RigProgram schedules** are intentionally not serialized; schedule inputs
  (animation tracks, switches, IK specs) serialize from their own authorities.
  Exit condition: none — this is the design, not a gap.
- **PinWarp stays smooth radial weighting** (no triangle puppet solver);
  **lattice stays bilinear XY**; **secondary motion stays planar XY** (Verlet).
  Exit condition: a concrete production shot that cannot be authored otherwise,
  plus golden tests, plus an ADR.
- **Animated camera DoF and physical lens optics (focal length / f-stop) fail
  closed in the lowering, permanently.** Static camera DoF (schema focus
  distance / aperture / max blur) is the only supported depth contract. Exit
  condition: a Chronon3D schema extension first — Motion never invents the
  vocabulary.
- **Preset freeze.** No new preset enters `motion/Presets*`. New vocabulary is
  authored in ChrononTemplate on top of the primitives. A core addition needs:
  a defining-property test in `chrononmotion_presets`, lowering coverage, and
  a line in this file. The catalog grows by proof, not by taste.

## RenderPlan lowering — supported subset v1

Lowering (`motion/RenderPlanLowering.hpp`, opt-in via `CHRONON3D_INCLUDE_DIR`)
bakes to dense `linear` keys at integer frames. Supported: flattened world
transforms; per-key easing baked (never re-spelled); camera rig poses + static
DoF; V3 static Merge/Morph path operators; inline static-triangle mesh payloads
(assetId without geometry is not consumable); rig geometry frames/samples/bakes
with matching topology; baked cubic paths and repeater/morph attachment.
Fail-closed (rejected, never approximated): shear, reflections, singular
transforms, unrepresentable anchors/perspective, drivers and per-key easing as
spelled constructs, physical optics, animated DoF, material/animation
semantics, invented assets/text/styling. Parity gates:
`motion_renderplan_easing_parity`, `motion_projection_parity`,
`motion_camera_bake_roundtrip`, `check_renderplan_camera_parity.cmake`.
Full end-to-end proof (manual/host gate):
`tools/check_render_plan_consumer_canary.sh`.

## `render/` scope rule

Allowed: GL preview and look-dev, `MotionSceneBridge`, headless canvas,
spike/certification proofs **behind `CHRONONMOTION_BUILD_SPIKE_PROGRAMS=OFF`
default**, GPU certification encoders inside spike-gated proofs only.
Forbidden: codec SDK linkage in the shipped library/tools, a compositing or
effects catalog, Chronon3D content-engine includes (only `chronon3d/render_plan`
is an approved vocabulary), new `thirdparty/` additions without an ADR,
production-pixel claims. Tracked `.mp4` review evidence under `render/spike/`
is grandfathered; new binaries need a contract note like
`render/spike/assets/scene03/` has.

## Hygiene

- Local build trees (`build/`, `build-*/`) are ignored, never tracked.
- Uncommitted work stays local until reviewed; one identifiable revision per
  certification claim (same rule as Chronon3D's focus gate).
