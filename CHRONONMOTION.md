# ChrononMotion — the Linux-only motion-engine fork of threepp

Linux is the only supported deployment target. Platform-specific compatibility
branches for other operating systems are intentionally not part of this tree.

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
| files | ~2.160 | 125 |
| lines | ~259.300 | ~17.700 (6.8%) |
| third-party deps at configure time | vcpkg features, FetchContent (GLFW, glslang, V-HACD, PhysX, pybind11, …) | none |
| build products | shared/static lib, editor, player, Python module, examples, tests | one static library + twelve test executables + separately registered boundary gate |

The deletions are **committed**, so this file is the ledger for them rather than
a pending diff. Every removed file is still reachable from the history, and any
single one can be put back to inspect it:

```shell
git log --oneline --diff-filter=D --name-only   # every deleted path
git show <pre-fork>:<path>                      # its bytes
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
| `geometries/` | 579 | `BoxGeometry`, `PlaneGeometry`, `SphereGeometry` — re-imported after the cut: plain `BufferGeometry` builders with no material, texture or loader behind them |
| `scenes/` | 51 | `Scene` as a light root container |
| `constants.hpp` | 63 | the six enums the above actually use |
| `math/Ray`, `math/Frustum` | — | dependency-free spatial queries for bounds, picking and future culling |
| `motion/Lighting`, `motion/CompiledScene` | — | data-only lighting and the evaluated renderer boundary |
| `materials/Descriptors` | — | Unlit/Lambert/Emissive material intent without renderer state |

## What was removed

**Wave 1 — the engine halves.** `renderers/` (OpenGL + Vulkan + shaders, 75k
lines), `loaders/` (glTF, OBJ, FBX, COLLADA, STL, USD, URDF/xacro, SVG,
DDS/HDR/EXR, …), `geometries/` (all but the three re-imported below),
`materials/` (the ~40-material zoo + capability
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
* **`math/` renderer vocabulary with no consumer** — `Lut` (texture colour
  lookup), `SphericalHarmonics3` (environment-map lighting probes),
  `Capsule`/`Cylindrical` (collision/coordinate helpers), `ImprovedNoise`
  (procedural terrain/vegetation noise), and `Rng` (an unreferenced PRNG) stay
  removed. `Ray` and `Frustum` were later reintroduced as small, dependency-free
  spatial primitives for the minimum + 1 foundation.
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
toward the target, a plain node turns its local **+Z**). Lighting is now represented
by ChrononMotion-owned data descriptors and is not part of the `Object3D` graph. `copy()` and the move constructor no longer carry
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

The fork above is the *maths* half. The minimum + 1 foundation extends it with
small spatial primitives and renderer-neutral scene descriptors. The source of
truth is the current ChrononMotion API, not a restored threepp runtime: upstream
algorithms were selectively adapted where useful, while ownership, lifetime and
scene compilation remain ChrononMotion-owned.

`MotionScene::compile()` is a snapshot boundary. It evaluates the scene at one
`MotionTime` and copies the resulting layer and light states into `CompiledScene`;
material and mesh descriptors are explicit data appended by the caller at that
boundary. It does not allocate GPU resources or invoke a renderer, and the snapshot is intended
for a future renderer or exporter to consume.

`include/chrononmotion/motion/` is the *motion* half a deterministic compiler
samples from — eleven headers, twelve translation units, no new dependency, and
no content machinery:

| file | responsibility |
|---|---|
| `motion/Content.hpp` | `ContentRef` / `ContentKind` / `ContentMetrics` / `MeasurementState`: the opaque handle to Chronon's content, the neutral rectangle placement needs, and the fingerprint that says which measurement of that content the rectangle came from |
| `motion/Easing.hpp` | the timing curve — linear, ease in/out/in-out, custom cubic Bézier, overshoot, bounce, elastic |
| `motion/Transform.hpp` | Position/Anchor/Scale XYZ, Rotation X/Y/Z, Orientation quaternion, Opacity |
| `motion/Track.hpp` | keyframes with per-segment easing; scalar and vector lerp, quaternion slerp; the channel modifiers (time warps: loop, posterize, delay, window — value filters: link, influence, smooth) |
| `motion/Layer.hpp` | `FrameRange`, `LayerTracks`, `Layer` — identity, parent, content, lifetime, animated pose |
| `motion/MotionScene.hpp` | `MotionTime`, `LayerState`, `MotionScene` — hierarchy and deterministic frame/PTS evaluation |
| `motion/MotionPath.hpp` | line / quadratic and cubic Bézier / Catmull-Rom, arc-length sampling, orient-to-path |
| `motion/MotionPlane.hpp` | `MotionPlane`, `ScreenPlane`, `PerspectiveView` — 2.5D planes, Z depth, projection, parallax |
| `motion/CameraRig.hpp` | `CameraPose`, `CameraRig` — a perspective camera plus dolly/pan/tilt/orbit/roll/look-at/animated FOV |
| `motion/Presets.hpp` | the preset list, written as keyframe authoring on the primitives above |
| `motion/Motion.hpp` | the umbrella include, re-exported from `chrononmotion/chrononmotion.hpp` |

`presets::` is the one part of that list implemented as several files, and the
split is by *family* rather than by size: `Presets_Common.cpp` holds the one
frame→seconds conversion and the direction/axis tables, then one translation unit
per family — `Presets_Place` (slides, depth, lifetime windows, 2.5D travel),
`Presets_Transform` (scale, opacity, pivot, orientation), `Presets_Motion` (paths,
hierarchies), `Presets_Camera` and `Presets_Environment` (coverage, horizon floor, ambient drift, aura, autofocus) — over a shared internal `Presets_detail.hpp`.
The three helpers in that header (`restOf`, `uniform`, `keyVector3`) exist
precisely because the families must key their curves *identically*; a per-family
copy is how one of them would quietly grow a different end-of-segment convention.
The public surface is unchanged: `motion/Presets.hpp` is still the whole list.

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
* **A measurement is labelled, and a re-measurement re-derives.** `ContentMetrics`
  carries a `fingerprint` — Chronon's digest of the content the rectangle was
  measured from — so "these numbers were taken from that content" is a fact a
  caller can check (`ContentRef::measurementState`) instead of an assumption.
  `MotionPlane::refresh(layer, remeasured)` rebuilds the plane when that digest
  changes — the anchor always, the aspect ratio too for a `fitted` plane — and
  writes it back to the layer. What it writes stays the *static* pose: a keyed
  `anchor` track still wins, exactly as it does over `applyTo`.
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
* **Modifiers are a stage, not a flag.** The modifiers on a channel are applied in
  two stages, and the stage a modifier belongs to is what it *does*, not the order
  it was added in. **Time warps** (`Loop`, `Posterize`, `Delay`, `Window`) move the
  instant the channel is read at. **Value filters** (`Smooth`, `Influence`) shape
  the value that comes back: `Smooth` runs innermost — it averages the raw channel
  across a symmetric window, so a linear run is left exactly where it was and a
  peak is pulled down — and `Influence` outermost, blending the result against the
  channel's own value at an origin instant. `Link` sits with the keys rather than
  with either stage: the borrowed delta is part of what "this channel's value"
  means, so a smoothing pass averages the borrowed motion together with the owned
  one. Rate follows value through both stages, exactly: averaging is linear and so
  commutes with differentiation (the reported rate is the mean of the rates), and
  an influence that has dialled a channel to zero reports zero velocity rather than
  the velocity its keys no longer deliver.
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
* **A documentary map vocabulary.** The map-sequence patterns sit on top of the
  same primitives, and none of them invents content: `radarPing` (an expanding
  target ping — one ring repeated by the channel's own `Loop`, so the reset at
  each boundary is instantaneous without ever keying the same instant twice),
  `areaReveal`, `extrudeRise`, `lineDraw` (a hairline, never a singular zero),
  `crosshairLock` (a reticle whose segments snap onto their own resting places),
  and the two camera moves `orbitalSlamZoom` (a dive curve on the descent) and
  `cameraDrift` (a slow orbit that never freezes) and `cameraOrbit`, the 2.5D move
  that swings `yaw` while it pitches `pitch` and never lets the target leave the
  frame. The geospatial `MapFlyoverBuilder` composes the ones that fit a map — a
  ping on the landed POI pin, an area overlay, a leader line, a reticle, a
  screen-space vignette and a dim — behind opt-in flags, so a plain flyover
  authors exactly the layers it always did.
* **A course is walked, not drawn.** A route between two places is the **great
  circle** between them (`greatCirclePoint`), not the straight line the
  projection makes of it, and so is a territory contour. Both are authored as a
  chain of soft glow sprites walked at **even arc length** (`authorGlowChain`):
  even spacing is what makes the chain read as one line however the projection
  bends, the sprite's falloff is wider than the step so neighbours composite, and
  the sprites light in sequence so the shape draws itself. A brightness wave then
  travels the chain and a brighter head rides the reveal front, so a drawn course
  keeps moving instead of dying on the map. The plan contract carries images and
  symbols, never paths — a curve has to be paid for in sprites. Whether the pixels
  look right is still the content owner's; this side only ever keys space and time.

### What the guardrail still guarantees

The boundary is unchanged: `motion/` mentions no shaping, no font stack, no
raster and no decoder. `ContentRef` is an id, a kind, a rectangle and an opaque
measurement fingerprint — so a system that anchors and places a text asset
without knowing what it is remains the only thing this tree can do. The
fingerprint is a string it never computes and never parses; it is Chronon's, and
this side only carries it. Any typewriter/cluster reveal must target Chronon-owned
stable element IDs through an additive Chronon3D contract; this engine never
splits text or recomputes advances.

### What the presets are, and are not

`presets::` holds no engine. Every preset writes keyframes into a `Layer`'s (or a
`CameraRig`'s) channels, so a preset is a starting point a caller keeps editing,
never a sealed animation. `cameraShake` is deterministic too: its offsets come
from decaying whole sine cycles, not from an RNG, because a frame that shakes
differently on a re-render is not a frame a compiler can promise. (Whole cycles
and a decay that reaches zero also mean the shake starts and resolves exactly on
the base pose, with no pop at either end.)

## Build

Linux is the only supported deployment target; CMake rejects other system names
at configure time. The build has no Windows toolchain branch or platform shim.

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
link it and include `chrononmotion/chrononmotion.hpp` (the umbrella was trimmed to the
keep-set and then extended with `motion/Motion.hpp`) or the individual headers.

The compile-time contract is split across twelve test executables plus the separately registered boundary gate:

* `chrononmotion_smoke` pins the fork itself — hierarchy and world transforms,
  `localToWorld`/`worldToLocal`, the camera vs node `lookAt` conventions, the
  perspective projection term, curve endpoints/`getPoints`/arc length/`CurvePath`
  traversal, keyframe interpolation and clamping, clip duration resolution,
  geometry bounds, the empty-material contract, `clone()` through
  `createDefault`, and parent/child detach. It also pins the re-imported
  primitives: `PlaneGeometry`/`BoxGeometry`/`SphereGeometry` vertex and index
  counts, the per-face groups, the bounds they produce, the retained construction
  parameters and the unit-length sphere normals.
* `chrononmotion_primitives` pins `Easing`, `Transform`, `Track`, every channel
  modifier and the content
  measurement: curve endpoints and the overshoot that must still land on 1, the
  anchor as the fixed point of *both* scale and rotation, `orientation` composing
  on top of `rotation`, shortest-path blending across ±180°, per-segment easing,
  discrete holds, sampling that is a pure function of time, and the measurement
  fingerprint's `Current`/`Stale`/`Unknown` verdict (including that a
  re-measured asset is still the *same* asset).
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
  where it started, a stagger whose layers are actually offset, a settle that
  lands *on* the resting pose, a wave whose lanes are actually out of phase, a
  follower that is where its source was one lag earlier).
* `chrononmotion_canary` pins the whole primitive set at once, on the canary
  composition described in that file, plus a second composition for the two
  capabilities the first does not exercise on its own — a pivot that travels
  while the layer turns about it, and an entrance and exit keyed from the
  layer's declared lifetime — and a third for the modifier stages and the recipe
  presets that sit on them (a work area that limits when a channel moves, one
  that softens the overshoot of a settle, a follower that borrows another
  layer's motion late and at half strength, and a wave sampled across a group).
* `chrononmotion_foundation` pins Ray/Frustum, data-only lights, material
  descriptors and the `CompiledScene` boundary.
* `chrononmotion_kinematics` pins sub-frame evaluation and motion channel
  derivative contracts.
* `chrononmotion_authoring` pins offline baker contracts and optional typed
  RenderPlan lowering, including deterministic camera/path adaptation.
* `chrononmotion_offline_golden` pins fixed expected samples and repeated-run
  stability for authoring-time solvers, PCM analysis/drivers, flow, and SVG paths.
* `chrononmotion_gltf` pins the static in-memory glTF/GLB geometry-only reader.
* `chrononmotion_upstream_regressions` adapts the retained threepp math/core/
  animation assertions at the pinned revision listed in the compatibility
  inventory.

`ChrononTemplate` is maintained as a separate sibling module. It consumes this
core's `MotionScene`, `Layer`, `ContentRef` and preset APIs, and is intentionally
not part of this library's source map or test target.

`chrononmotion_boundary` runs separately and is the architectural guardrail.

The optional `chrononmotion_eval_bench` target is enabled with
`-DCHRONONMOTION_BUILD_BENCHMARKS=ON`. It measures steady-state
`MotionScene::evaluate()` time, allocation count and allocated bytes per frame;
scene construction is outside the measured interval. The benchmark is a
consumer of the public compiled library, not part of the production data path.

## The boundary this tree sits on

The rule, stated once:

> **Chronon owns content and pixels. ChrononMotion owns space and time.**
>
> ChrononTemplate owns composition; RenderingGen owns editorial intent.

Motion3D receives only an opaque `ContentRef` plus Chronon-produced metrics and
fingerprint. It never receives source text, font data, glyph ids, cluster
positions, raster buffers or glow parameters that would require interpreting
content. Typewriter/cluster animation must remain a Chronon3D-owned capability;
the current layer ABI transports the complete content identity and transform,
not a locally invented per-character layout.

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

The same rule runs the other way along the sibling modules. ChrononMotion keeps
the six curves its own evaluation needs, because it is dependency-free and
samples every track itself; it does **not** keep a *catalogue* of named curves.
That catalogue is Chronon3d's — `chronon3d::Easing` with its quad/cubic/expo/
sine/back/elastic/bounce in/out/in-out set, the `CubicBezier` sampler and the
RenderPlan `easing` tokens — and the path operators (`Trim`, `WigglePath`,
`ZigZag`, `Twist`, `Repeater`, `Morph`) are Chronon3d's too. Adding a second
spelling of any of those here would give the family two authorities for one
vocabulary, which is the defect this whole boundary exists to prevent. The
motion half grows the things only it can own: the timeline, the hierarchy, the
channel stages and the camera.

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
verified to build and pass its tests with zero content machinery. `math/`, `core/`,
`geometries/` and `motion/` all mention none of it — and `motion/Content.hpp` is
the reason the last one can: anchoring a text asset needed an id, a rectangle and
an opaque fingerprint, not a font.
(The only hit in the whole tree before the comment-stripping pass was the word
"rasterizes" in one `Material` comment.)

## Pinned deduplication guarantees (Motion vs Chronon3D)

Three overlaps with the renderer were audited. In each case both sides keep
their own code — deleting either would break the boundary — and a test pins
that they cannot drift apart silently:

1. **Easing.** `motion::Easing` (11 kinds) evaluates; `chronon3d::Easing`
   (25 GLM-backed tokens) evaluates hand-authored plans. They meet only in
   the bake: `lowerToRenderPlan` samples Motion tracks at integer frames and
   emits dense `linear` keys, so no curve is ever re-spelled. Pinned by
   `tests/motion_renderplan_easing_parity.cpp`, which covers all 11 Motion
   kinds (including `circOut`) sample-for-sample against the lowered plan.
2. **Projection.** `motion::projectToScreen` (authoring: safe area, framing)
   and the renderer `project_world_point` contract (pixels) share the
   vertical-FOV pinhole law; the deliberate difference is the forward axis
   (-Z Motion, +Z render plan, mirrored by the lowering). Pinned by
   `tests/motion_projection_parity.cpp`, which compares both on identical
   translation-only poses with mirrored depth across viewports, FOVs, depths
   and points (< 0.02 px). Rotation-convention parity (quaternion vs euler
   order) is owned by the camera lowering's `chrononCameraEuler`, which
   serializes the inverse of Chronon3D's `glm::quat(euler)` constructor.
3. **Camera bake.** The rig-to-track bake is the only passage from the Motion
   camera to the renderer; nothing is re-derived downstream. Pinned by
   `tests/motion_camera_bake_roundtrip.cpp`: baked tracks equal rig samples
   (mirrored Z, FOV), a pure dolly keeps rotation constant, and lowering
   twice is bit-deterministic.

Two writers emit the same schema with different contracts, and that is
intentional: `RenderPlanLowering::lowerToRenderPlan` bakes drivers, per-key
easings and camera motion into linear samples, while
`RenderPlanEmission::renderPlanToJson` refuses what it cannot spell
(drivers, per-key easing) instead of baking. Use the lowering for Motion
scenes; use the emission writer for hand-authored typed plans.

## Deliberately not done yet

1. **Mesh layers and real 3D objects.** The model stops at text planes, image
   planes, nulls and a camera. Box, Plane and Sphere geometries are available to
   `Mesh`, and the minimum + 1 light/material descriptors are available, but a
   renderer-backed `MeshLayer` and GPU scene are intentionally still absent.
2. **`Object3D` and `Layer` do not share a graph.** `Object3D` is the three.js
   node the kept maths, curves and cameras are written against; `Layer` is the
   motion model. `MotionScene` composes world matrices itself and a `CameraRig`
   writes its sampled pose into a `PerspectiveCamera` node. Each is the single
   authority for its own role today, but unifying them is a real design decision,
   not a rename, and it is deliberately not taken here.
3. **Broad glTF is deferred; static geometry import is implemented.**
   `chrononmotion/assets/GltfReader.hpp` reads static glTF 2.0 JSON or GLB 2.0
   in memory and produces `BufferGeometry` + `Mesh` nodes with an explicit
   caller-supplied resolver for external buffers. It accepts triangle lists and
   a narrow POSITION/NORMAL/TEXCOORD_0 + index contract; it imports no materials
   and rejects animation, skinning, morph targets, extensions, sparse accessors,
   compression, and other primitive modes. See `docs/GLTF_READER.md`. The full
   upstream `GLTFLoader` remains intentionally absent.
4. **More geometry primitives.** Box, Plane and Sphere are re-imported as small,
   renderer-free `BufferGeometry` builders. The rest of upstream's geometry
   system (shape/extrusion and specialized primitives) remains deferred.
5. **No serialization.** The loader/exporter pair is gone, so `uuid()`/`setUuid()`
   and `Layers`-style filtering have no consumer yet. Any per-layer attachment
   the engine needs later should be a typed field, not a return of the
   `userData` bag.
6. **Upstream's test suite.** The Catch2 tests were deleted with `tests/`.
   The pinned inventory and compatibility classification are in
   `docs/THREEPP_TEST_COMPATIBILITY.md` at threepp revision
   `e2b165ece3904012d97a08029e0bd6cd5252892a`. Representative retained math,
   core, and animation assertions are now adapted into
   `tests/motion_upstream_regressions.cpp`; the inventory records source files,
   covered areas, and adaptations. This is a scoped regression port, not a
   mechanical import of every Catch2 case. AnimationMixer, Layers, Raycaster,
   Uniform, renderer-only cases, and removed math APIs remain intentionally
   excluded.

### Offline baking and RenderPlan boundary

The concrete static geometry import scope and its buffer, accessor, hierarchy,
size, and fail-closed rules are maintained in `docs/GLTF_READER.md`. The reader
accepts in-memory JSON/GLB and only invokes a caller resolver for external buffer
bytes; it never resolves paths or performs implicit file/network I/O.

`motion/OfflineBaking.hpp` exposes deterministic authoring-time Verlet chain and
2D rigid-body baking, analytic 2D two-segment IK, multi-segment 3D FABRIK IK with
a world-space pole vector and optional frame-sampled joint-position tracks,
normalized interleaved-PCM feature analysis and frame-sampled audio drivers,
measured row/column flow layout, and SVG path parsing/morph/repeater baking.
Solvers return ordinary samples/tracks/paths; they are not called during
rendering and the PCM API never decodes files or performs implicit I/O. Work,
geometry and sample budgets are validated, and
`tests/motion_offline_golden.cpp` pins fixed outputs plus repeat stability.
3D IK solves each target from the authored initial pose rather than carrying
solver state between frames, so seeking and rebaking remain deterministic.

`motion/RenderPlanLowering.hpp` is an opt-in adapter built only when a consumer
provides Chronon3d headers. It evaluates Motion at integer output frames and
lowers representable world transforms to typed RenderPlan v2/v3 data, explicitly
opting emitted three-component transforms into the plan's 3D projection contract.
Content, shape styling and opaque assets remain caller-supplied. Unsupported
anchors, shear, reflections, singular transforms, physical focal-length/f-stop
optics, animated camera DoF, and unsupported scene content fail closed. Static
camera DoF uses the schema-defined focus-distance/aperture/max-blur fields and
survives JSON emission, canonical decoding, and camera descriptor evaluation.
Camera rig poses and V3 static Merge/Morph path operators have dedicated adapter
coverage; the sibling adapter tests send projected JSON through canonical schema
validation and typed decoding, including semantic Morph topology rejection and
static DoF camera descriptor preservation.
Chronon3d consumes RenderPlan data; it does not run Motion solvers or analyze PCM
in its frame path.

The producer-to-consumer canary is executable across both repositories. Build
`chrononmotion_authoring` with the optional Chronon3D adapter, build
`chronon3d_cli`, then run:

```shell
tools/check_render_plan_consumer_canary.sh \
  build/adapter/chrononmotion_authoring \
  ../Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
