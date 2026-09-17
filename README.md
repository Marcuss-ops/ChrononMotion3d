# ChrononMotion

**A motion engine, forked from [threepp](https://github.com/markaren/threepp).**

ChrononMotion keeps the half of threepp that a deterministic motion/transform
compiler needs — the scene graph, the transform and quaternion/curve maths, the
cameras, the keyframe tracks — and drops the half that exists to put pixels on a
screen or to drive a game and a simulation. No renderer, no window, no physics,
no sensors, no loaders, no python, no material zoo.

```text
threepp (upstream)                     ChrononMotion
────────────────────────────────       ─────────────────────────────────
math/ core/ animation/ cameras/        kept, and largely untouched
extras/curves                          kept
objects/{Group,Mesh}, scenes/Scene     kept, narrowed
materials/ (40 materials + mixins)     one abstract Material base
renderers/ loaders/ geometries/        deleted
lights/ helpers/ utils/ textures/      deleted
canvas/ controls/ input/ audio/        deleted
postprocessing/ splats/ extras/        deleted
apps/ examples/ tests/ python/         deleted
```

Roughly **5.4% of the upstream library sources survive** (~14.1k of ~259.3k lines,
99 files), and nothing is fetched at configure time. What grows back on top is the
half threepp does not have: the deterministic motion model in
`include/threepp/motion/` — see [The motion model](#the-motion-model).

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
deliberately not done yet. Read it before changing anything here.

## Status

The motion model is landed on top of the fork. The three.js half still provides
the maths, the curves, the cameras and the keyframe vocabulary; `threepp/motion/`
is the layer/transform/timeline/camera model a deterministic compiler samples
from. glTF import, geometry primitives and the namespace rename are still
deferred — see the list at the end of [CHRONONMOTION.md](CHRONONMOTION.md).

## The motion model

```text
Transform          Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation, Opacity
Track              keyframes, per-segment easing, deterministic sampling
Easing             linear / ease in-out / custom cubic Bézier / overshoot / bounce
Layer              identity, parent, content, in/out lifetime, animated transform
MotionScene        hierarchy (Parent, Null) + deterministic frame/PTS evaluation
MotionPath         line / cubic Bézier / Catmull-Rom + orient-to-path
MotionPlane        2.5D text and image planes, Z depth, perspective + parallax
CameraRig          perspective, look-at, dolly/pan/tilt/orbit/roll, FOV/focal
Presets            the YouTube presets, built on the primitives above
```

The presets cover the list a text/image promo actually needs: slide in/out on
all four sides, push/pull on Z, scale pop and overshoot, fade + slide, fade +
scale, flip X/Y, card turn, 3D tilt, spin Z, spin XYZ, parallax entrance, camera
push/pull/pan/orbit/shake, floating hover, drift, arc and path motion, staggered
entrance and parent-group reveal.

The composition rules, in one place:

* a layer's local matrix is `T(position) · R · S(scale) · T(-anchor)`, with
  `R = orientation · euler(rotation)` — the anchor is the single pivot for both
  rotation and scale;
* a channel with keys overrides the layer's static pose, so a preset can shape a
  whole transform or touch one axis;
* `evaluate(frame)` is a pure function of `(scene, frame, fps)`: the same frame
  always produces the same matrices, whatever was evaluated before it;
* **text and image planes are the same type.** The only difference between a text
  layer and an image layer is the opaque `ContentRef` they carry, so their
  transform, anchor and parallax semantics cannot drift apart.

```cpp
#include "threepp/threepp.hpp"

using namespace threepp;
using namespace threepp::motion;

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

## Build

No dependencies. CMake 3.20+ and a C++20 compiler:

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

The result is one static library, `chrononmotion` (alias
`ChrononMotion::chrononmotion`), plus the test binaries below with
`CHRONONMOTION_BUILD_TESTS=ON`:

| binary | what it pins |
|---|---|
| `chrononmotion_smoke` | hierarchy/transform/curve/keyframe behaviour of the fork itself |
| `chrononmotion_primitives` | `Easing`, `Transform`, `Track` |
| `chrononmotion_timeline` | `Layer`, `MotionScene`, lifetimes, determinism, validation |
| `chrononmotion_view` | `MotionPath`, `MotionPlane`/`PerspectiveView`, `CameraRig` |
| `chrononmotion_presets` | every preset in the list, by its defining property |
| `chrononmotion_canary` | the whole primitive set on one timeline (see below) |
| `chrononmotion_boundary` | the architectural guardrail |

Consumers include `threepp/threepp.hpp` or the individual headers, e.g.

```cpp
#include "threepp/threepp.hpp"

using namespace threepp;

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
