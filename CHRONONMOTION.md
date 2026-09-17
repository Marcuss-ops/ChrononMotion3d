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

## Build

```shell
cmake --preset dev        # Debug + smoke test
cmake --build --preset dev
ctest --preset dev

cmake --preset release    # Release, warnings as errors — the gate
cmake --build --preset release
ctest --preset release
```

The library is one target, `chrononmotion` (alias `ChrononMotion::chrononmotion`),
with `include/` as its only include path and no dependency to fetch. Consumers
link it and include `threepp/threepp.hpp` (the umbrella was trimmed to the
keep-set) or the individual headers.

The compile-time contract is pinned by `tests/motion_smoke.cpp`: hierarchy and
world transforms, `localToWorld`/`worldToLocal`, the camera vs node `lookAt`
conventions, the perspective projection term, curve endpoints/`getPoints`/arc
length/`CurvePath` traversal, keyframe interpolation and clamping, clip duration
resolution, geometry bounds, the empty-material contract, `clone()` through
`createDefault`, and parent/child detach.

## Deliberately not done yet

1. **`Object3D` → `LayerNode`.** The node still speaks three.js. The target is a
   node that owns `anchorPoint`, `opacity`, `inFrame`/`outFrame`, `is3D`,
   `parentId` and the track set (`positionTrack`, `anchorTrack`, `scaleTrack`,
   `orientationTrack`, `rotationX/Y/ZTrack`, `opacityTrack`) — i.e. a node whose
   transform is *sampled from tracks at a frame*, not mutated by a render loop.
2. **The deterministic evaluator.** `MotionEvaluator(frame, pts)` over the
   `AnimationClip`/`KeyframeTrack` representation now in the tree, with the
   frame→value mapping a function of the frame number only.
3. **glTF.** Deferred on purpose. `GLTFLoader` is the single most expensive item
   on the keep-list: it pulls the material zoo, textures, geometries and
   `MaterialVariants` back in. The cheap version of the same capability — a small
   glTF/GLB reader that produces `BufferGeometry` + `Mesh` with no material
   system — is the better next step, and `Mesh`/`BufferGeometry` are already the
   containers it would fill.
4. **Geometry primitives.** `geometries/` (Box, Plane, Sphere, …) was removed
   with the rest. A `Mesh` can be built by hand from a `BufferGeometry` today
   (see the smoke test), but a handful of primitives is probably worth
   re-importing once the node/layer model settles.
5. **Naming.** The namespace is still `threepp` and the include prefix is still
   `threepp/` — deliberately, so a future upstream diff stays readable. Renaming
   to `chrononmotion/` is a mechanical change and should be its own commit.
6. **Upstream's test suite.** The Catch2 tests were deleted with `tests/`; the
   keep-set's behaviour is currently covered by one smoke binary. Re-importing
   the upstream tests for `math/`, `core/` and `animation/` would give the fork
   real regression depth.
7. **No serialization, no layers, no userData.** The loader/exporter pair is
   gone, so `uuid()`/`setUuid()` and `Layers`-style filtering have no consumer
   yet. Any per-node attachment the engine needs later should be a typed field,
   not a return of the `userData` bag.
