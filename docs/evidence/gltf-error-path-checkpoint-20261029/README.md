# Animated glTF fail-closed test checkpoint — 2026-10-29

## Code contract inspected

`loadGltfMotion` rejects disabled motion-import policy before parsing. The
motion reader uses an explicit root-key allowlist; unsupported extensions such
as `KHR_draco_mesh_compression` are not accepted and cannot be silently ignored.
External buffer bytes remain caller-resolved; the reader has no implicit file
access path.

The existing rig-pipeline test already covers missing external-buffer
resolution, truncated buffer contracts, invalid inverse-bind shape, unsupported
skin indices, unnormalized weights, malformed CUBICSPLINE topology, and invalid
morph weights. This checkpoint adds explicit regressions for a declared Draco
extension and a disabled motion-import policy. The extension test also checks
that the public diagnostic retains the `glTF: ` prefix and that no external
buffer resolution occurs for either the unsupported extension or a disabled
motion-import policy.

## Provenance and verification

- ChrononMotion3D `HEAD`: `ea8982dc6d870852f42030154ff2a816d7686af7`, dirty worktree.
- Changed files: `src/chrononmotion/assets/GltfReader.cpp` and
  `tests/motion_rig_pipeline.cpp`.
- Initial updated CTest exposed a real diagnostic-contract gap: the shared JSON
  DOM rejected unsupported root fields with `std::invalid_argument` but without
  the public `glTF: ` prefix. `loadGltfMotion` now normalizes parser/reader
  invalid-argument diagnostics while preserving already-prefixed messages.
- `git diff --check` — exit 0.
- From `/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D`:
  `/usr/bin/c++ -I/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/include -std=gnu++20 -Wall -Wextra -Werror -fsyntax-only tests/motion_rig_pipeline.cpp` — exit 0. This checks syntax only; it does not link or execute the new assertions.
- After the unrelated Chronon3D `chronon3d_cli -j7` build finished and no
  other compiler jobs remained, ran
  `cmake --build build/release-fast --target chrononmotion_rig_pipeline -j2`.
  The target linked successfully.
- `ctest --test-dir build/release-fast --output-on-failure -R '^chrononmotion_rig_pipeline$'`
  — exit 0, 1/1 CTest PASS. Direct binary output: **47 checks, 0 failures**.
  This includes disabled-import policy rejection before resolver invocation,
  unsupported Draco declaration rejection before resolver invocation, and the
  stable `glTF: ` diagnostic prefix.

The producer-side fail-closed regression is now verified locally. The wider
integrated error path is still part of the downstream consumer acceptance.
Chronon3D inline-mesh Vulkan rendering, pixel parity, and clean-SHA CI remain
separate open gates.

Reproduction commands:

```sh
cmake --build build/release-fast --target chrononmotion_rig_pipeline -j1
ctest --test-dir build/release-fast --output-on-failure -R '^chrononmotion_rig_pipeline$'
```

The original `-j7` build did not overlap with the Motion3D rebuild. Peak
available memory during the `-j2` build remained above 10 GiB; no OOM occurred.
