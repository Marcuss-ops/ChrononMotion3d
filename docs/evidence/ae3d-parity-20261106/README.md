# After Effects 3D parity audit — 2026-11-06

This evidence runs the six deterministic contracts from the Classic/Advanced 3D
comparison against the production math: ChrononMotion's two-node `CameraRig`,
Chronon3D's unified camera projection contract, `lighting.comp`'s point/spot
model, and `compute_dof_blur_radius()`. It does not substitute a different
renderer for a missing feature.

The audit is split by ownership. ChrononMotion3D owns the authoring contracts
(zoom/FOV, two-node camera, spot angle/feather units); Chronon3D owns the
renderer kernels (point falloff, shadow map). The point-falloff kernel now has
an explicit AE mode and passes. The shadow map remains a production feature
gap, not a formula fix.

## Artifact and command

- Audit source: `tools/ae3d_parity_audit.cpp`
- Built binary (ignored build output): `out/ae3d_parity/ae3d_parity_audit`
- JSON evidence: `out/ae3d_parity/audit.json`

```shell
cd ChrononMotion3D
cmake -S . -B build/chronon/dev -DCHRONONMOTION_BUILD_TESTS=ON
cmake --build build/chronon/dev --target chrononmotion_ae_parity chrononmotion_view -j 2
g++ -std=c++20 -O2 -Wall -Wextra \
    -I include -I ../Chronon3d/include \
    -I ../Chronon3d/vcpkg_installed/linux-video-test/x64-linux/include \
    tools/ae3d_parity_audit.cpp \
    ../Chronon3d/src/scene/camera/camera_projection_source.cpp \
    build/chronon/dev/libchrononmotion.a \
    -L ../Chronon3d/vcpkg_installed/linux-video-test/x64-linux/lib \
    -lspdlog -lfmt -pthread \
    -o out/ae3d_parity/ae3d_parity_audit
./out/ae3d_parity/ae3d_parity_audit --json out/ae3d_parity/audit.json
```

The audit exits non-zero while any contract is `FAIL` or `GAP`. The JSON is
valid and deterministic; two consecutive runs produced the same SHA-256
`eb0a5074f9437fc211ca94a38612f0ae50320aa61cb9caac0afcecb4bdde5d8d`.

## Results

| # | Contract | Status | Measured |
|---|----------|--------|----------|
| 1 | 1:1 projection at Z=0 | PASS | 50 mm / 20.25 mm gate → zoom 2666.666667 px; 400×400 plane → bbox 400.000×400.000 px, raster coverage 160000 px (expected 160000); FieldOfView mode identical; native 3D camera bbox 399.999976×399.999939 px; clip depth 0.000000 at near, 1.000000 at far. GPU framebuffer readback was not run. |
| 2 | Two-node camera / look-at | PASS | Target centred through a 720-step 360° orbit: max error 0.000244 px. At the vertical crossing the view direction changes 0.027976° and the right axis changes 0.0000°; exact pole aim error 0.000000 px. No NaN. The exact pole looks straight down. |
| 3 | Point-light falloff | PASS | `PointLightFalloff::AeInverseSquareClamped` in the C3D CPU evaluator and the `lighting.comp` branch: I(r_start)=1.000000, I(2·r_start)=0.250000, ratio 0.250000. The historical smooth model remains the default; the audit selects the AE mode explicitly. |
| 4 | Spot cone and feather | PASS | `aeSpotHalfAngleRadians(60)` → 30.000001° half-angle; `aeSpotPenumbra(50)` → 0.500000. Taper is monotonic, C1 smoothstep: factor 1.000000 at 15°, 0.000000 at 30°, max step 0.005355. Passing AE's total 60° directly to the half-angle field still gives factor 1.000000 at 30°; the CM3D helper prevents that authoring error. |
| 5 | Thin-lens depth of field | PASS | CoC at focus (z=1000) = 0.000000000 px. Near z=800: 12.531329 px; far z=1200: 8.354218 px; near/far = 1.500000. Maximum relative error against `|z-D|/z · f²/(N(D-f))` = 7.3e-8. C3D kernel; CM3D transports focus/fstop. |
| 6 | Shadow map and PCF | GAP | No shadow-map, PCF, Shadow Diffusion or adaptive normal bias in the production Vulkan backend. `ShadowNode` is a 2.5D projected layer shadow. `SHADOWMAP_TYPE_PCF`/`PCF_SOFT` exists only in `render/` (threepp OpenGL look-dev). This is a renderer feature, not a falloff formula. |

