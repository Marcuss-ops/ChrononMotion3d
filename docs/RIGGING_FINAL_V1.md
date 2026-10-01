# ChrononMotion3D Rigging Final V1

## Goal and boundary

One authoring authority (`RigGraph` → `RigEvaluator`) for 2D, 2.5D, SVG and
lightweight mesh/character motion. Output is ordinary transforms, tracks and
baked geometry. Chronon3D must not evaluate bones, IK, controllers or rigs.
No renderer, implicit I/O, arbitrary expressions, or new third-party dependency.
Reuse `solveIKChain3D`, `bakeVerletChain`, `MotionPath` and existing SVG bakers.

**Status: rigging core certified on executed tests; remaining scope is explicit
at the end of this file.** Checkboxes require executed tests, not declared types.

## Ordered implementation and acceptance

### 0. Baseline
- [x] Release builds with warnings as errors, all enabled CTest tests pass.
- [x] Boundary guardrail passes; optional adapter results reported separately.

### 1. Graph and skeleton (P0)
- [x] Stable numeric nonzero RigNodeId; duplicate IDs and missing references fail.
- [x] Typed Bone/Controller/LayerBinding/PathPoint/MeshBinding/Pin nodes.
- [x] Compiled dependency graph, parent-first stable ID ordering, cycle rejection.
- [x] Bind/rest/current poses and skeleton hierarchy with positive bone length/axis.
- [x] FK, local/world round trip, bind → pose → bind recovery.
- [x] Validate NaN/Inf, quaternion, singular transforms, enum and work budgets.
- [x] Pure random-access evaluation; 100 identical evaluations bit-stable.
- [x] Layer bindings bake to ordinary tracks without changing Chronon3D.

### 2. Constraints (P0)
- [x] Extend existing ConstraintKind and reuse its evaluator where applicable.
- [x] Parent, Orient, Point, StretchTo, position/rotation/scale limits.
- [x] IKChain adapter to canonical FABRIK; pole vector supported.
- [x] 2/4/8/32 segment reachable/unreachable/boundary/moving target/pole tests.
- [x] Reachable end error < 1e-4, segment lengths stable, no NaN, repeat stable.
- [x] SpaceSwitch with explicit spaces, active space, preserve-world offset.
- [x] TestSpaceSwitchNoPop at frames 20/21, including rotated/scaled parents.
- [x] Fixed documented constraint stages; dependency and constraint cycles fail.
- [x] DampedAim, SurfaceAttach, LookAtWithRoll semantics and validation.

### 3. Controllers, drivers, poses (P0)
- [x] Controller1D/2D/3D/Bool/Enum, validated ranges and neutral/left/right tests.
- [x] Typed DriverGraph: Constant, Controller, Time, Track, Distance, Angle,
      Remap, Clamp, Smoothstep, Add, Multiply, Min, Max, seeded Noise.
- [x] Resolved numeric inputs; cycles/NaN rejected; bounded remap deterministic.
- [x] Driver outputs support bones, controller/constraint/deformer/pose weights.
- [x] PoseMixer Override/Additive/Weighted/Masked blends, weights 0/.5/1.
- [x] Upper-body masks preserve legs; additive pose preserves base.
- [x] Single canonical RigOperatorRegistry, validation/evaluation/capabilities.

### 4. Motion and deformation (P1)
- [x] SecondaryMotion adapter to Verlet; bone/controller/layer/path/pin targets.
- [x] Validated stiffness/damping/mass/gravity/drag/maxAngle semantics.
- [x] Zero force/rest, damping/settle, repeated bake and random access tests.
- [x] SVG rigid groups bind to bones without SVG parsing/content ownership.
- [x] Position/Rotation/Scale/Stiffness pins; puppet arm/head/body benchmark.
- [x] Unified DeformerStack: Bend/Wave/Twist/Bulge/Taper/Lattice/PinWarp/
      PathWarp/NoiseWarp/SquashStretch.
- [x] 4x4/8x8 lattice validation and face/flag/logo tests.
- [x] Empty stack and amount zero exact identity; A+B differs from B+A.
- [x] Weighted 2D skin, max four influences, normalized weights, inverse bind.
- [x] SplineIK uses MotionPath; rigid chords on curved paths, arc-length order,
  straight-path invariance, unfittable-chain rejection and repeat determinism
  are tested.
- [x] FK/IK blend and matching; TestFkIkSwitchNoPop for both switch directions.
- [x] MorphMixer weights 0/.5/1, topology validation and deterministic multi-morph.

### 5. Pipeline (P2)
- [x] Narrow glTF motion import: nodes/joints/skins/inverse binds/JOINTS_0/
      WEIGHTS_0/clips/morphs. No material/shader/scene-authoring expansion.
