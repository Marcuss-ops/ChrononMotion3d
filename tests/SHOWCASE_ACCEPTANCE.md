# ChrononMotion Acceptance Suite

The acceptance runner turns the 60 showcase ideas in the motion roadmap into
small deterministic C++ scenes with quantitative assertions. It is a motion-data
suite, not a renderer: this repository cannot produce golden pixels on its own.
Each supported case records a committed 64-bit FNV-1a signature over the
length-delimited exported Motion JSON and sampled scene/solver output in
`showcase_golden.json`. Assertions are evaluated independently of the signature.
Track easing is authored on the starting key because each key controls the curve
into its next key. The signature is a compact regression tripwire, not a
cryptographic integrity claim.

## Run

From `ChrononMotion3D/` after configuring/building a CMake preset:

```sh
cmake --build --preset release-fast --target chrononmotion-showcase
./build/release-fast/chrononmotion-showcase --list
./build/release-fast/chrononmotion-showcase --case transform_grid
./build/release-fast/chrononmotion-showcase --case 16
./build/release-fast/chrononmotion-showcase --all
```

`--all` runs all supported cases, verifies each against the committed golden,
and reports unsupported capabilities as `[SKIP]` with the reason. Skips do not
count as passes. `--dump-golden` prints regenerated signatures only after every
supported scenario's assertions succeed; updating the manifest is an explicit
reviewed operation.

CTest always registers `chrononmotion_showcase_all`, `chrononmotion_showcase_catalog`,
and `chrononmotion_showcase_renderplan`. With `CHRONON3D_INCLUDE_DIR` configured,
the RenderPlan selection runs normally. Without the optional adapter, case 59
appears as an explicit capability skip in the catalogue and `--all`; CTest marks
only `chrononmotion_showcase_renderplan` disabled.

## Catalogue and acceptance checks

