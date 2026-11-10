# ChrononMotion preview runtime

## Goal

Give ChrononMotion the **application layer** it was missing, without moving any
renderer, filesystem, thread or content dependency into the deterministic core:

1. an asset server with generational handles, async loads, change events, a
   dependency graph, hot reload and reference-counted unload;
2. IO / compute / async task pools with ordered, deterministic map and reduce;
3. a playback clock that maps wall time onto the existing frame + sub-frame
   calendar;
4. a renderer-neutral debug-draw command buffer with camera, light, rig and
   motion-scene helpers;
5. a diagnostics collector and overlay;
6. a small `PreviewHost` that wires the five together.

The preview layer does not modify the core; the only core edit in this change
is an unrelated contract repair found during full-suite validation
(`applyFocusSequence` now rejects every nonzero `irisBlades`). The new library
lives in `preview/`; the GL line renderer lives in `render/`.

## Architecture

```
preview/
  include/chrononmotion/preview/
    AssetServer.hpp     handles, events, dependency graph, hot reload, leases
    TaskPool.hpp        Io/Compute/Async pools, ordered map/reduce
    PlaybackClock.hpp   play/pause/seek/rate/loop, fixed-step accumulator
    DebugDraw.hpp       line command buffer + camera/light/rig/scene helpers
    Diagnostics.hpp     frame stats, ring buffer, JSON, text overlay
    PreviewHost.hpp     composition root
  src/chrononmotion/preview/...
  tests/preview_tests.cpp
```

The GL consumer is `render/include/threepp/debug/DebugDrawRenderer.hpp` and
`render/src/threepp/debug/DebugDrawRenderer.cpp`; it draws the command buffer as
`GL_LINES` and can render it in screen space for the diagnostics overlay.

### Boundary rules

- `preview/` may use the C++ standard library (`<thread>`, `<filesystem>`,
  `<chrono>`), but it links only `chrononmotion` and `Threads::Threads`.
- No file watcher, image decoder, text shaper, font stack, GL or Vulkan header
  enters `include/` or `src/` under the core.
- The core stays deterministic and single-threaded. `parallelFor` is an
  application-side executor; callers must only parallelise pure, independent
  work. `parallelMap` and `parallelReduce` preserve index/order.
- Asset loaders are registered per `AssetServer`; the built-in loaders are
  `TextAsset`, `BinaryAsset`, `MotionSceneAsset` and `chrononmotion::GltfAsset`.

## Acceptance criteria

- `chrononmotion_preview` configures and builds with `CHRONONMOTION_BUILD_RENDER=OFF`.
- `chrononmotion_preview` CTest covers:
  - task submission, ordered map, deterministic reduce and exception propagation;
  - play/pause/seek/rate/loop, exact frame/sub-frame mapping and fixed steps;
  - asset Added/Modified/Removed/Failed events, dependency propagation, cycle
    rejection, lease/unload/GC and file-change polling;
  - debug primitives, text, camera/light/rig/scene helpers;
  - diagnostics history, averages, JSON and overlay;
  - `PreviewHost` frame update, debug builder and diagnostics.
- `chrononmotion_boundary` and `chrononmotion_render_scope` still pass.
- With `CHRONONMOTION_BUILD_RENDER=ON`, the GL debug renderer compiles and its
  headless pixel test draws a visible line into the framebuffer.
- The full CTest suite passes in both lanes that the host can run.

## Verification

Verified on 2026-11-09 with the `release-fast` preset on Linux.

| Lane | Configuration | CTest result |
|---|---|---|
| Core + preview | `CHRONONMOTION_BUILD_RENDER=OFF`, `CHRONONMOTION_BUILD_PREVIEW=ON` | 66/66 executed passed; 1 optional test disabled |
| Core + render + preview | `CHRONONMOTION_BUILD_RENDER=ON`, `CHRONONMOTION_BUILD_PREVIEW=ON` | 69/69 executed passed; 1 optional test disabled |

Commands:

```sh
cmake --preset release-fast -S ChrononMotion3D \
  -B ChrononMotion3D/build/preview-release-fast \
  -DCHRONONMOTION_BUILD_RENDER=OFF \
  -DCHRONONMOTION_BUILD_PREVIEW=ON
cmake --build ChrononMotion3D/build/preview-release-fast -j 8
ctest --test-dir ChrononMotion3D/build/preview-release-fast --output-on-failure

cmake --preset release-fast -S ChrononMotion3D \
  -B ChrononMotion3D/build/render-release-fast \
  -DCHRONONMOTION_BUILD_RENDER=ON \
  -DCHRONONMOTION_BUILD_PREVIEW=ON \
  -DCHRONONMOTION_BUILD_TESTS=ON
cmake --build ChrononMotion3D/build/render-release-fast -j 8
ctest --test-dir ChrononMotion3D/build/render-release-fast --output-on-failure
```

Specific evidence:

- `chrononmotion_preview_tests`: **105 checks, 0 failures**.
- `chrononmotion_render_debug_draw_pixels`: passed headless under EGL and
  reported `DebugDrawRenderer drew 64 red pixels`.
- `chrononmotion_boundary` and `chrononmotion_render_scope`: passed in both
  lanes.
- The only disabled test in each lane is the optional
  `chrononmotion_showcase_renderplan`, gated on a Chronon3D include directory.

Full-suite validation also exposed two stale HEAD checks unrelated to the
preview layer, repaired and rechecked here:

- `applyFocusSequence` now rejects every nonzero `irisBlades` value, matching
  the RenderPlan contract, instead of accepting 3–12 blades.
- The `editorial_light_rig` showcase golden was refreshed after the light-rig
  serializer gained its `falloff` field; the new hash is
  `869f21eb77bf766e`.

## Non-goals

- Replacing the production Vulkan/CUDA/NVENC path.
- An ECS, a scene editor, or a full UI toolkit.
- Defining a new time authority: `ResolvedFramePlan` remains the calendar.
- Making `motion::MotionScene::evaluate` parallel by itself. The task pools are
  for offline bake / asset decode, where the caller owns isolation.

## Usage sketch

```cpp
chrononmotion::preview::PreviewHost host({.fps = {30, 1}, .frameCount = 300});
host.clock().play();
host.setDebugBuilder([&](auto& draw) {
    // draw.cameraRig(rig, host.clock().seconds(), color);
});
host.update(1.0 / 30.0);
auto& overlay = host.debugDraw();
```

The GL app calls `DebugDrawRenderer::render(host.debugDraw(), viewProjection)` in
the main pass and `renderScreenSpace(...)` after it. The command buffer is
renderer-neutral; tests and offline tools inspect it without a GL context.
