# ChrononMotion — the motion-engine fork of threepp

This tree is a **cut**, not a rewrite. threepp's value for a motion system is its
scene graph, its transform math, its cameras, its curve library and its keyframe
tracks — the parts that take months to get right and that a motion compiler
cannot do without. Everything that only exists to put pixels on a screen or to
drive a game/simulation — the OpenGL and Vulkan renderers, the GLFW window, the
editor, physics, sensors, splats, post-processing, the loader zoo, the Python
bindings — was removed instead of carried.

Measured against the checkout this fork starts from (library sources only:
`include/` + `src/`):

| | upstream threepp | ChrononMotion |
|---|---|---|
| files | ~2.160 | 99 |
| lines | ~259.300 | ~14.100 (5.4%) |
| third-party deps at configure time | vcpkg features, FetchContent (GLFW, glslang, V-HACD, PhysX, pybind11, …) | none |
| build products | shared/static lib, editor, player, Python module, examples, tests | one static library + one smoke test |

The deletions are **uncommitted until they are committed**, so they are a diff,
not a fait accompli:

```shell
git status --short          # 2064 deletions, 28 modified, tests/ + this file untracked
git checkout -- <path>      # put any single file back
```

## What was kept

| module | lines | why it is here |
|---|---|---|
| `math/` | 8.241 | vectors, matrices, quaternion/Euler, `MathUtils`, `Interpolant` + interpolants, `Box2`/`Box3`, `Sphere`, `Plane`, `Line3`, `Triangle`, `Color`, `Spherical` — transform, anchor, parenting, slerp, bounds |
| `core/` | 2.725 | `Object3D` (the skeleton: parent/children, position/rotation/quaternion/scale, local+world matrices, `lookAt`, `localToWorld`/`worldToLocal`, `updateMatrixWorld`), `BufferGeometry`, `BufferAttribute`, `EventDispatcher`, `Assert`, `misc.hpp` |
| `extras/core` + `extras/curves` | 1.470 | `Curve`, `CurvePath`, `Path`, `Interpolations`, the eight concrete curves (CatmullRom, cubic/quadratic Bézier 2D+3D, Ellipse, Line, Spline) — motion paths and spatial interpolation |
| `animation/` | 703 | `AnimationClip`, `KeyframeTrack` (+ `Number`/`Vector`/`Quaternion`/`Color` tracks), `AnimationUtils` |
| `cameras/` | 450 | `Camera`, `PerspectiveCamera`, `OrthographicCamera` — 2.5D, parallax, depth, camera moves |
| `objects/` | 224 | `Group` (null/parent container) and `Mesh` (geometry + materials), isolated |
| `materials/` | 119 | one abstract `Material` base — identity, version, side, opacity, alpha |
| `scenes/` | 51 | `Scene` as a light root container |
| `constants.hpp` | 63 | the six enums the above actually use |

## What was removed

