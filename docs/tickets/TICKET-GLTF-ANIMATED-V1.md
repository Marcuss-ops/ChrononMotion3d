# TICKET-GLTF-ANIMATED-V1 — glTF animated skin and morph pipeline

<!-- protocollo-chiusura-completa:2026-10-29:v1 -->

## Protocollo di chiusura completa (2026-10-29)

> Autorità dedicata: sezione **54 — strict-native C3D e CI/hardware** di `../../../PIANO-CHIUSURA-TICKET-APER-TI-2026-10-29.md` (fase consigliata **F2** · priorità **P1**).
> Questo blocco rende la scheda autosufficiente: riporta qui i criteri dedicati alla chiusura e le regole valide per dichiararla `COMPLETE`.
> Fotografia dell'audit 2026-10-29: prima di eseguire un criterio, verificane l'attualità contro il codice, i test e le evidenze già presenti in questa scheda.

### Criteri dedicati da portare a verde

- [ ] Integrare l’inline mesh animata in Chronon3D strict-native e produrre una prova CLI con pixel visibili per skin/morph/texture/alpha; confrontare con fixture/golden producer.
- [ ] Eseguire CI e parity hardware su SHA pulito, registrando device, frame/hash, route e risultati. Le suite locali producer già passate non chiudono l’accettazione downstream.
- [ ] Confermare pipeline di errore per glTF non supportato e mantenere contratto in-memory/fail-closed durante il test integrato.

### Regola di chiusura (valida per questa e ogni scheda)

1. Separa sempre **implementazione locale**, **test locale**, **parità GPU o asset/riferimenti esterni**, **CI same-SHA** e **decisioni di prodotto**; una categoria non sostituisce l'altra.
2. Registra per ogni prova: comando completo, SHA (clean o dirty), preset build, host/device, exit status, contatori (test/assert/frame/hash/digest) e percorso dell'evidenza sotto `docs/evidence/`.
3. I gate esterni (runner, content pack, footage, host GPU, decisione di owner) non si chiudono localmente: lo Stato resta `PARTIAL`/`BLOCKED` o `WAITING EXTERNAL` con prerequisito e owner espliciti. Non trasformare mai `NOT RUN`/`FAIL` in `PASS`.
4. Non rimuovere fallback, API o capability fail-closed prima del relativo gate di equivalenza/census; non implementare feature proposte senza la decisione di scope.
5. Chiusura effettiva = tutti i criteri dedicati verdi (oppure formalmente ritirati con motivazione e owner) + aggiornamento di `## Stato` in questa scheda, degli indici canonici se la scheda è indicizzata, di `TODO-RIPRESA-100X-E-TICKET-APER-TI.md`, e archiviazione della scheda secondo le convenzioni del repository.

### Percorso rapido (ordine di esecuzione consigliato)

1. Riconfronta i criteri dedicati qui sopra con lo stato attuale della scheda: le voci già soddisfatte non si rieseguono, si ricollegano all'evidenza esistente.
2. Frammenta l'esecuzione in micro-step: chiudi prima le azioni locali raggiungibili nel checkout corrente, poi le decisioni di contratto/scope aperte, infine prepara e richiedi i gate esterni del piano.
3. Dopo ogni criterio, spontalo in questa sezione con il link all'evidenza; annota nella stessa sezione `NOT RUN`/`BLOCKED` dove la prova non è disponibile, senza cancellare il criterio.
4. Quando tutti i criteri dedicati sono verdi (o ritirati in modo formale), applica la Regola di chiusura (punto 5), aggiorna gli indici canonicali e archivia la scheda.

<!-- /protocollo-chiusura-completa:2026-10-29:v1 -->

## Stato

PARTIAL (updated 2026-10-29). The motion-only JSON reader, rig bake bridge, regression
fixture, and showcase coverage are implemented and pass locally. CUBICSPLINE
Hermite interpolation for TRS, morph weights, and normalized quaternion output
is now implemented with tangent fixtures. A named `GltfMotionClipLibrary`
provides explicit clip selection, duration lookup, and absolute-frame evaluation.
NORMAL morph deltas now import, blend, skin, bake, and transfer as optional V3
normal samples. The fixed-topology textured/alpha bridge is agreed and wired to
Chronon3D. A current-source software raw-sink rerun now shows the textured,
alpha-cutout subject moving across 31 distinct frames. The current-source
Chronon3D software-render → NVENC route now matches that raw animation after
fixing the host staging format. Strict-native Vulkan drawing, GPU-render
parity, and host-GPU/CI certification remain open. Details are in
[`ticket-roundtrip-current-source-20261029`](../evidence/ticket-roundtrip-current-source-20261029/README.md).

