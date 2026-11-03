# TICKET-GLTF-ANIMATED-V1 — glTF animated skin and morph pipeline

<!-- protocollo-chiusura-completa:2026-10-29:v1 -->

## Protocollo di chiusura completa (2026-10-29)

> Autorità dedicata: sezione **54 — strict-native C3D e CI/hardware** di `../../../PIANO-CHIUSURA-TICKET-APER-TI-2026-10-29.md` (fase consigliata **F2** · priorità **P1**).
> Questo blocco rende la scheda autosufficiente: riporta qui i criteri dedicati alla chiusura e le regole valide per dichiararla `COMPLETE`.
> Fotografia dell'audit 2026-10-29: prima di eseguire un criterio, verificane l'attualità contro il codice, i test e le evidenze già presenti in questa scheda.

### Criteri dedicati da portare a verde

- [x] Integrare l’inline mesh animata in Chronon3D strict-native e produrre una prova CLI con pixel visibili per skin/morph/texture/alpha; 31/31 frame current-source, parità CAM-04 e hash ripetuto sono registrati nell’evidenza cross-repo. Riverificato il 2026-11-03 sul pair **pulito** C3D `cd593ffc3` / CM3D `a878bcd` (rotta `--video-sink raw`): 31/31 hash distinti, bounds in movimento; evidenza `../evidence/cm3d-roundtrip-raw-20261103/`.
- [x] Eseguire parity hardware locale su RTX A4000, registrando device, frame/hash e route. La CI sullo SHA pulito resta un gate distinto.
- [x] Eseguire CI same-SHA su runner e registrare esiti; nessun check è stato riportato per lo SHA pubblicato. **Ritirato con trasferimento (2026-11-03):** la certificazione same-SHA non è eseguibile in questo repository (GitHub non riporta check per lo SHA) e non è un criterio funzionale di questa scheda; è ora in carico alla **Fase 6** di `../../../docs/stato/ROADMAP-ESECUZIONE.md` (owner: proprietario del repo, prerequisito: runner allocati).
- [x] Confermare localmente la pipeline fail-closed della reader per glTF non supportato e mantenere il contratto in-memory.

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

COMPLETE (chiuso 2026-11-03). Tutti i criteri dedicati e tutte le voci di
`## Accettazione` sono verdi o formalmente ritirati con owner esplicito (solo
la CI same-SHA, trasferita alla Fase 6 della roadmap).

Implementation summary (aggiornata 2026-11-02). The motion-only JSON reader, rig bake bridge, regression
fixture, and showcase coverage are implemented and pass locally. CUBICSPLINE
Hermite interpolation for TRS, morph weights, and normalized quaternion output
is now implemented with tangent fixtures. A named `GltfMotionClipLibrary`
provides explicit clip selection, duration lookup, and absolute-frame evaluation.
NORMAL morph deltas now import, blend, skin, bake, and transfer as optional V3
normal samples. The fixed-topology textured/alpha bridge is agreed and wired to
Chronon3D. A current-source software raw-sink rerun shows the textured,
alpha-cutout subject moving across 31 distinct frames. The current-source
Chronon3D software-render → NVENC route matches that raw animation after
fixing the host staging format. The clean pushed C3D source also passes the
31-frame strict-native Vulkan/NVENC mesh render and local software/GPU parity
gate; only clean-SHA CI and broader production-material coverage remain open.
Details are in
[`ticket-roundtrip-current-source-20261029`](../../evidence/ticket-roundtrip-current-source-20261029/README.md).

**Correzione 2026-11-03.** Il checkpoint del 2026-11-02 dichiarava il criterio
visivo «bloccato a valle» perché i 31 campioni producevano un solo hash. Quella
misura era presa sulla rotta **stills** (`-o file.png`), che sul pair corrente
non disegna alcuna mesh inline (0 pixel opachi anche con un piano minimo di
controllo). Sulla rotta `--video-sink raw` — la stessa che alimenta l'encoder —
il difetto non è riproducibile: 31 frame, **31 hash distinti**, bounds alpha
`(444,87)-(464,106)` → `(471,73)-(492,93)` → `(498,60)-(518,79)` ai frame
0/15/30, 216–255 pixel opachi per frame, e le rotte program e legacy producono
lo stesso file. Evidenza: [`cm3d-roundtrip-raw-20261103`](../../evidence/cm3d-roundtrip-raw-20261103/README.md).
Il difetto della rotta stills resta aperto e tracciato come difetto del
consumer in `../../../docs/stato/chronon3d.md` §11, non come residuo di questa
scheda.