**Wave 1 — the engine halves.** `renderers/` (OpenGL + Vulkan + shaders, 75k
lines), `loaders/` (glTF, OBJ, FBX, COLLADA, STL, USD, URDF/xacro, SVG,
DDS/HDR/EXR, …), `geometries/`, `materials/` (the ~40-material zoo + capability
mixins), `lights/`, `helpers/`, `utils/`, `textures/`, `canvas/`, `controls/`,
`input/`, `postprocessing/`, `splats/`, `audio/`, `extras/` (physx, terrain,
vegetation, sensors, road, imgui, kinematics, pointcloud, fauna, uav, conveyor,
effects), `src/external/` (glad, GLFW, earcut, meshoptimizer, miniaudio, libwebp,
nlohmann, pugixml, quickhull, stb), `src/shaders/`, `src/resources/`,
`examples/`, `tests/` (upstream's Catch2 suite), `apps/` (editor + player),
`python/`, `editor-core/`, `editor-wheel/`, `doc/`, `geodata/`, `scripts/`,
`.github/`, `cmake/`, `vcpkg.json`, `pyproject.toml`, `CITATION.cff`.

**Wave 2 — the renderer residue inside what survived.** Once the renderer was
gone, a second pass removed the code paths that could no longer be fed, plus the
renderer vocabulary that came along with them:

* **Morph targets** — `BufferGeometry`'s `morphAttributes_`,
  `morphTargetsRelative`, `getMorphAttribute`/`getOrCreateMorphAttribute`/
  `getMorphAttributes` and the morph branches inside `computeBoundingBox`,
  `computeBoundingSphere` and `toNonIndexed`. `Mesh` no longer has morph
  influences at all, so nothing could ever fill these. `groups`/`GeometryGroup`
  stayed: "which index range uses which material" is geometry semantics.
* **GPU upload state** — `BufferAttribute::updateRange`, `setUsage`/`getUsage`
  and its `DrawUsage usage_` member, plus `InstancedBufferAttribute` (a vertex
  attribute divisor for an `InstancedMesh` that no longer exists). `DrawRange`
  and `BufferGeometry::drawRange`/`setDrawRange` went too — a draw limiter whose
  only consumer was `Mesh`'s deleted raycast.
* **`core/Clock`** — a wall-clock stopwatch. A frame-deterministic motion engine
  must not read a clock, and nothing called it but the umbrella header.
* **`math/` renderer vocabulary with no consumer** — `Frustum` (culling),
  `Ray` (picking, which existed for the deleted `Raycaster`), `Lut` (texture
  colour lookup), `SphericalHarmonics3` (environment-map lighting probes),
  `Capsule`/`Cylindrical` (collision/coordinate helpers), `ImprovedNoise`
  (procedural terrain/vegetation noise), `Rng` (an unreferenced PRNG).
  `Ray::intersectsPlane`, `Sphere::intersectsPlane` and friends went with `Ray`.
* **`constants.hpp`** — from 273 to 63 lines: `CullFace`, `ShadowMap`,
  `ShadowMapConfig`, `Blending`, `BlendEquation`, `BlendFactor`, `DepthFunc`,
  `CombineOperation`, `ToneMapping`, `Mapping`, `TextureWrapping`, `Filter`,
  `Type`, `Format`, `Loop`, `DepthPacking`, `NormalMapType`, `StencilOp`,
  `StencilFunc`, `DrawUsage`, `as_integer`, the flat/smooth shading and
  triangle-draw-mode constants, and the GPU read/copy usage constants were all
  only ever read by deleted code. Kept: `Side`, `Interpolation`, `Ending`,
  `AnimationBlendMode`, `ColorSpace`.
* **Other no-caller API** — `Material::copyInto` (its callers were `clone` and
  `copyCompatibleFrom`, both already gone), `ObjectWithMaterials::replaceMaterial`
  (a `setMaterial` duplicate once `copyInto` went), and the `Boolean`/`String`
  keyframe tracks (never included, and neither has a numeric interpolant, so no
  evaluator could sample them).

Everything else in `math/` was kept deliberately, even where nothing references
it today: it is small, self-contained and plausible for a motion/anchor/bounds
engine to need. `Box2` is the one file with literally no consumer — kept by
decision, since a 2D AABB is the natural anchor/text-bounds box.

## Behaviour deltas against upstream

These are the changes that are more than a deletion; each one is a deliberate
narrowing of the API.

**`Object3D` — stripped to the node.**
Gone: `RenderCallback`, `modelViewMatrix`, `normalMatrix`, `Layers`/`layers`,
`castShadow`, `receiveShadow`, `frustumCulled`, `autoLod`, `renderOrder`,
`userData`, `onBeforeRender`/`onAfterRender`, `raycast()`, `material()` and
`materialAs<T>()` (plus the `Material`/`Raycaster`/`Intersection` forward
declarations and the `<any>`/`<optional>` includes they needed).
Kept: `geometry()` — it is how `Mesh` reports its buffer and how
`Box3::expandByObject` measures a node, and it is not renderer-specific.
`lookAt()` still detects a `Camera` by type (a camera turns its local **-Z**
toward the target, a plain node turns its local **+Z**); the `Light` half of that
test is gone with `lights/`. `copy()` and the move constructor no longer carry
layers/shadow/culling/render-order/userData/render callbacks, and `copy()` now
copies `animations` (the clip list belongs to the node).

**`Mesh` — geometry + materials, nothing else.**
`raycast()` and its bounding-volume hooks, the `ObjectWithMorphTargetInfluences`
base and the skinning/instancing siblings (`SkinnedMesh`, `InstancedMesh`) are
gone. Constructing a `Mesh` without a material **no longer invents a
`MeshBasicMaterial`**: it owns zero materials and `material()` returns nullptr.
`ObjectWithMaterials::material()` was also made null-safe — upstream called
`materials_.front()` on a vector that could be empty, which is undefined
behaviour, and this fork made that reachable by removing the implicit default.

**`Material` — an abstract base, not a material system.**
What survives: `id`, `name`, `side`, `opacity`, `transparent`, `visible`,
`alphaTest`, `uuid()`, `version()`/`needsUpdate()`, `dispose()`, `type()`,
`as`/`is`. Gone: the `MaterialValue` variant, `setValues`/`setValue`, `setUuid`,
`clone()`/`createDefault()`, `copyCompatibleFrom`, `copyInto`, blending, stencil,
clipping planes, depth state, polygon offset, `defaultAttributeValues`,
`Uniform`, textures and every capability mixin.

**`Scene` — a root, not a compositor.**
`Background`, `environment`, the `Fog`/`FogExp2` variant, `overrideMaterial` and
`autoUpdate` are gone (`Fog`, `FogExp2`, `CubeTexture`, `Texture` with them).
`Scene` gained `createDefault()`, so `scene->clone()` returns a `Scene` rather
than a bare `Object3D` as it silently did upstream.

**Animation — the data model, not the runtime.**
`AnimationMixer`, `AnimationAction`, `PropertyBinding`/`PropertyMixer` and
`MaterialAnimationProxy` are **removed**: binding a clip to an object's
properties by traversing a name path is exactly what Chronon's own evaluator
does, and keeping two authorities for "what value does this property have at
frame N" is the thing this fork exists to avoid. What remains is the
representation: clips, tracks, times/values, interpolation modes, `makeAdditive`,
`shift`/`scale`/`trim`/`optimize`, and `math/Interpolant` for sampling.
Two consequences worth knowing:
* `AnimationClip::findByName(const Object3D&, name)` no longer starts with
  `if (!object.geometry()) return nullptr;`. That guard (upstream) made a
  `Group` or `Scene` — the nodes a motion graph is built from — report no clips
  at all.
* `Interpolant` no longer declares `friend class AnimationAction`/`AnimationMixer`.
  With no mixer, `evaluate()` is the whole sampling contract.

**Math — same maths, minus the instance branches and the renderer helpers.**
`Box3::expandByObject` lost its `InstancedMesh` fast path (a node is measured
through the geometry it carries). `Color.cpp` replaced threepp's
`utils::parseInt`/`parseFloat` with two local helpers that behave the same way
(0 on an unparseable capture). The files listed under wave 2 are gone; what is
left is dependency-clean and compiles without any object header.

**Warnings.** `math/MathUtils.cpp` (`-Wparentheses`, UUID bit masking) and
`animation/KeyframeTrack.cpp` (an `unsigned` loop index against an `int` write
index) were cleaned up, and `AnimationClip`'s constructor now initialises members
in declaration order, so the tree builds clean under `-Wall -Wextra -Werror`.

## The motion model (landed)

The fork above is the *maths* half. `include/threepp/motion/` is the *motion*
half a deterministic compiler samples from — eleven headers, eight translation
units, no new dependency, and no content machinery:

| file | responsibility |
|---|---|
| `motion/Content.hpp` | `ContentRef` / `ContentKind` / `ContentMetrics`: the opaque handle to Chronon's content, plus the neutral rectangle that placement needs |
| `motion/Easing.hpp` | the timing curve — linear, ease in/out/in-out, custom cubic Bézier, overshoot, bounce, elastic |
| `motion/Transform.hpp` | Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation quaternion, Opacity |
| `motion/Track.hpp` | keyframes with per-segment easing; scalar and vector lerp, quaternion slerp |
| `motion/Layer.hpp` | `FrameRange`, `LayerTracks`, `Layer` — identity, parent, content, lifetime, animated pose |
| `motion/MotionScene.hpp` | `MotionTime`, `LayerState`, `MotionScene` — hierarchy and deterministic frame/PTS evaluation |
| `motion/MotionPath.hpp` | line / quadratic and cubic Bézier / Catmull-Rom, arc-length sampling, orient-to-path |
| `motion/MotionPlane.hpp` | `MotionPlane`, `ScreenPlane`, `PerspectiveView` — 2.5D planes, Z depth, projection, parallax |
| `motion/CameraRig.hpp` | `CameraPose`, `CameraRig` — a perspective camera plus dolly/pan/tilt/orbit/roll/look-at/animated FOV |
| `motion/Presets.hpp` | the preset list, written as keyframe authoring on the primitives above |
| `motion/Motion.hpp` | the umbrella include, re-exported from `threepp/threepp.hpp` |

### The composition rules, stated once

* **Transform.** `local = T(position) · R · S(scale) · T(-anchor)` with
  `R = orientation · eulerToQuaternion(rotation)`. The anchor is the single
  pivot for rotation *and* scale, and the point `p` lands at
  `position + R·S·(p - anchor)`.
* **Channels.** `position`/`anchor`/`scale`/`rotation`/`orientation`/`opacity`
  are independently keyed. A channel with keys overrides the layer's static
  pose; the un-keyed channels keep it. That is what lets one preset shape a whole
  transform and another touch a single axis.
* **The pivot is a channel too.** `anchor` is keyed like anything else, so the
  point a layer rotates and scales about can travel over time instead of being
  fixed at author time: `presets::anchorTo` keys it and the transform composes
  against whatever the track sampled at that frame.
* **Lifetime.** `Layer::lifetime` (`inFrame`/`outFrame`) gates a layer and its
  whole subtree at frame-evaluation time. `presets::lifetimeWindows` derives the
  arrival and departure windows from those two frames and
  `presets::lifetimeSlide` keys them, so an in/out animation is tied to the
  declared lifetime rather than to frames an author picked by hand. A window the
  lifetime cannot hold is reported absent and left un-keyed — never moved to a
  frame the layer is not alive for.
* **Orientation is a quaternion, not Euler.** It is slerped, and `slerpQuaternions`
  fixes the antipodal sign, so an orientation track never takes the long way
  through ±180°. `rotation` stays XYZ Euler because that is what an author edits.
* **Evaluation.** `MotionScene::evaluate(frame)` samples every layer at
  `frame / fps` and composes world matrices parents-first, so a Null layer's
  motion propagates to everything under it and a hidden Null hides its subtree.
  It is a pure function of `(scene, frame, fps)`: the same frame always produces
  the same matrices, whatever was evaluated before it. `MotionTime::atPts`
  rounds, so a PTS one ulp below an exact frame still lands on that frame.
* **Text and image are the same type.** `MotionPlane` does not know or care what
  a layer draws — text planes and image planes are *the same struct*, driven
  through the same `Transform`. The only difference is the opaque `ContentRef`.
  There is no separate text-animation path to drift out of sync.
* **Parallax is automatic.** `PerspectiveView::pixelsPerUnit(distance)` shrinks
  with distance, so the same camera move shifts a near plane more than a far one
  and a dolly enlarges it more. No parallax is authored anywhere.

### What the guardrail still guarantees

The boundary is unchanged: `motion/` mentions no shaping, no font stack, no
raster and no decoder. `ContentRef` is an id, a kind, and a rectangle — so a
system that anchors and places a text asset without knowing what it is remains
the only thing this tree can do.

### What the presets are, and are not

`presets::` holds no engine. Every preset writes keyframes into a `Layer`'s (or a
`CameraRig`'s) channels, so a preset is a starting point a caller keeps editing,
never a sealed animation. `cameraShake` is deterministic too: its offsets come
from decaying whole sine cycles, not from an RNG, because a frame that shakes
differently on a re-render is not a frame a compiler can promise. (Whole cycles
and a decay that reaches zero also mean the shake starts and resolves exactly on
the base pose, with no pop at either end.)

## Build

```shell
cmake --preset dev        # Debug + tests
cmake --build --preset dev
ctest --preset dev

cmake --preset release    # Release, warnings as errors — the gate
cmake --build --preset release
ctest --preset release
```

`dev-fast` and `release-fast` are those same two configurations built with
ccache, mold and Ninja at 16 jobs (ccache and mold must be on `PATH`). They are
for running the gate repeatedly without paying for a full rebuild; the plain
presets stay dependency-free.

The library is one target, `chrononmotion` (alias `ChrononMotion::chrononmotion`),
with `include/` as its only include path and no dependency to fetch. Consumers
link it and include `threepp/threepp.hpp` (the umbrella was trimmed to the
keep-set and then extended with `motion/Motion.hpp`) or the individual headers.

The compile-time contract is split across six binaries:

* `chrononmotion_smoke` pins the fork itself — hierarchy and world transforms,
  `localToWorld`/`worldToLocal`, the camera vs node `lookAt` conventions, the
  perspective projection term, curve endpoints/`getPoints`/arc length/`CurvePath`
  traversal, keyframe interpolation and clamping, clip duration resolution,
  geometry bounds, the empty-material contract, `clone()` through
  `createDefault`, and parent/child detach.
* `chrononmotion_primitives` pins `Easing`, `Transform` and `Track`: curve
  endpoints and the overshoot that must still land on 1, the anchor as the fixed
  point of *both* scale and rotation, `orientation` composing on top of
  `rotation`, shortest-path blending across ±180°, per-segment easing, discrete
  holds, and sampling that is a pure function of time.
* `chrononmotion_timeline` pins `Layer` and `MotionScene`: a channel with keys
  overriding the static pose, the closed and open `FrameRange`, parents-first
  world composition, a hidden Null hiding its subtree, multiplicative world
  opacity, frame/PTS equivalence, evaluation that does not depend on evaluation
  history, and `validate()` reporting unknown parents, self-parenting and cycles
  without hanging.
* `chrononmotion_view` pins `MotionPath`, `MotionPlane`/`PerspectiveView` and
  `CameraRig`: waypoint interpolation, arc-length sampling, orient-to-path, the
  anchor as a plane pivot, projected size scaling with 1/distance, automatic
  parallax, behind-camera rejection, and every camera move.
* `chrononmotion_presets` pins every preset in the list by its defining property
  — where it starts, where it ends, and what makes it that preset rather than a
  neighbour (an overshoot that actually overshoots, a shake that always resolves
  where it started, a stagger whose layers are actually offset).
* `chrononmotion_canary` pins the whole primitive set at once, on the canary
  composition described in that file, plus a second composition for the two
  capabilities the first does not exercise on its own — a pivot that travels
  while the layer turns about it, and an entrance and exit keyed from the
  layer's declared lifetime.

`chrononmotion_boundary` runs separately and is the architectural guardrail.

## The boundary this tree sits on

The rule, stated once:

> **Chronon owns content. ChrononMotion owns space and time.**

```text
        Chronon                                  ChrononMotion
   (content authority)                        (motion authority)
──────────────────────────                 ──────────────────────────
TextAsset / ImageAsset / VideoAsset        Layer
TextStyle                                  parent / anchor
font resolution                            position / rotation / scale
HarfBuzz shaping, ICU/bidi                 transform tracks, opacity track
layout, line breaking                      lifetime (in/out frame)
bounds and glyph metrics                   timeline, camera
rasterization, glyph atlas                 frame → value compilation
──────────────────────────                 ──────────────────────────
                     │                              │
                     └──────────► AssetId ──────────┘
                            + ContentMetrics
                            + compiled world matrix / opacity
```

ChrononMotion therefore must not grow a font stack, a text shaper, a glyph
atlas, a text layout engine, an image decoder or a rasterizer — and it must not
grow a second `TextStyle`, a second definition of "bounds", or a second `Text`
type. If it needs one of those, the answer is a smaller `ContentRef`, not a copy
of the content engine.

The measurable form of the rule is the architectural guardrail:

```shell
cmake -DSOURCE_DIR=$(pwd) -P tests/check_motion_boundary.cmake
```

`tests/check_motion_boundary.cmake` (run by CTest as `chrononmotion_boundary`,
and by CI on every push) scans **code, not prose** — comments are blanked first,
so a doc comment may name a library this tree is forbidden to *use* — and fails on
any reference to text/shaping (`harfbuzz`, `freetype`, `fribidi`, `mtsdf`,
`glyph`, `typeface`, `font_cache`, `shaping`, `text_layout`, `kerning`, `bidi`,
font extensions), media decoding (`stb_image`, `libpng`, `jpeglib`, `libwebp`,
`ffmpeg`/`libavcodec`, `nvdec`/`nvenc`), raster/GPU (`blend2d`, `skia`, `cairo`,
`vulkan`, `opengl`, `glsl`, `shader`, `swapchain`, `glfw`), or a configure-time
dependency (`find_package`, `FetchContent`, `pkg_check_modules`, `vcpkg`). It also
asserts the library target links nothing. A line can opt out with a reasoned
`boundary-guardrail: allowed` comment; that is a design decision, not a
convenience.

What the guardrail guarantees today is a **property**, not a hope: this tree is
verified to build and pass its tests with zero content machinery. `math/`, `core/`
and `motion/` all mention none of it — and `motion/Content.hpp` is the reason the
last one can: anchoring a text asset needed an id and a rectangle, not a font.
(The only hit in the whole tree before the comment-stripping pass was the word
"rasterizes" in one `Material` comment.)

## Deliberately not done yet

1. **Mesh layers, lights and real 3D objects.** The model stops at text planes,
   image planes, nulls and a camera. `MeshLayer`, lights and true 3D geometry are
   the step *after* this set, not part of it.
2. **`Object3D` and `Layer` do not share a graph.** `Object3D` is the three.js
   node the kept maths, curves and cameras are written against; `Layer` is the
   motion model. `MotionScene` composes world matrices itself and a `CameraRig`
   writes its sampled pose into a `PerspectiveCamera` node. Each is the single
   authority for its own role today, but unifying them is a real design decision,
   not a rename, and it is deliberately not taken here.
3. **glTF.** Deferred on purpose. `GLTFLoader` is the single most expensive item
   on the keep-list: it pulls the material zoo, textures, geometries and
   `MaterialVariants` back in. The cheap version of the same capability — a small
   glTF/GLB reader that produces `BufferGeometry` + `Mesh` with no material
   system — is the better next step, and `Mesh`/`BufferGeometry` are already the
   containers it would fill.
4. **Geometry primitives.** `geometries/` (Box, Plane, Sphere, …) was removed
   with the rest. A `Mesh` can be built by hand from a `BufferGeometry` today
   (see the smoke test), but a handful of primitives is probably worth
   re-importing now that the layer model has settled.
5. **`ContentMetrics` is not fingerprinted.** It carries a size and an anchor but
   nothing binds them to the asset they measured, so this side cannot detect a
   stale measurement when the content changes. The fingerprint belongs to
   Chronon; carrying it across `ContentRef` is the fix.
6. **`Transform::anchor` is set, not derived.** `MotionPlane::applyTo` writes the
   pivot the plane computed; nothing recomputes it from the bounds. An author who
   changes a layer's content size therefore has to re-apply the plane. Deriving
   the anchor from a fingerprinted `ContentMetrics` would close that gap.
7. **No serialization.** The loader/exporter pair is gone, so `uuid()`/`setUuid()`
   and `Layers`-style filtering have no consumer yet. Any per-layer attachment
   the engine needs later should be a typed field, not a return of the
   `userData` bag.
8. **Naming.** The namespace is still `threepp` and the include prefix is still
   `threepp/` — deliberately, so a future upstream diff stays readable. Renaming
   to `chrononmotion/` is a mechanical change and should be its own commit.
9. **Upstream's test suite.** The Catch2 tests were deleted with `tests/`. The
   fork's own behaviour is now covered by one smoke binary plus the four motion
   binaries and the boundary guardrail; re-importing the upstream tests for
   `math/`, `core/` and `animation/` would still give the fork real regression
   depth.