- [x] Fail-closed buffer/accessor/hierarchy/topology/weight/work-budget tests.
- [x] chrononmotion.rig.v1 authoring serialization and strict round-trip reader.
- [x] bakeRig produces ordinary Track<T>/paths/geometry and MotionScene bindings.
      RigProgram bake emits per-frame pose + geometry and one signature covering
      all channels; transforms lower to MotionScene tracks transactionally.

### 6. Certification (FINAL)
- [x] 01 FK Arm; 02 Two Bone IK; 03 Eight Bone Snake; 04 Pole Knee.
- [x] 05 IK Reach Limits; 06 FK/IK Switch; 07 Space Switch; 08 Aim Turret.
- [x] 09 Rotation Limit; 10 Stretch Arm; 11 Spline Tail; 12 Secondary Tail.
- [x] 13 SVG Rigid; 14 SVG Puppet; 15 SVG Lattice; 16 Skin Mesh.
- [x] 17 Pose Mixer; 18 Controller Joystick; 19 glTF Skeleton; 20 Mega Character.
- [x] Mega Rig 10s: neutral, controller, IK, pole, space, FK/IK, bend,
      secondary settle, additive wave, neutral. No pop/NaN/drift/length changes.
- [x] Bake frames 0..300 twice: identical pose/track/geometry signatures.
- [x] Direct frame 197 equals sequential 0..197; same seed same bake.
- [x] Performance corpus: 100 bones, 100 constraints, 20 controllers,
      10 deformers, 1000 frames; report time/allocations/peak memory.
- [x] No unbounded allocation growth, no string lookups in hot path.
      Current corpus gate measures equal allocation count/bytes on two passes;
      warmed FK has zero allocations. This is not a leak-sanitizer certification.
- [x] Release, Debug and architecture gates green; document optional gate skips.

## Exclusions

Cloth, full 3D soft body/hair/muscles/ragdolls, full retargeting, mocap editor,
node GUI, painting/weight editor and Blender-like scene authoring are excluded.

## Current API and implemented contracts

Public headers: `RigGraph.hpp`, `RigDrivers.hpp`, `RigDeformers.hpp`,
`RigBaking.hpp`, `RigOperators.hpp` (exported by `Motion.hpp`), plus the motion
reader declarations in `assets/GltfReader.hpp`.

- `RigProgramEvaluator` schedules every frame in one canonical order:
  authored animation → spaces → drivers → pose mix → IK (canonical FABRIK,
  controls resolved from world nodes or tracks) → constraints + limits →
  secondary → geometry (morphs, deformers, rigid/skin). Controllers route
  weights to IK, constraints, pose layers, deformers and morphs through resolved
  IDs; competing authorities and IK feedback fail closed.
- `RigProgramEvaluator::bake()` emits per-frame pose + geometry and one FNV-1a
  signature over float bits, IDs, counts, all transform channels and geometry.
  Transforms lower transactionally to MotionScene tracks via `applyBake`.
- `matchIK` provides FK→IK matching (effector + pole recovered from the FK pose;
  solving reproduces the FK pose exactly); `matchWorldPose` provides IK→FK and
  re-parenting matching. Canaries 06/07 pin both no-pop directions.
- `evaluateRigSpaces` adapts MotionScene's timed space-switch authority; the
  direct-vs-sequential regression covers animated parent spaces.
- Constraints: Parent, Orient, Point, StretchTo, three limits, DampedAim,
  SurfaceAttach, LookAtWithRoll; targets resolve in parent space at every stage.
- `rigOperatorRegistry()` is the single typed catalog (48 operators with stage,
  category, arity, capabilities); graph/driver/deformer validation dispatches
  through it instead of independent per-category registries.
- Secondary motion maps stiffness/mass/force/drag/maxAngle onto the canonical
  Verlet baker (inverse-mass response, stability-gated fixed step, exact length
  reconstruction); zero-force still rests exactly.
- Serialization: `chrononmotion.rig.v1` (graph round trip, byte-for-byte,
  unknown fields/duplicate keys/cycles rejected) and `chrononmotion.rig.program.v1`
  (graph + controllers/drivers + poses + geometry with deformers; schedules and
  import-derived handles are deliberately rejected and re-derived elsewhere).
- glTF motion import (`loadGltfMotion`): TRS hierarchy, triangle POSITION,
  skins with explicit inverse binds (validated, not silently replaced),
  JOINTS_0/WEIGHTS_0 (float or normalized), morph targets, LINEAR/STEP clips
  incl. animated `weights` channels; buffer/accessor/topology failures closed.

