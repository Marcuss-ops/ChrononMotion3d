# ChrononMotion

## Rigging Final V1 — core certified on executed tests

The authoring rig authority (`RigGraph`, typed controllers/drivers, pose
layers, geometry deformers, skin/morph mixing, staged `RigProgram` scheduling
and canonical solver adapters) is available through
`chrononmotion/motion/Motion.hpp`. It bakes ordinary tracks, transforms and
geometry with one deterministic signature per bake; Chronon3D remains outside
rig evaluation. The 20 rig canaries, the 10-second Mega Rig, Release/Debug
gates and an ASan/UBSan leak gate pass on the current tree; see the
[evidence-based milestone checklist](docs/RIGGING_FINAL_V1.md) for the exact
gates, implemented contracts, remaining scope limits (schedule serialization,
puppet-triangle deformation, planar secondary, narrow glTF motion import) and
evidence log.

**A Linux-only motion engine, forked from [threepp](https://github.com/markaren/threepp).**

ChrononMotion keeps the half of threepp that a deterministic motion/transform
compiler needs — the scene graph, the transform and quaternion/curve maths, the
cameras, the keyframe tracks — and drops the half that exists to put pixels on a
screen or to drive a game and a simulation. No renderer, no window, no physics,
no sensors, no runtime media decoders, no python, no material zoo. A deliberately
small, dependency-free static glTF 2.0/GLB 2.0 reader now imports triangle
geometry as `BufferGeometry` + `Mesh`; see [the reader contract](docs/GLTF_READER.md).

```text
threepp (upstream)                     ChrononMotion
────────────────────────────────       ─────────────────────────────────
math/ core/ animation/ cameras/        kept, and largely untouched
extras/curves                          kept
objects/{Group,Mesh}, scenes/Scene     kept, narrowed
materials/ (40 materials + mixins)     one abstract Material base
renderers/ loaders/ material zoo        deleted
geometries/                              Box, Plane, Sphere + imported BufferGeometry
lights/ helpers/ utils/ textures/      renderer/runtime parts deleted; data-only lights and spatial math kept
canvas/ controls/ input/ audio/        deleted
postprocessing/ splats/ extras/        deleted
apps/ examples/ tests/ python/         deleted
```

Roughly **6.9% of the upstream library sources survive** (~17.8k of ~259.3k lines,
130 files across `include/` and `src/`), and nothing is fetched at configure time. What grows back on top is the
half threepp does not have: the deterministic motion model in
`include/chrononmotion/motion/` — see [The motion model](#the-motion-model).
Three geometry primitives (`BoxGeometry`, `PlaneGeometry`, `SphereGeometry`)
were re-imported with it, so a `Mesh` has something to carry that is not built
by hand.

The fork is not a rewrite: `Object3D` is still the three.js node — parent and
children, position/rotation/quaternion/scale, local and world matrices,
`lookAt()`, `localToWorld()`/`worldToLocal()`, `updateMatrixWorld()` — with the
renderer's flags and callbacks removed. `Curve`, `CurvePath`, `CatmullRomCurve3`,
the Bézier curves, `AnimationClip`, `KeyframeTrack`, `Interpolant`, the cameras
and the transform maths come across as they are. The animation *runtime* is gone
(no `AnimationMixer`): what stayed is the clip/track data model, because the
sampling authority has to be Chronon's, not a second mixer's.

**[CHRONONMOTION.md](CHRONONMOTION.md) is the ledger**: what was kept and why,
what was deleted, every behaviour change against upstream, and what is
deliberately not done yet. Read it before changing anything here. Offline baker
contracts are in [OfflineBaking.hpp](include/chrononmotion/motion/OfflineBaking.hpp),
and the optional typed adapter contract is in
[RenderPlanLowering.hpp](include/chrononmotion/motion/RenderPlanLowering.hpp).

## Minimum + 1 foundation

The retained 3D foundation is intentionally renderer-agnostic: `Ray` and
`Frustum` provide spatial queries, `AmbientLight`, `DirectionalLight`,
`PointLight` and `SpotLight` provide data-only lighting descriptors, and
`UnlitMaterial`, `LambertMaterial` and `EmissiveMaterial` describe material
intent without shaders, textures or GPU state. `MotionScene::compile()` produces an evaluated `CompiledScene` snapshot with
layer and light states; material and mesh descriptors can be appended explicitly
at the renderer boundary. The snapshot is intended for a future renderer, with
no GPU state or content bytes owned here. These types
are ChrononMotion-owned; the upstream threepp renderer, shadow system and
`Object3D` light graph are not restored.

## Status

The motion model, deterministic offline bakers, and static geometry reader are
available. Verlet/rigid-body, IK, PCM audio feature/driver, flow layout, SVG
morph/repeater, and RenderPlan lowering execute at authoring time only. The
optional Chronon3d adapter is built only when `CHRONON3D_INCLUDE_DIR` is supplied;
it lowers representable poses into typed V2/V3 plans and is never a renderer
runtime dependency. Schema-defined static camera DoF round-trips through the
plan adapter; physical lens metadata and animated camera DoF fail closed. A
schema-driven CTest gate detects camera-field contract drift. Static glTF
2.0/GLB 2.0 import is geometry-only and fail-closed;
see [GLTF_READER.md](docs/GLTF_READER.md). The public headers and C++ namespace
use `chrononmotion`; Box, Plane, Sphere, and imported BufferGeometry primitives
are available.

## The motion model

```text
Transform          Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation, Opacity
Track              keyframes, per-segment easing, deterministic sampling
Track modifiers    loop / posterize / delay / link / window / influence / smooth
Easing             linear / ease in-out / custom cubic Bézier / overshoot / bounce
Layer              identity, parent, content, in/out lifetime, animated transform
Lifetime           inFrame/outFrame; the entrance and exit are keyed from it
MotionScene        hierarchy (Parent, Null) + deterministic frame/PTS evaluation
MotionPath         line / cubic Bézier / Catmull-Rom + orient-to-path
MotionPlane        2.5D text and image planes, Z depth, perspective + parallax
CameraRig          perspective, look-at, dolly/pan/tilt/orbit/roll, FOV/focal
Presets            the YouTube presets, built on the primitives above
```

The presets cover the list a text/image promo actually needs: slide in/out on all four sides, push/pull on Z, scale pop and overshoot, fade + slide, fade + scale, flip X/Y, card turn, 3D tilt, spin Z, spin XYZ and a spin that unwinds *into* place (`spinIn`), bounce and spring settles (`bounceIn`, `springSettle`), an opacity pulse train (`pulseOpacity`), parallax entrance, camera push/pull/pan/orbit/shake, floating hover, drift, arc and path motion, staggered entrance, parent-group reveal and a travelling group wave (`rippleWave`) — plus an animated pivot (`anchorTo`), secondary motion with lag (`followThrough`) and an entrance/exit keyed from the layer's own lifetime (`lifetimeSlide`). A documentary map vocabulary extends the same list: a target ping (`radarPing`, the ripple a drop pin leaves behind), an area reveal (`areaReveal`), a 2.5D lift (`extrudeRise`), a self-drawing rule (`lineDraw`), a reticle that snaps onto its point (`crosshairLock`), and three camera moves — `orbitalSlamZoom` (a dive curve on the descent), `cameraDrift` (a slow orbit that never freezes) and `cameraOrbit` (the 2.5D swing that holds its target centred while it pitches and yaws).

On the map, the same vocabulary is composed rather than reimplemented. `MapFlyoverBuilder` walks a route or a territory contour as a chain of soft glow sprites at even arc length — the great circle between two places, never the straight line the projection makes of it — lit in sequence with a travelling brightness wave and a pulsing head, so the course draws itself soft-edged and keeps moving. A vignette and a dim are screen-space plates (a vignette belongs to the frame, not to a plate that pans), a callout is centred inside a hard safe margin, and a territory lights its fill to a fraction of its own alpha while the rest of the map steps back.

The channel modifiers are the other half of the vocabulary: `Loop` repeats a window, `Window` holds outside one, `Posterize` holds on a grid, `Delay` shifts the read instant, `Link` borrows another layer's change, `Influence` dials a channel down against its own origin value, and `Smooth` averages a channel over a symmetric window. They are the reference-free half of After Effects' channel toolbox, and they are *authoring-time data*: each one is exported, never baked into a key list.

The composition rules, in one place:

* a layer's local matrix is `T(position) · R · S(scale) · T(-anchor)`, with
  `R = orientation · euler(rotation)` — the anchor is the single pivot for both
  rotation and scale;
* a channel with keys overrides the layer's static pose, so a preset can shape a
  whole transform or touch one axis — including `anchor`, so the pivot a layer
  turns about can itself be keyed;
* a measurement carries a fingerprint, and `MotionPlane::refresh` re-derives the
  pivot — and a `fitted` plane's aspect ratio — when the content is re-measured;
  what it writes is the static pose, so a keyed `anchor` track still wins;
* `lifetime` (`inFrame`, `outFrame`) gates a layer *and* its subtree, and the
  arrival/departure animations are keyed from those two frames rather than from
  hand-picked ones: an in/out animation cannot drift from the range the layer is
  visible for;
* `evaluate(frame)` is a pure function of `(scene, frame, fps)`: the same frame
  always produces the same matrices, whatever was evaluated before it;
* **text and image planes are the same type.** The only difference between a text
  layer and an image layer is the opaque `ContentRef` they carry, so their
  transform, anchor and parallax semantics cannot drift apart.

```cpp
#include "chrononmotion/chrononmotion.hpp"

using namespace chrononmotion;
using namespace chrononmotion::motion;

MotionScene scene("promo", 30.f);
scene.addNull(10, "Null_Main");

Layer& title = scene.addContent(11, "Title", ContentRef::text("title", {800.f, 120.f}), 10);
MotionPlane(title).applyTo(title);           // 2.5D plane: anchor + Z depth
title.transform.position.y = 2.f;

presets::slideIn(title, presets::Direction::Left, 0, 30, 6.f, 30.f);
presets::fadeIn(title, 0, 30, 30.f);

CameraRig camera(20, 60.f, 16.f / 9.f);
camera.setPosition({0.f, 0.f, 14.f}).setTarget({0.f, 0.f, 0.f});
presets::cameraPush(camera, 45, 30, 4.f, 30.f);

auto cameraNode = camera.makeCamera();
scene.evaluate(45);                          // deterministic: frame in, state out
camera.apply(45.f / 30.f, *cameraNode);
```

Social/web-style scene recipes live in the separate sibling module
`../ChrononTemplate`; this core exposes only the motion primitives and
`MotionScene` contract they consume. The template module owns composition and
timing while text/image bytes, font layout and rasterization remain outside
ChrononMotion through `ContentRef`.

## Build

Linux is the only supported deployment target. No platform abstraction or
Windows toolchain is kept. Dependencies are limited to CMake 3.20+ and a C++20
compiler:

```shell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

```shell
cmake --preset release        # -Wall -Wextra -Werror, the gate a change must pass
cmake --build --preset release
ctest --preset release
```

The optional adapter preserves schema-defined static camera depth of field
through RenderPlan JSON and Chronon3D camera evaluation; unsupported physical
lens metadata and animated camera DoF are rejected rather than discarded. A
schema-derived CTest gate checks camera-field transport parity.

To build the optional Chronon3d RenderPlan adapter and its authoring tests, use
an adjacent Chronon3d checkout and configure with the Chronon3d include root,
for example `cmake --preset release -DCHRONON3D_INCLUDE_DIR=../Chronon3d/include`
(and provide any consumer dependency include root via
`CHRONON3D_DEPENDENCY_INCLUDE_DIR`). Chronon3d's opt-in adapter lane builds the
adapter plus `chronon3d_motion_plan_adapter_tests`, which sends V2/V3 plans
through the canonical schema validator and typed decoder.

`dev-fast` and `release-fast` are the same two builds with ccache, mold and
Ninja, at 16 parallel jobs (they need ccache and mold on `PATH`, and about 16 GB
of RAM). They are the same gate, just seconds faster on a warm cache. Measured on
this tree, a full cold `-j16` rebuild peaks at roughly 2 GB of compiler RSS, so
the budget is a ceiling and not a requirement:

```shell
cmake --preset release-fast
cmake --build --preset release-fast
ctest --preset release-fast
```

The optional evaluation benchmark is built without changing the library or test
configuration:

```shell
cmake --preset release -DCHRONONMOTION_BUILD_BENCHMARKS=ON
cmake --build --preset release --target chrononmotion_eval_bench
./build/release/chrononmotion_eval_bench
```

It reports mean `evaluate()` time plus allocations and allocated bytes per frame
for the measured run; the allocation counters cover the steady-state loop, not
scene construction.

The result is one static library, `chrononmotion` (alias
`ChrononMotion::chrononmotion`), plus the test binaries below with
`CHRONONMOTION_BUILD_TESTS=ON`:

| binary | what it pins |
|---|---|
| `chrononmotion_smoke` | hierarchy/transform/curve/keyframe behaviour of the fork itself |
| `chrononmotion_primitives` | `Easing`, `Transform`, `Track`, and every channel modifier (window/influence/smooth semantics, rate, validation and export) |
| `chrononmotion_timeline` | `Layer`, `MotionScene`, lifetimes, determinism, validation |
| `chrononmotion_view` | `MotionPath`, `MotionPlane`/`PerspectiveView`, `CameraRig` |
| `chrononmotion_presets` | every preset in the list, by its defining property (the animated pivot, the lifetime-driven entrance/exit, the settles, the wave and secondary motion) |
| `chrononmotion_canary` | the whole primitive set on one timeline (see below), plus the annotated-pivot/lifetime composition and the channel-modifier/recipe composition |
| `chrononmotion_foundation` | Ray/Frustum, data-only lights, materials and CompiledScene |
| `chrononmotion_kinematics` | sub-frame evaluation and motion channel derivatives |
| `chrononmotion_authoring` | authoring bakers and the optional RenderPlan adapter |
| `chrononmotion_offline_golden` | fixed-value and repeated-run offline-baking checks |
| `chrononmotion_boundary` | the architectural guardrail (CTest script, not a test executable) |
| `chrononmotion_gltf` | static glTF/GLB geometry import and fail-closed validation |
| `chrononmotion_upstream_regressions` | adapted retained threepp math/core/animation regression assertions |

Upstream threepp test provenance and compatibility are tracked in
[THREEPP_TEST_COMPATIBILITY.md](docs/THREEPP_TEST_COMPATIBILITY.md); the file
separates retained API assertions from renderer-only and removed AnimationMixer tests.

Consumers include `chrononmotion/chrononmotion.hpp` or the individual headers, e.g.

```cpp
#include "chrononmotion/chrononmotion.hpp"

using namespace chrononmotion;

int main() {

    auto scene = Scene::create();
    auto group = Group::create();
    group->position.set(0.f, 0.f, -4.f);
    scene->add(group);

    // A Mesh keeps whatever geometry it is handed; no material is invented for it.
    auto geometry = BufferGeometry::create();
    auto mesh = Mesh::create(geometry);
    mesh->scale.set(1.f, 1.f, 1.f);
    group->add(mesh);

    // Motion path: sample it, then place the node at the sample.
    CatmullRomCurve3 path({Vector3(0.f, 0.f, 0.f), Vector3(1.f, 0.f, 0.f), Vector3(2.f, 1.f, 0.f)});
    Vector3 sample{};
    path.getPointAt(0.5f, sample);
    mesh->position.copy(sample);

    // Keyframes: the clip resolves its own duration from the last key.
    auto track = NumberKeyframeTrack::create("opacity", {0.f, 1.f}, {0.f, 1.f});
    auto clip = std::make_shared<AnimationClip>("intro", -1.f,
                                                std::vector<std::shared_ptr<KeyframeTrack>>{track});
    scene->animations.push_back(clip);

    scene->updateMatrixWorld();

    Vector3 where{};
    mesh->getWorldPosition(where);

    return 0;
}
```

## License

ChrononMotion is a derivative work of threepp (MIT) and, through it, of
[three.js](https://github.com/mrdoob/three.js) r129 (MIT) — the public API is
three.js', and substantial portions of the surviving sources are the ported
implementation. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
