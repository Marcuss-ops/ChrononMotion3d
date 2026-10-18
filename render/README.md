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
  These process a rendered 3D scene; Chronon3D still owns video-layer
  composition, project effects and final encoding.
- `materials/` (full set — the zoo is small next to the renderer; use
  Basic/Lambert/Standard), `lights/`, `textures/`, `geometries/`.
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

# Milestone 3: MotionScene + CameraRig -> MotionSceneBridge -> multi-frame GL render
xvfb-run -a ./build/dev/render/chrononmotion_render_bridge /tmp/bridge.ppm 256 256

# Milestone 4: Deformable image mesh animation (PinWarp + Wave via RigProgram) -> PPM or MP4
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh /tmp/image_mesh.mp4 512 512 61
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh <input.png> /tmp/out.mp4 1080 1080 90

# threepp scene post-processing: RenderPass -> UnrealBloomPass -> PPM
xvfb-run -a ./build/dev/render/chrononmotion_render_postprocessing /tmp/bloom.ppm 512 512
```

