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

- deleted stale sources whose headers no longer exist upstream
  (`animation/{AnimationAction,AnimationMixer,PropertyBinding,PropertyMixer}.cpp`,
  `objects/{DisplacedMesh,Ocean}.cpp` — Vulkan-coupled);
- `include/threepp/favicon.hpp` stub (headless needs no window icon);
- `GLTFLoader` keeps materials/lights/animation-clip support; the animation
  *runtime* (mixer/action) stays out — sampling authority remains Motion's.

## What is in here

- `renderers/gl/` + `GLRenderer` + `RenderTarget` + embedded GLSL shaders:
  the OpenGL renderer, no Vulkan.
- `materials/` (full set — the zoo is small next to the renderer; use
  Basic/Lambert/Standard), `lights/`, `textures/`, `geometries/`.
- `loaders/`: GLTFLoader, ImageLoader, TextureLoader (+ ModelLoader,
  MaterialVariants, AssetSource). No FBX/OBJ/COLLADA/USD zoo.
- `canvas/`: headless GLFW canvas (Null platform, no display server for the
  window itself; needs a GL implementation — Xvfb/llvmpipe on CPU boxes,
  EGL/GPU on render workers).
- `thirdparty/`: glad, glfw, stb, nlohmann/json, meshoptimizer, quickhull,
  libwebp decoder subset.

## Deliberately NOT here

Vulkan renderer, post-processing, audio, controls/input (except the passive
event source Canvas links against), physics, examples, python, editor.
Compositing, effects and encoding stay in Chronon3D.

## Build & spike

```shell
cmake --preset dev
cmake --build --preset dev --target chrononmotion_render_spike
xvfb-run -a ./build/dev/render/chrononmotion_render_spike /tmp/cube.ppm 256 256
```

The spike renders a textured, Lambert-lit cube offscreen and writes a PPM.
Milestone 1 (Textured Cube) is that spike; next: GLB import proof, then the
Motion→threepp bridge (baked world matrices + camera → threepp scene).
