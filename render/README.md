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

## Scene 03 composition contract: ChrononTemplate desk introduction

- **Subject and assets:** supplied blank room GLB, Quaternius Universal Animation Library Standard GLB, plywood desk texture, and Ubuntu Italic title mask. Preserve the same room, desk, chair and character geometry across the three comparison variants. Asset licenses and font attribution are recorded in `render/spike/assets/scene03/`.
- **Composition:** begin on the seated character and desk; move through a modest lateral orbit and push toward the white page. End with “Benvenuti in ChrononTemplate” readable on the page while part of the seated character remains visible above it.
- **Coordinates and camera:** metres; Y up; X screen horizontal; +Z points from the subject toward camera. The deterministic ChrononMotion `CameraRig` uses keyed position, target and focus distance at 0, 2.8, 6.3, 8.4 and 10 seconds. FOV 41 degrees; near/far 0.1/100. Camera positions: `(-0.06,1.32,2.78)`, `(-0.74,1.30,2.62)`, `(-0.63,1.24,2.02)`, `(-0.47,1.29,1.64)`, held to 10 seconds. Targets move from `(-0.5,0.98,0.22)` through `(-0.5,0.91,0.24)` and `(-0.5,0.83,0.51)` to the page at `(-0.5,0.78,0.59)`.
- **Motion and timing:** 10 seconds, 30 fps, 300 frames. Variant 1 uses the camera move with base lighting/materials; variant 2 adds shaped key/rim lighting, shadows and rack focus; variant 3 adds a deterministic crossfade from `Sitting_Idle_Loop` to `Sitting_Talking_Loop` and progressive title reveal from 5.4 to 9.0 seconds. `Scene03_Chronon3D_FilmFinish.plan.json` defines the Chronon3D video-plus-overlay composite and validates. The installed Chronon3D CLI build has native FFmpeg decoding disabled, so the delivered master applies the same restrained vignette/grain PNG overlay with FFmpeg after the 3D render.
- **Lighting and layers:** matte porcelain character; low ambient fill, warm overhead key, cool rim and soft shadow mapping; ACES tone mapping and BokehPass. The rack focus is measured from the seated character at about 2.82 m toward the page at about 1.18 m, with restrained blur. Room and chair retain dark matte clay, desk retains plywood texture, and the page stays opaque and thin.
- **Antialiasing:** the Scene 03 EffectComposer uses 4x MSAA on its internal render targets. Canvas framebuffer antialiasing alone does not affect the geometry pass after it is redirected through the composer. This reduces edge crawl on the thin page during the camera push-in.
- **Must not appear:** facial features, extra characters, unrelated props or writing other than the requested title.
- **Review evidence:** compare the three 10-second variants at identical geometry and framing. Inspect all frames at 0.5-second intervals in the final contact sheet and measure character/page ROI sharpness at opening, handoff and ending; check temporal change, title legibility and wall intersections before delivery.

# Milestone 3: MotionScene + CameraRig -> MotionSceneBridge -> multi-frame GL render
xvfb-run -a ./build/dev/render/chrononmotion_render_bridge /tmp/bridge.ppm 256 256

# Milestone 4: Deformable image mesh animation (PinWarp + Wave via RigProgram) -> PPM or MP4
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh /tmp/image_mesh.mp4 512 512 61
xvfb-run -a ./build/dev/render/chrononmotion_render_image_mesh <input.png> /tmp/out.mp4 1080 1080 90

# threepp scene post-processing: RenderPass -> UnrealBloomPass -> PPM
xvfb-run -a ./build/dev/render/chrononmotion_render_postprocessing /tmp/bloom.ppm 512 512
```

### Chronon finishing effects

`threepp/postprocessing/shaders/ChrononPostProcessShader.hpp` provides a
single full-screen finishing shader for scanlines, glass grid, radial RGB
separation, VHS jitter/chroma bleed, halation, lens distortion, edge softening,
horizontal streak bloom, and midtone-weighted grain. It is exposed by
`threepp/postprocessing/postprocessing.hpp`. The pass expects the caller to
bind `tDiffuse`, a preblurred full-frame `tBackdrop`, and a blurred bright-pass
`tHighlights`; disable effects by leaving their intensity uniforms at zero.

For a chain, render the source, extract highlights with
`chrononHighlightExtractShader()` (`uThreshold`/`uKnee`), and blur into separate
ping-pong targets with `chrononGaussianBlurShader()`. Set `uDirection` to a
horizontal then vertical pixel radius. Use a reduced-resolution target for
wide halation and streaks; use a full-size blur target for backdrop glass and
edge softening. Finally run `chrononPostProcessShader()` once at output size.
`uResolution` is in pixels, `uTime` is seconds, and the effect uniforms are
intensities (zero disables each effect). For directional streaks, the caller
can blur highlights horizontally only and bind that result to
`tHighlights`. This keeps convolution in dedicated passes and combines the
single-sample effects in one pass.

Temporal posterization must be applied before scene evaluation: call
`chrononSteppedTime(timeSeconds, targetFps)` and evaluate the scene at the
returned time while keeping the export frame rate unchanged. Default is 15 fps.