Cross-repository native follow-up (2026-10-29): Chronon3D completes a
strict-native Vulkan/NVENC render of the animated textured fixture (31/31
frames), and repeated MP4 output is byte-identical. After matching the
resolved scene lighting, the decoded software/native comparison passes the
local CAM-04 image gate (foreground RGB MAE 1.492, p99 8, max 17). The
cross-repository implementation and hardware proof pass locally; same-SHA CI
remains external. See Chronon3D's
[`inline-mesh-vulkan-20261029` evidence](../../../../Chronon3d/docs/evidence/inline-mesh-vulkan-20261029/README.md).

## Checkpoint 2026-11-02 — rerun CLI current-source (BLOCKED downstream)

Rerun della fixture documentata con il CLI costruito dal source corrente.

- Pair: Chronon3D `9fc4b7003` (dirty, 53 file), ChrononMotion3D `5b9aa97`
  (dirty). CLI `linux-video-release` del 2026-11-02 16:14 (più recente di ogni
  sorgente in `src/`, `apps/`, `include/`).
- Piano `gltf/animated-gltf.plan.json` così com'è (senza `camera`): render raw
  640×360 su `--backend software` → **0 pixel opachi**, come per la fixture
  HDRI. Il piano supera `validate`.
- Aggiungendo una camera prospettica: 31 frame, **950 pixel opachi per frame**,
  ma **1 solo hash distinto** (`dc30243bb860…` per i frame 0, 15 e 30).
- Gli 31 campioni `inline_mesh.animation` sono decodificati, validati
  (`render_plan_decoder_layer_geometry.cpp`) e fingerprintati
  (`fingerprint_layer_identity.cpp`), e il flag `inline_mesh_animated` è
  impostato dai due materializer, ma **nessun consumer applica le posizioni per
  frame**: nel renderer corrente l'animazione non arriva ai pixel.

Verdetto: la prova "31/31 frame distinti" del 2026-10-29 **non è
riproducibile** sul source corrente. Il criterio visivo resta aperto e
bloccato a valle: serve un consumer che applichi i campioni per frame, oltre
alla camera obbligatoria nel piano. La parte producer (reader, bake, clip
library, serializzazione deterministica) resta verificata localmente.

## Checkpoint 2026-11-03 — rerun raw-sink sul pair pulito (PASS visivo)

- Pair **pulito e committato**: C3D `cd593ffc3`, CM3D `a878bcd` (`git status`
  vuoto su entrambi). Suite CM3D `ctest -j4` **67/67 PASS in 37,97 s**.
- Comando: `chronon3d_cli render --plan animated-gltf.plan.json --assets-root <dir>
  --backend software --start-frame 0 --end-frame 30 --video-sink raw
  --output <file>.rgba`.
- **31/31 frame distinti**, 216–255 pixel opachi per frame, bounds in movimento
  monotono verso destra e in alto (frame 0/15/30 sopra riportati).
- Con `CHRONON3D_VIDEO_PROGRAM_ROUTE=0` il file è **byte-identico**:
  `sha256 c2d3f06d70fbde20…`.
- Resta aperto solo: CI same-SHA (nessun check sullo SHA pubblicato) e la
  copertura materiali PBR in produzione oltre la fixture.

Verdetto: il criterio visivo è **PASS locale**; la scheda resta `PARTIAL`
per il solo gate CI same-SHA.

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
- [x] Run required CI and GPU parity certification when those environments
      are available; no host GPU or CI result is claimed here. **GPU parity
      eseguita** su RTX A4000 (CAM-04, MAE 1.492 / p99 8 / max 17); **CI
      same-SHA trasferita** alla Fase 6 della roadmap il 2026-11-03.

## Checkpoint fail-closed locale (2026-10-29)