| # | Case | Executable acceptance checks |
|---:|---|---|
| 01 | Transform Grid | nine layer/channel variants, sampled matrices |
| 02 | Easing Gallery | ten responses, start/end, midpoint shape and overshoot contracts |
| 03 | Moving Pivot | keyed anchor at midpoint/end |
| 04 | Parent Solar System | parent/grandchild world positions |
| 05 | Parent Scale Torture | matrix product and shear terms |
| 06 | Quaternion Spin | accumulated angular travel and per-frame step |
| 07 | Bezier Path Ride | endpoints, orientation keys and tangent-facing midpoint |
| 08 | Path Constant Speed | equal arc-length step error across line/Bézier/Catmull-Rom |
| 09 | Path Loop Seam | closed-path endpoint equality |
| 10 | Stagger Wall | exact 50-card stagger starts, offscreen offsets and per-card endpoint arrival |
| 11 | Center-Out Reveal | exact 21-item radial rank-to-start-time mapping, including tie order |
| 12 | Random Deterministic Reveal | seeded 100-item permutation drives 100 layers with matching reveal times |
| 13 | Follow Through Chain | valid links and exact accumulated 0.04-second lag per follower |
| 14 | Spring Tail | pinned root, finite joints and eight-segment length error across every bake frame |
| 15 | Rigid Drop | finite/bounded positions, track parity, floor clearance and pairwise separation across 181 frames |
| 16 | Two Bone Arm | 61 target/error/segment-length checks |
| 17 | IK Reach Limits | inner/outer clamp and boundary reachability |
| 18 | General IK Snake | 8-segment FABRIK end-effector target convergence, reach and lengths |
| 19 | Pole Vector Knee | every sample keeps opposite knees on the requested pole sides |
| 20 | Space Switch | two timed world/torso/object handoffs preserve world pose, then follow the final parent; random-access evaluation |
| 21 | Aim Turret | aim-axis agreement at five target samples |
| 22 | Billboard Cards | facing direction agreement |
| 23 | Keep Distance Orbit | world-space radius at six instants |
| 24 | Constraint Blend | animated Aim influence checks free/half/full orientations and no-pop continuity |
| 25 | SVG Morph Basic | inclusive frames, cubic controls/endpoints/midpoint, closed topology and repeatability |
| 26 | SVG Morph Adversarial | relative line/H/V/C/S/Q/T/A/Z normalization |
| 27 | SVG Repeater Spiral | first-copy identity, second-copy transform, fifty copies and repeatability |
| 28 | SVG Bone Arm | weighted cubic control-point skinning follows a two-bone rig while the base remains fixed |
| 29 | SVG Puppet Flag | pinned mast stays fixed; deterministic sinusoidal motion grows through stiffness falloff |
| 30 | Squash Stretch Icon | overshooting squash, transform scale and exact rest endpoint |
| 31 | Wave Deformer | rest-frame identity, deterministic inclusive geometry bake, measurable in-field displacement |
| 32 | Grid Falloff Wave | 400-card 20×20 grid, sampled monotone spatial influence sweep and opacity output |
| 33 | Radial Falloff Flip | center maximum, distance attenuation and zero rotation outside radius |
| 34 | Cloner Spiral | 200 scene layers, deterministic allocated IDs, indexed spiral radius/angle and staggered reveal |
| 35 | Magnetic Swarm | 200 finite samples, attractor convergence, repulsor dispersion and deterministic baking |
| 36 | Vortex | 200 samples, stable outer orbit radius and increasing inner-core radius |
| 37 | Bento Assemble | twelve flow-laid panels, pairwise bounds, exact landing positions and opacity reveal |
| 38 | Auto Layout Localization | larger measurements expand without overlap; refreshed measurements/fingerprints update scene layers |
| 39 | Audio Bass Grid | generated low-frequency PCM drives finite scalar keys and actual X/Y scale tracks |
| 40 | Audio Onset Punch | silence-to-tone onset detection drives finite scalar keys and actual scale punch |
| 41 | Camera Dolly + Parallax | authored camera pan drives opposing near-vs-far screen-space shifts |
| 42 | Camera Orbit | orbit radius at 13 sampled frames |
| 43 | Camera Whip | overshoot sample and exact settle endpoint |
| 44 | Dolly Zoom | normalized apparent scale throughout interpolated camera tracks |
| 45 | Focus Pull | authored focus-distance interpolation |
| 46 | Depth Cascade | exact 30-card stagger times, depth offsets and per-card arrival |
| 47 | Perspective Deck | valid parent graph and changed child world pose under animated pivots |
| 48 | Time Reverse | remapped source endpoint/midpoint values |
| 49 | Freeze + Ramp | seven remapped time/pose samples |
| 50 | Posterize Motion | held values and sample-boundary advance |
| 51 | Infinite Loop | position seam plus repeated position, rotation and scale samples |
| 52 | Subframe Stress | X/Y exact-time values and random-access reproducibility |
| 53 | 2000 Layer Stress | evaluation order, graph validation and first/last transform poses |
| 54 | 1000 Track Stress | dense 21-key tracks on 1,000 layers |
| 55 | Hierarchy Stress | 100 levels and accumulated world position |
| 56 | Same Seed Golden | bit-identical same-seed driver samples and changed-seed divergence |
| 57 | Different FPS Equivalence | exact-PTS pose equality at nine shared quarter-second timestamps |
| 58 | Bake vs Runtime Parity | 81 baked path keys vs direct path samples |
| 59 | RenderPlan Roundtrip | JSON emission, canvas/output, 31-frame position endpoints and repeat serialization; requires optional adapter |
| 60 | Mega Scene | path, Aim, Verlet-track parity, 3D IK target/lengths and hierarchy evaluate together |

With the optional adapter configured, **60 of 60** showcase cases execute and
no capability gaps are skipped. Without the adapter, **59 execute and one is
skipped**: case 59 requires the optional adapter. The golden manifest retains
the adapter-enabled signature for case 59 so both build configurations share
one reviewed baseline; the base runner validates all 59 executable case
signatures and permits that single adapter-only entry.
Case numbers remain fixed so roadmap references are stable.

The ChrononMotion case 59 checks emitted JSON fields and byte-for-byte repeated
serialization; it does **not** invoke the Chronon3D schema validator/decoder.
Canonical typed validation and decoding are implemented in the Chronon3D
`chronon3d_motion_plan_adapter_tests` target, which exercises schema-valid V2/V3
lowering plus audio/video remap, blend modes, SVG Morph/Repeater and camera
plans. Build and run that target to close the repository-reachable typed
RenderPlan contract. Neither runner produces rendered frames or pixel goldens:
a renderer-backed, content-asset-backed acceptance lane remains outside these
trees' current capability.

## Golden scope

Golden signatures include `sceneToJson` (authoring intent) plus selected sampled
world matrices, solver paths, or transformed values. Assertions remain the
readable behavioral contract; hashes catch unanticipated edits across the
captured scene/pose surface. Scene metrics, memory allocations, wall-clock
performance and rendered pixels are not frozen by this suite. Stress scenarios
validate completion and output invariants, not a machine-dependent timing
threshold.
