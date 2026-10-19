# render/ — offscreen GL render sibling

Pixel-producing sibling of the dependency-free `chrononmotion` core library.
The core owns space and time; this module turns Motion-baked scenes into
pixels. It is a separate static library (`chrononmotion_render`) with its own
dependencies — the core links nothing and stays renderer-free (pinned by
`tests/check_motion_boundary.cmake`, which explicitly scopes `render/` out).

## Provenance

Reintroduced from upstream threepp at the pinned revision
`e2b165ece3904012d97a08029e0bd6cd5252892a` (same revision as
`docs/THREEPP_TEST_COMPATIBILITY.md`), in the `threepp::` namespace, unmodified
except:

- omitted stale sources whose headers no longer exist upstream
  (`objects/{DisplacedMesh,Ocean}.cpp` — Vulkan-coupled);
- restored upstream animation runtime sources
  (`animation/{AnimationAction,AnimationMixer,PropertyBinding,PropertyMixer}.cpp`)
  for GLTF clips; the bridge samples clips at absolute Motion frame time;
- `include/threepp/favicon.hpp` stub (headless needs no window icon);
- `GLTFLoader` includes materials/lights/animation-clip support and the restored
  mixer/action runtime. `MotionSceneBridge` samples bound top-level clips from
  absolute Chronon frame time; nested time remapping and clip blending remain
  unimplemented.

## What is in here

- `renderers/gl/` + `GLRenderer` + `RenderTarget` + embedded GLSL shaders:
  the OpenGL renderer, no Vulkan.
- `postprocessing/`: threepp's OpenGL `EffectComposer`, image-space passes,
  `UnrealBloomPass` and `BokehPass`, restored from the same pinned revision.
  `GTAOPass` and `OutlinePass` are backported from upstream threepp commit
  `8518a6c22d051775c45dbd0c04a3ba92e92a5366` (tag `2026-10-11`), preserving
  their upstream headers and implementation. GTAO provides screen-space
  contact/corner occlusion; OutlinePass provides selected-object silhouettes,
  including a separate hidden-edge color. They run in the existing GL
  `EffectComposer`; they do not add a renderer or change the core boundary.
  These process a rendered 3D scene; Chronon3D still owns video-layer
  composition, project effects and final encoding.
- `materials/` (full set — the zoo is small next to the renderer; use
  Basic/Lambert/Standard), `lights/`, `textures/`, `geometries/`.
- `motion/CinematicSceneLook.hpp`: reusable lighting, shadow-map, fog and
  optional clay-material composition for imported scenes. It uses existing
  threepp `AmbientLight`, `HemisphereLight`, `SpotLight`, `DirectionalLight`,
  `FogExp2`, `Scene::overrideMaterial` and renderer shadow-map APIs; it is a
  look preset, not a new material or renderer.
- `loaders/`: GLTFLoader, ImageLoader, TextureLoader (+ ModelLoader,
  MaterialVariants, AssetSource). No FBX/OBJ/COLLADA/USD zoo.
- `canvas/`: headless GLFW canvas (Null platform, no display server for the
  window itself; needs a GL implementation — Xvfb/llvmpipe on CPU boxes,
  EGL/GPU on render workers).
- `thirdparty/`: glad, glfw, stb, nlohmann/json, meshoptimizer, quickhull,
  libwebp decoder subset.

- `motion/MotionBridge.hpp` (`threepp::motion::MotionSceneBridge`): stateful
  bridge that synchronizes evaluated `MotionScene` / `CompiledScene` world
  transforms, `CameraRig` poses, lights (`Ambient`/`Directional`/`Point`/`Spot`),
  `MaterialDescriptor`s, `BufferGeometry`s, imported GLB subtrees, and
  per-frame deformed mesh vertices (`RigFrameGeometry` / `DeformerStack`) into
  `threepp::Scene` + `threepp::PerspectiveCamera`. Bound GLTF `AnimationClip`s
  are sampled at the absolute Chronon frame/fps time, including random access.
  `MaterialKind::PBR` maps roughness, metalness and emission to threepp
  `MeshStandardMaterial`. `bindMaterialNormalMap(materialId, texture)` attaches
  tangent-space normal maps to bridged PBR materials without putting GPU texture
  state in Chronon's renderer-neutral descriptor. When
  lowering to Chronon3D's compact inline material contract those values are
  approximated as specular/shininess rather than full PBR.