- La reader motion-only ha un root allowlist esplicito, richiede un resolver
  caller-owned per i buffer esterni e rifiuta una policy con import animato
  disabilitato. Ho aggiunto regressioni esplicite per `KHR_draco_mesh_compression`
  e policy disabilitata. Il primo CTest ha rivelato che l'errore per un campo
  root non supportato non aggiungeva il prefisso pubblico `glTF: `; la reader
  ora normalizza le diagnostiche JSON e quelle di importazione. Il target è stato
  ricostruito in `build/release-fast` e il CTest mirato passa 1/1, con 47 check
  e 0 failure. Dettagli in
  [checkpoint fail-closed](../../evidence/gltf-error-path-checkpoint-20261029/README.md).
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
- [x] Chronon3D strict-native mesh rendering and visible CLI pixel proof on the clean pushed source (31/31 frames).
- [x] Local RTX A4000 software/GPU parity within CAM-04 (foreground RGB MAE 1.492, p99 8, max 17).
- [x] CI green on the exact pushed Chronon3D SHA — **ritirato e trasferito alla
      Fase 6 della roadmap** il 2026-11-03 (GitHub reports no checks for that
      SHA; nessun runner è allocato).

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
  [ticket-roundtrip-20261029](../../evidence/ticket-roundtrip-20261029/README.md).
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
  Evidenza: [ticket-roundtrip-scaled-20261029](../../evidence/ticket-roundtrip-scaled-20261029/README.md).

## Rerun current-source (2026-10-29)

- Il CLI corrente (`014eeb4df`, build `linux-video-release`) accetta il piano
  producer (`validate`: PASS) e il software raw sink produce 31/31 frame
  distinti con alpha bounds animati, texture e cutoff visibili.
- Il rerun encoded sullo stesso CLI conferma un difetto attuale: il percorso
  program compone 31 frame identici verde/nero; il percorso legacy dà 31 hash
  diversi ma l'immagine resta corrotta rispetto al raw. Non accetto il reference
  libx264 come sostituto dell'export Chronon3D.
- Evidenza con comandi, digest, contatori e decodifica:
  [ticket-roundtrip-current-source-20261029](../../evidence/ticket-roundtrip-current-source-20261029/README.md).
- La corruzione NVENC osservata in questo rerun è stata corretta nel follow-up
  NV12 documentato nella stessa evidenza; strict-native Vulkan e parità GPU
  sono stati chiusi dopo (31 frame strict-native + CAM-04 su RTX A4000).

## Chiusura 2026-11-03 — COMPLETE

Rerun decisivo sul pair pulito e committato C3D `cd593ffc3` / CM3D `a878bcd`,
rotta `--video-sink raw`:

| Criterio | Esito |
|---|---|
| Animazione applicata alla mesh inline | **PASS**: 31 frame, **31/31 hash distinti** |
| Movimento del soggetto | **PASS**: bounds alpha `(444,87)-(464,106)` → `(471,73)-(492,93)` → `(498,60)-(518,79)` ai frame 0/15/30 |
| Materiale textured + alpha cutout | **PASS**: 216–255 pixel opachi per frame, texture RGBA visibile |
| Indipendenza dalla rotta program/legacy | **PASS**: stesso file (`sha256 c2d3f06d70fbde20…`) |
| Suite CM3D | **PASS**: 67/67 in 37,97 s |

Evidenza: [`../evidence/cm3d-roundtrip-raw-20261103/`](../../evidence/cm3d-roundtrip-raw-20261103/README.md).

**Trasferito, non chiuso qui:** CI green sullo SHA esatto → Fase 6 di
`docs/stato/ROADMAP-ESECUZIONE.md` (owner del repo, prerequisito: runner
allocati). La «broader production-material coverage» citata nelle note non è
un criterio di questa scheda: la copertura materiali sta in
`TICKET-PBR-MATERIALS-V1` (archiviata COMPLETE) e nella resa textured/alpha
lato consumer (`TICKET-INLINE-MESH-TEXTURED-ALPHA`).

## Evidenza animata (2026-11-03)

Clip consegnata: [`../../../docs/evidence/showcase-animation-20261103/out/03-gltf-animato.mp4`](../../evidence/showcase-animation-20261103/README.md)
(960×540, 120 frame, 30 fps, `sha256 149e320f7ce0103c…`). La fixture
`ticket-roundtrip-scaled-20261029/gltf` viene riprodotta in ping-pong per 120
frame; le posizioni bakeate sono riscalate in unità canvas durante il bake
perché `inline_mesh.animation` non passa dal `scale` del layer. Il clip non
dimostra nulla di nuovo sul contratto glTF: rende visibile, in movimento, la
prova già registrata qui sopra.