```

It asks the C++ Motion test to emit its deterministic V3 path-and-particle
scene, validates those bytes with Chronon3D's canonical decoder, then renders
the same scene through the software video lane. It checks the decoded video
dimensions, duration and frame changes. The optional output-directory argument
keeps the plan, validation report, MP4 and decoded-frame hashes for inspection.

### Closed since the fork

Three items this file used to list as open are now landed and pinned, so they are
recorded here instead of above:
* **`ContentMetrics` is fingerprinted** (`ContentMetrics::fingerprint`,
  `ContentRef::measured`, `ContentRef::measurementState`). An un-fingerprinted
  ref reports `Unknown`, never a silent `Current`.
* **The anchor is derived, not only set**
  (`MotionPlane::derivedFrom` / `MotionPlane::refresh`). A re-measurement moves
  the pivot, and the aspect ratio of a `fitted` plane with it.
* **The namespace and include prefix are `chrononmotion/`**, not `threepp/`, so
  the public names no longer advertise a fork that this tree is not.

## Offscreen render sibling (render/)

The GL renderer, materials, lights, textures, GLTF/Image/Texture loaders and
the headless canvas were reintroduced from upstream threepp at the pinned
revision — not into the core library, but as the separate `render/` module
(`chrononmotion_render`, `threepp::` namespace, own third-party deps).
The core stays dependency-free and renderer-free; the boundary guardrail
explicitly scopes `render/` out while still pinning `include/`/`src/`/`tests/`.
Dropped on re-entry: stale mixer/action sources, Vulkan-coupled objects,
window icon stub. Proven by the headless textured-cube spike
(`chrononmotion_render_spike`); GLB proof and the Motion→threepp bridge are
next. See `render/README.md`.
