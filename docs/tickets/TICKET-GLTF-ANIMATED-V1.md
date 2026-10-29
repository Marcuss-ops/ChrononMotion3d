# TICKET-GLTF-ANIMATED-V1 — glTF animated skin and morph pipeline

## Stato

PARTIAL (2026-10-28). The motion-only JSON reader, rig bake bridge, regression
fixture, and showcase coverage are implemented and pass locally. CUBICSPLINE
Hermite interpolation for TRS, morph weights, and normalized quaternion output
is now implemented with tangent fixtures. A named `GltfMotionClipLibrary`
provides explicit clip selection, duration lookup, and absolute-frame evaluation.
NORMAL morph deltas now import, blend, skin, bake, and transfer as optional V3
normal samples. The fixed-topology textured/alpha bridge is agreed and wired to
Chronon3D; only its strict-native GPU implementation and visible CLI proof remain.
Host-GPU and CI certification are external follow-ups; they do not replace the
remaining local feature work.

## Consegnato

- `loadGltfMotion` accepts in-memory glTF 2.0 JSON with an explicit external
  buffer resolver. It imports bounded TRS hierarchies, skins, inverse-bind
  matrices, indexed triangle POSITION/NORMAL geometry, JOINTS_0/WEIGHTS_0,
  POSITION/NORMAL morph targets, and LINEAR/STEP/CUBICSPLINE translation,
  rotation, scale, and weights channels. CUBICSPLINE tangents retain Hermite
  semantics through Track sampling; quaternion samples are normalized. Unsupported input fails with
  `std::invalid_argument` and `glTF:`.
- Imported clips map into `RigProgram`; rig baking applies skin and morph
  deformation and the Chronon3D adapter emits V3 `inline_mesh.animation`
  samples. There is no implicit static Scene or render-frame routing.
- `GltfMotionClipLibrary` creates a validated runtime evaluator per imported
  clip; callers enumerate names and explicitly sample by name, frame, and FPS.
  Unknown names and invalid morph-channel topology fail closed.
- Shared in-memory regression fixtures cover skin, joint attributes, inverse
  bind, LINEAR TRS/POSITION morph animation, and CUBICSPLINE translation,
  quaternion rotation, and morph weight tangents. The authoring integration
  test checks the 31-frame bake, resulting positions, producer validation, and
  deterministic serialization.
- Showcase case `gltf_skinned_morph` checks deterministic bake output and
  sampled geometry. Its golden is stable across two consecutive dumps.
- Release tests passed on 2026-10-28:
  `chrononmotion_rig_pipeline`, `chrononmotion_authoring`,
  `chrononmotion_gltf`, and `chrononmotion_showcase_all` (4/4).
- `chrononmotion_rig_pipeline` passes with nonzero CUBICSPLINE tangents for
  translation and morph weights, normalized quaternion Hermite output, and
  rejection of mismatched tangent topology.

## Lavoro rimanente

- [x] Add glTF CUBICSPLINE interpolation with tangent and quaternion semantics.
- [x] Import and deform NORMAL morph deltas with validation and fixtures.
- [x] Define and implement a named CLIP_LIBRARY runtime route with explicit
      selection and deterministic absolute-frame sampling.
- [x] Agree and document the cross-repository V3 animated geometry contract
      with Chronon3D `TICKET-INLINE-MESH-TEXTURED-ALPHA`: fixed indexed
      topology, per-frame positions and optional normals, static base UVs,
      textures, material and alpha cutoff.
- [ ] Run required CI and GPU parity certification when those environments
      are available; no host GPU or CI result is claimed here.

## Accettazione corrente

- [x] Motion-only reader contract and numerical limits documented separately
      from the strict static `loadGltf` route.
- [x] Bounded hierarchy, skins, JOINTS_0/WEIGHTS_0, inverse binds, POSITION
      morphs, and LINEAR/STEP/CUBICSPLINE channels covered by deterministic
      fixtures and malformed-input rejection tests.
- [x] Imported animated skin+morph asset baked and serialized through the V3
      adapter; local integration and showcase tests pass.
- [x] CUBICSPLINE interpolation/tangents and normalized quaternion sampling.
- [x] CLIP_LIBRARY authority and runtime clip selection implemented.
- [x] NORMAL morph deltas import, skin, and bake.
- [x] Textured/alpha V3 bridge contract agreed and carried through V3 samples.
- [ ] Chronon3D strict-native mesh rendering and visible CLI pixel proof.
- [ ] CI and hardware parity certified.

## Dipendenze

- `TICKET-PBR-MATERIALS-V1.md` for visual material import (separate scope).
- Chronon3D `TICKET-INLINE-MESH-TEXTURED-ALPHA.md` for downstream textured
  animated geometry and alpha rendering.

## Riferimenti

- `docs/GLTF_READER.md`
- `docs/CLIP_LIBRARY.md`
- `docs/RIGGING_FINAL_V1.md`
- `docs/AUTORIG_GLB.md`

## Verifica locale aggiornata (2026-10-29)

- Release CTest mirato: `chrononmotion_rig_pipeline`, `chrononmotion_hdri`,
  `chrononmotion_light_rig`, `chrononmotion_authoring`, `chrononmotion_gltf`,
  `chrononmotion_showcase_all` — 6/6 PASS.
- Il raw sink software è ora selezionabile anche quando il RenderPlan dichiara
  output PNG (fix CLI Chronon3D: la preparazione crea la vista video di
  compatibilità in presenza di `--video-sink raw`). La compilazione del piano
  animato riesce e vengono scritti due frame, ma entrambi sono RGBA uniformi
  neri. L'anteprima PNG software mostra il triangolo statico, ma quella lane è
  documentata come non pixel-truth e non prova la deformazione per frame.
- La prova è un piano sintetico V3 inline-mesh, non un render di un GLB reale.
  La geometria animata e i test producer/reader restano verdi; la prova visiva
  software del mesh e il rendering strict-native Vulkan restano aperti.
- Nessuna verifica host-GPU o CI è stata eseguita o dichiarata.