## Deliberately NOT here

Vulkan renderer, audio, interactive controls (the passive
`PeripheralsEventSource` that `Canvas` subclasses IS here), physics,
sensors, examples, python, editor. Compositing, effects and encoding
stay in Chronon3D. The spike/demo/certification binaries defined in
`render/CMakeLists.txt` are not built by default: enable them with
`-DCHRONONMOTION_BUILD_SPIKE_PROGRAMS=ON`.

## Build & headless proofs

```shell
cmake --preset dev
cmake --preset dev -DCHRONONMOTION_BUILD_SPIKE_PROGRAMS=ON   # enables the SPIKE/proof binaries
cmake --build --preset dev --target \
  chrononmotion_render_spike \
  chrononmotion_render_glb \
  chrononmotion_render_bridge \
  chrononmotion_render_image_mesh \
  chrononmotion_render_postprocessing

# Milestone 1: Textured cube -> PPM
xvfb-run -a ./build/dev/render/chrononmotion_render_spike /tmp/cube.ppm 256 256

# Milestone 2: Embedded-texture GLB 2.0 import -> PPM (or pass <model.glb> <out.ppm>)
xvfb-run -a ./build/dev/render/chrononmotion_render_glb /tmp/glb.ppm 256 256

# Cinematic scene-look preview: orbit, shadow map, fog and GTAO -> frame sequence
xvfb-run -a ./build/dev/render/chrononmotion_render_glb --scene-orbit <room.glb> /tmp/lookdev 8 854 480

## First production room shot: thinking mannequin

Scene contract for the first end-to-end documentary room shot:

- **Subject and assets:** the user-provided `Room_Blank_with_Mannequin.glb`, with its textured room and orange mannequin.
- **Composition:** the mannequin stays near frame center, shown at a human-scale distance from inside the room; camera FOV is 48 degrees.
- **Motion and timing:** a 12-second, 30 fps, deterministic 96-degree orbit; the static `MannequinPlacement` node receives a very slow whole-body idle sway and subtle bob sampled from absolute timeline time.
- **Layers:** one beauty render with imported materials, shadow map, GTAO, depth fog and ACES tone mapping.
- **Must not appear:** camera views from outside the room, wall cut-throughs, titles or extra props.
- **Preview check:** inspect the opening, middle and ending contact sheet for framing, wall intersections and subject visibility before delivering the video.

The mannequin in the supplied GLB is a single static mesh with no skin or animation clips. Its procedural idle moves the full mesh gently; it does not articulate the head or arms.

## Scene 03 composition contract: low-poly seated character at desk

- **Subject and assets:** supplied blank room GLB and a scene-authored low-poly human character. Build the body from shaped profile meshes and tapered limbs: glossy featureless white head and hands, slim red short-sleeve shirt, dark trousers and dark shoes. Do not use the previous primitive mannequin or downloaded person models.
- **Composition:** seated upright in the office chair; thighs angle forward, shins drop below the desktop, both elbows bend and both hands rest visibly on the desk. Match the supplied reference's centered, medium-wide view.
- **Motion and timing:** after the composition is approved, make a slow 180-degree camera move. Keep the character pose fixed.
- **Layers:** beauty render using the room's plywood texture on the desk, a procedural cloth weave, soft shadow mapping, ACES tone mapping and shallow depth of field.
- **Must not appear:** facial features, extra characters, titles or unrelated props.
- **Preview approval:** inspect the 854×480 still for a complete head and feet, slim connected silhouette, readable bent seated pose, hands in contact with the desk, visible chair and no wall intersection before producing the orbit or final 1920×1080 video.

# Milestone 3: MotionScene + CameraRig -> MotionSceneBridge -> multi-frame GL render
xvfb-run -a ./build/dev/render/chrononmotion_render_bridge /tmp/bridge.ppm 256 256

# Milestone 4: Deformable image mesh animation (PinWarp + Wave via RigProgram) -> PPM or MP4
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh /tmp/image_mesh.mp4 512 512 61
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh <input.png> /tmp/out.mp4 1080 1080 90

# threepp scene post-processing: RenderPass -> UnrealBloomPass -> PPM
xvfb-run -a ./build/dev/render/chrononmotion_render_postprocessing /tmp/bloom.ppm 512 512
```