Cross-repository native follow-up (2026-10-29): Chronon3D completes a
strict-native Vulkan/NVENC render of the animated textured fixture (31/31
frames), and repeated MP4 output is byte-identical. After matching the
resolved scene lighting, the decoded software/native comparison passes the
local CAM-04 image gate (foreground RGB MAE 1.492, p99 8, max 17). The
cross-repository implementation and hardware proof pass locally; same-SHA CI
remains external. See Chronon3D's
[`inline-mesh-vulkan-20261029` evidence](../../Chronon3d/docs/evidence/inline-mesh-vulkan-20261029/README.md).

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

## Checkpoint fail-closed locale (2026-10-29)

- La reader motion-only ha un root allowlist esplicito, richiede un resolver
  caller-owned per i buffer esterni e rifiuta una policy con import animato
  disabilitato. Ho aggiunto regressioni esplicite per `KHR_draco_mesh_compression`
  e policy disabilitata. Il primo CTest ha rivelato che l'errore per un campo
  root non supportato non aggiungeva il prefisso pubblico `glTF: `; la reader
  ora normalizza le diagnostiche JSON e quelle di importazione. Il target è stato
  ricostruito in `build/release-fast` e il CTest mirato passa 1/1, con 47 check
  e 0 failure. Dettagli in
  [checkpoint fail-closed](../evidence/gltf-error-path-checkpoint-20261029/README.md).
- La verifica locale producer-side è chiusa; la voce fail-closed del protocollo
  resta aperta per la prova integrata producer→consumer. La route mesh
  strict-native, la parità GPU e CI same-SHA restano gate separati.

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
- Il primo raw sink/MP4 della fixture è risultato nero/verde e non mostrava la
  mesh animata; l'evidenza storica resta in
  [ticket-roundtrip-20261029](../evidence/ticket-roundtrip-20261029/README.md).
  È stata superata dal rerun software/Vulkan strict-native citato nello stato
  corrente sopra, con 31/31 frame e parità CAM-04.
- La CI sullo SHA pubblicato non è ancora stata verificata; il ticket resta
  `PARTIAL` fino al gate same-SHA.

## Round-trip visibile aggiornato (2026-10-29)

- La fixture CLI emessa da `motion_authoring_renderplan.cpp` è ora posizionata
  al centro e scalata per mantenere leggibili texture e deformazione con la
  camera fallback Chronon3D. Producer rebuild seriale, 349 checks e
  `chrononmotion_authoring` CTest 1/1 PASS.
- La nuova prova software raw, catturata con il CLI Release disponibile, ha
  31/31 hash distinti e alpha bounds che si spostano da `[320,118,376,171]` a
  `[460,48,516,101]`; i pixel mostrano la texture RGBA. Il CLI usato è però
  compilato da `67fd33f7f`, non dal Chronon3D source HEAD `014eeb4df`, quindi
  questi pixel sono diagnostici e vanno ripetuti sul binario current-source.
- Il MP4 NVENC dello stesso CLI resta black/green e non rispetta i raw pixel;
  il reference MP4 esterno da raw→libx264 è solo confronto e non chiude la
  rotta canonica. Strict-native Vulkan, parità e CI same-SHA restano aperti.
  Evidenza: [ticket-roundtrip-scaled-20261029](../evidence/ticket-roundtrip-scaled-20261029/README.md).

## Rerun current-source (2026-10-29)

- Il CLI corrente (`014eeb4df`, build `linux-video-release`) accetta il piano
  producer (`validate`: PASS) e il software raw sink produce 31/31 frame
  distinti con alpha bounds animati, texture e cutoff visibili.
- Il rerun encoded sullo stesso CLI conferma un difetto attuale: il percorso
  program compone 31 frame identici verde/nero; il percorso legacy dà 31 hash
  diversi ma l'immagine resta corrotta rispetto al raw. Non accetto il reference
  libx264 come sostituto dell'export Chronon3D.
- Evidenza con comandi, digest, contatori e decodifica:
  [ticket-roundtrip-current-source-20261029](../evidence/ticket-roundtrip-current-source-20261029/README.md).
- La corruzione NVENC osservata in questo rerun è stata corretta nel follow-up
  NV12 documentato nella stessa evidenza. Restano strict-native Vulkan,
  parità GPU-render e CI same-SHA.