Known scope limits, stated precisely:
- RigProgram v1 serialization intentionally excludes schedules; serialize the
  schedule inputs (animation tracks, switches, IK specs) from their own
  authorities instead.
- PinWarp remains smooth radial weighting, not a triangle-based puppet solver;
  lattice is bilinear XY. Secondary motion is planar XY (Verlet authority).
- SplineIK keeps rigid chords: each joint is the first point forward on the
  MotionPath at exact rest-chord distance (scan + bisection), the root slides
  forward just enough to fit the interval, and an unfittable chain or a
  non-converged chord fails closed. The old stretch-to-fit behavior is gone.
- glTF motion import is a narrow V1 contract; skins must match canonical bind
  unless explicit inverse binds are supplied, and materials/extensions stay out.

## Evidence log

Certification re-run (2026-09-30, current tree, machine: Linux/g++):

- Release `-Wall -Wextra -Werror`: **42/42 enabled CTest tests passed**
  (`chrononmotion_showcase_renderplan` stays disabled without
  `CHRONON3D_INCLUDE_DIR`). Debug: **42/42 passed**. ASan+UBSan
  (`detect_leaks=1`): **42/42 passed**, no leaks, no UB reported.
- `chrononmotion_rig_canaries --all`: **1255 checks, 0 failures** across all
  20 canaries including the Mega Rig 10-second sequence.
- Rig suites: `chrononmotion_rig` **3076 checks** (graph ordering/cycles/rest
  pose/local-world round trip/random-access determinism, FABRIK reach sets,
  SpaceSwitch + FK/IK no-pop both directions, secondary, SplineIK),
  `chrononmotion_rig_program` **1729 checks** (controllers, driver arithmetic,
  pose mixer, DampedAim/SurfaceAttach/LookAtWithRoll),
  `chrononmotion_rig_pipeline` **36 checks** (strict serialization, glTF
  import) — all 0 failures.
- Performance gate re-sample (machine-dependent): FK 1000 frames
  **40.3 ms / 0 allocations** warmed; full corpus **182 ms/pass /
  17,000 allocations** with bounded repeat volume (95.1 MB/pass);
  bake 234 ms, peak RSS ≈ **65 MiB**.

- Initial Release build: blocked by pre-existing undefined `skip` call in
  `tests/motion_showcase.cpp` when the optional adapter is disabled.
- Existing authorities verified by source audit: FABRIK 3D, 2D analytic IK,
  Verlet, SVG skin/path pin bakers, layer SpaceSwitch.
- Final executed gates (current tree): Release `-Wall -Wextra -Werror` —
  **42 enabled CTest tests passed**; Debug — **42 passed** (rig subset rerun
  25/25). `chrononmotion_showcase_renderplan` stays disabled without
  `CHRONON3D_INCLUDE_DIR`; no adapter success is claimed.
- Sanitizer gate: ASan+UBSan build with `detect_leaks=1` — **24/24 rig tests
  passed** (rig, program, pipeline, 20 canaries incl. Mega Rig). No leaks,
  no UB, no invalid accesses reported. **Re-confirmed 2026-09-30: 42/42
  enabled tests passed** in the sanitize build.
- Rig suites: ordering/cycles/invalid data/immutable snapshots, 100 repeated
  poses, 301-frame repeat bake, direct197, buffer reuse, FABRIK 2/4/8/32 with
  reach/boundary/pole, FK↔IK matching both directions, timed SpaceSwitch
  no-pop incl. animated parents and direct-vs-sequential, all controller kinds,
  full driver arithmetic table, pose weights/masks/additive, registry coverage,
  deformer stack order, lattice, four pin kinds, skin (explicit inverse binds),
  morphs, secondary parameter contract, DampedAim/SurfaceAttach/LookAtWithRoll,
  rig.v1 + rig.program.v1 round trips, glTF motion malformed inputs.
- Program bake acceptance: frames 0..300 baked twice with identical full
  signature (pose floats + all transform channels + geometry); every frame's
  signature equals its direct evaluation; geometry and nonposition channels
  verified to participate in the signature.
- Release performance sample (machine-dependent, not a universal budget):
  FK 1000 frames **30.4 ms / 0 allocations** after warm-up; full corpus
  **≈160 ms/pass / 17,000 allocations** with identical allocation volume across
  passes; peak RSS ≈ **66 MiB**. Values are reported by
  `chrononmotion_rig_performance` on every run.
- Fixes made in this tree during the milestone: undefined `skip` reference in
  the showcase catalog without the adapter; unbalanced lattice object in rig
  program JSON; cross-namespace serialization declarations; a fixture null-deref
  detected by the ASan build.
