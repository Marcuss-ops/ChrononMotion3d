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
99 files), and nothing is fetched at configure time.

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

This is a fresh cut. The library builds, the smoke test passes, and the shape of
the "motion engine" half is settled — but the node model is still three.js, not
yet the layer node (`anchorPoint`, `inFrame`/`outFrame`, `opacity`, per-property
tracks) the compiler will sample from, and there is no glTF import yet. See the
deferred list at the end of [CHRONONMOTION.md](CHRONONMOTION.md).

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
`ChrononMotion::chrononmotion`), plus `chrononmotion_smoke` with
`CHRONONMOTION_BUILD_TESTS=ON`. Consumers include `threepp/threepp.hpp` or the
individual headers, e.g.

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