Summary: `PASS=5 PARTIAL=0 FAIL=0 GAP=1`.

## Interpretation

- **Projection and DoF match the AE equations** at the level of the production
  math. The projection test is exact in pixels but uses CPU raster coverage, not
  a Vulkan framebuffer readback; the GPU rasterizer's edge rule remains untested.
- **The two-node camera is singularity-safe.** The conditioned frame keeps the
  target centred, removes the 180° roll flip, and points exactly at the target at
  the pole. This is a ChrononMotion3D change.
- **Point falloff now has an explicit AE mode.** `PointLightSource` carries
  `PointLightFalloff`; `range` is the AE clamp start in
  `ae_inverse_square_clamped`, and `decay` is ignored in that mode. The default
  `smooth_inverse_power` preserves existing plans. The 16-float GPU payload uses
  its reserved `w` lane for the mode, so the buffer layout is unchanged.
- **Spot feather math is correct**, and the AE→wire conversion exists in CM3D:
  total Cone Angle → half-angle, Feather percent → normalized penumbra.
- **Shadow Diffusion parity cannot be certified** because a production
  shadow-map pass does not exist. The OpenGL look-dev PCF is not evidence for
  the Vulkan renderer. The production backend is compute-only: 27 shaders, all
  `.comp`, no vertex shader, no `vkCreateGraphicsPipelines` and no real
  `vkCmdDraw`; mesh rasterization lives in `inline_mesh.comp`. A light-space
  depth target and a caster feed therefore have to be introduced, not merely
  connected. It is not a one-line shader change.
- **VMA, async synchronization and shader hot reload are not CM3D work.** The
  core boundary forbids Vulkan/OpenGL/GLSL/shader in the authored tree, and no
  CM3D consumer exists. Hot reload could only live in the `render/` look-dev
  tree (P2); it is not implemented here.

## Guardrails and checks

- CM3D `chrononmotion_ae_parity`: 28 checks, 0 failures.
- CM3D `chrononmotion_view`: 147 checks, 0 failures.
- `tests/check_motion_boundary.cmake` with `-DSOURCE_DIR`: 324 files scanned,
  boundary intact.
- `tests/check_render_scope.cmake` with `-DSOURCE_DIR`: render sibling scope
  intact.
- C3D falloff standalone test (header-only production evaluator): PASS.
- `glslangValidator -V lighting.comp`: PASS (SPIR-V generated).
- Two audit runs produced byte-identical JSON.

### Build limitation

The full C3D `chronon3d_renderer_core_tests` target was configured, but the
targeted rebuild exceeded the 600 s tool timeout and the filesystem had about
8 GB free. No full C3D suite result is claimed here. The C3D validation in this
evidence is limited to the header-only falloff test and shader compilation.

## Source references

- `Chronon3d/include/chronon3d/math/camera_projection_contract.hpp`
- `Chronon3d/include/chronon3d/rendering/light_context.hpp`
- `Chronon3d/include/chronon3d/rendering/lighting_eval.hpp`
- `Chronon3d/include/chronon3d/render_plan/render_plan_layer_types.hpp`
- `Chronon3d/src/render_plan/render_plan_decoder_layer.cpp`
- `Chronon3d/src/render_plan/render_plan_compiler_scene_materializers.cpp`
- `Chronon3d/src/backends/vulkan/shaders/lighting.comp`
- `Chronon3d/src/render_graph/nodes/lighting_node.cpp`
- `Chronon3d/include/chronon3d/scene/model/camera/dof.hpp`
- `ChrononMotion3D/src/chrononmotion/motion/CameraRig.cpp`
- `ChrononMotion3D/include/chrononmotion/motion/Lighting.hpp`
- `ChrononMotion3D/tests/motion_ae_parity.cpp`
- `ChrononMotion3D/src/chrononmotion/motion/LightRigRenderPlan.cpp`
