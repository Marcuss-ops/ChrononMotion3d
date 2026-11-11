# Frontiera CM3D ↔ Chronon3d: stato di unificazione (blur / DOF / easing / 2.5D)

> Doc unica di verità del confine tra `ChrononMotion3D` (authoring motion,
> senza renderer, fork di threepp) e `Chronon3d` (renderer Vulkan/software che
> consuma i RenderPlan). Ogni voce ha evidence `path:line` nel tree al
> 2026-10-28. Le zone **CHIUSE** sono implementate e verificate; le zone
> **FUTURE-SCOPE** sono dichiarate e NON vanno reimplementate in parallelo.

> **Direzione aggiornata 2026-11-01:** per la pipeline camera, compositing,
> DoF e shutter fa fede [`ADR-CAMERA-PIPELINE-OWNERSHIP.md`](ADR-CAMERA-PIPELINE-OWNERSHIP.md).
> Questo documento descrive in quelle sezioni il confine legacy ancora presente
> nel codice; non è più la regola target. Migrazione e acceptance sono aperte.
> Il contratto canonico di unità, spazi, rotazioni e capability camera è
> [`CAMERA_RENDERPLAN_CONTRACT.md`](CAMERA_RENDERPLAN_CONTRACT.md).

## Ruoli (perché non c'è duplicazione di renderer)

| | CM3D | Chronon3d |
|---|---|---|
| Ruolo | authoring/motion (no renderer) | renderer, consuma RenderPlan |
| Canale verso l'altro | lowering → `chrononmotion_chronon3d_adapter` (opt-in, CMakeLists.txt:159-190) | decode + compile + render graph |

## Zone CHIUSE

### C1 — Apertura: f/stop come unica verità authored
- `CameraRig.hpp:209-233`: bridge "DOF unification"; il legacy gain non è più
  authored né trasportato. La preview GL deriva il proprio gain interno da
  `f_stop`; il wire usa `camera.f_stop` come singola autorità, e il consumer
  C3D deriva da esso il parametro interno del kernel dopo il decode.

### C2 — Lowering fisico V3 implementato
- `RenderPlanLoweringCamera.cpp:99-118`: `focal_length_mm`, `f_stop`,
  `iris_blades` e `dof_highlight_gain` emessi dal rig;
  `dof.aperture` non è più emesso sul wire.
- Fail-closed speculare: `lowerCameraPose` (range f/stop [0.7, 32], iris [0, 12],
  gain [0, 8], DOF con aperture ⇒ focus distance > 0) e decoder C3D
  `render_plan_decoder_support.cpp:244-257` valida le stesse gamme.

### C3 — Iris poligonale fail-closed; highlight weighting supportato
- `dof.hpp`: `validate_dof_render_capabilities` rifiuta `iris_blades` nonzero
  e highlight gain fuori da [0, 8]; il gain nell'intervallo è supportato dai
  kernel software e Vulkan per il bokeh circolare.
- Call site: `per_pixel_dof_node.cpp:342`.
- **Drift chiuso 2026-11-02:** il 2026-11-01 il producer Motion rifiutava il
  gain perché nessun kernel raggiungibile lo eseguiva. Da quando il kernel a
  disco con highlight weighting è atterrato, `lowerCameraPose`,
  `emissionCameraProblem` e `applyFocusSequence` accettano il gain e lo
  trasportano nel piano, mentre il solo `iris_blades` resta fail-closed. Il
  predicato resta l'unica sede: quando esisterà il kernel a lamelle cambia lì,
  e Motion lo segue senza patch.
- **FOV di default:** `CameraRig` e `CameraPose` partono da **50°** verticali
  (era 60), allineati al piano optical studio; il costruttore e i default dei
  membri erano disallineati (60 vs 50) e sono stati riuniti. Pin:
  `tests/motion_authoring_renderplan.cpp` (default FOV 50).
- **Gate sensore fisico:** `CameraRig::setSensorHeight` accetta [1, 100] mm,
  rifiuta non-finiti; il campo arriva a `camera.sensor_height_mm` sia nel
  piano base sia in ogni `camera_samples`. Pin: setter (range e non-finiti),
  propagazione a entrambi gli endpoint e FOV di default.

### C4 — Default di sequenza coerenti col contratto (fix 2026-10-28)
- `CameraFocusSequence.hpp`: `highlightGain` default **0.f** (era 1.f — un
  preset di default produceva un piano che il renderer rifiuta);
  `irisBlades` già 0. Pin di contratto nel test
  `tests/motion_camera_focus_sequence.cpp` (default FocusSequence → pose
  highlightGain == 0; il test verifica il passaggio del gain supportato e il
  rifiuto delle iris poligonali prima di mutare i canali del rig).

### C5 — Legacy DoF wire gain rimosso (migrazione coordinata 2026-10-31)
- `CameraPlan::dof_aperture` e `camera.dof.aperture` sono stati rimossi dal
  wire/schema e dall'emissione CM3D. `CameraPlan::f_stop` è l'unica unità
  authored e typed; `camera.dof.f_stop` resta accettato come alias wire per
  compatibilità ed è normalizzato al decode.
- `CameraPlan::dof_f_stop` e `CameraPlan::dof_iris_blades` sono stati rimossi:
  i valori nested wire sono normalizzati nei campi camera-level, con errore se
  contraddicono i valori top-level. CM3D emette soltanto i campi camera-level;
  gli alias annidati sono decoder-only e hanno demolition debt `ACTIVE` nel
  ticket di consolidamento.
- Lowering e emissione CM3D delegano range/capability admission allo stesso
  `camera_contract_problem()` del decoder C3D; i range camera non sono più
  mantenuti in tre blocchi manuali separati.
- Il renderer deriva il proprio parametro interno di blur dal f-stop nel
  `render_plan_camera_adapter.hpp`; `DepthOfFieldSettings::aperture` resta
  un'unità d'implementazione del kernel, non un campo authored/trasportato.
- Il decoder rifiuta il vecchio wire key. L'equivalenza pixel/runtime va ancora
  certificata con i gate del ticket di consolidamento.

### C6 — Full-scene shutter: emissione `temporal_motion_blur` (chiusa 2026-10-28)
- CM3D autoreggia lo shutter come unità authored unica
  (`MotionScene::setShutter(ShutterSettings)`, angolo in (0, 360], samples in
  [2, 64], fail-closed alla set). Il lowering
  (`RenderPlanLowering.cpp`) lo mappa 1:1 su `temporal_motion_blur` V3 e
  rifiuta fail-closed l'autoring doppio (scena + opzione caller). L'opzione
  `RenderPlanLoweringOptions::temporalMotionBlur` resta l'override
  programmatico.
- Formato scena roundtrip: `temporal_motion_blur` scritto solo se authored
  (`SceneExport.cpp`), letto fail-closed (`SceneImport.cpp`); test in
  `tests/motion_authoring_renderplan.cpp` e `tests/motion_scene_import.cpp`.

### C7 — Piano 2.5D: convergenza delle tre firme (chiusa 2026-10-28)
- Le firme erano: CM3D `MotionPlane.hpp:25`, CM3D `Layer::is3D/flat()`; C3D
  `camera_2_5d.hpp` + depth plane DOF (`kUnsetDofDepth`, dof.hpp:44).
  La convergenza è il contract JSON
  [`contracts/chronon.render-plan.2_5d-plane.v1.json`](../contracts/chronon.render-plan.2_5d-plane.v1.json)
  (stesso stampo di `chronon.render-plan.inline-mesh.v1.json`), non una
  struct condivisa in codice: pinna l'unità authored del piano, l'unità di
  valico (profondità camera-space positiva, `visible <=> depth > 1e-4`),
  `perspective_scale = focal_y_px / depth` con le regole focal per
  Zoom/Fov/PhysicalLens, e il sentinel `kUnsetDofDepth = 1e18`.
- Regola: il mapping dalla world-Z CM3D esiste solo tramite la view transform
  del projection contract C3D; nessun lato reimplementa le convenzioni
  dell'altro.

## Zone FUTURE-SCOPE (NON reimplementare in parallelo)

### Closeout camera — checkpoint 2026-11-01

- **Stato implementato (legacy):** Motion risolve gerarchia/world e bake dei
  campioni camera/layer nel RenderPlan; C3D esegue projection, depth,
  compositing, DoF e shutter. Questa descrive il codice corrente, non
  l'ownership target stabilita nell'ADR collegato in testa al documento.
- L'adapter esistente è il valico legacy. La migrazione deve introdurre un
  piano risolto con semantica posseduta da Motion, mantenendo il backend C3D
  privo di scena authored, layer model editoriale e callback runtime verso
  Motion.
- Motion authoring, camera bake roundtrip e parity RenderPlan camera passano
  localmente (3/3); `chrononmotion_boundary` passa (1/1). Queste prove locali
  non certificano il pair CI same-SHA.
- Il fix di trasferimento depth e il compositing CPU per-pixel sono
  implementazioni backend locali, non ownership semantica definitiva. Canary
  mesh/piano: 46/46 asserzioni; suite renderer-core: 204/204 casi.
- Shutter GPU, layout/lifecycle Vulkan e accettazione video restano gate backend
  aperti nel
  [`ticket camera di C3D`](../../Chronon3d/docs/tickets/archive/TICKET-CAMERA-DEFINITIVE-CLOSURE.md).
  Non duplicare queste implementazioni in Motion per aggirare i gate.

### Contratto camera/layer risolti — aggiornamento 2026-10-30

- Motion risolve local → world con parent/Null, look-at, roll ed easing. Il
  confine riflette Z (`S = diag(1,1,-1)`); per la camera `R_c3d = S R_motion S`.
  Le rotazioni renderer sono gradi, ricostruite nell'ordine `qZ*qY*qX`.
- `camera_contract.hpp` è l'ammissione condivisa producer/decoder/compiler:
  FOV verticale 10–120°, focale 0 oppure 12–200 mm, sensore 1–100 mm,
  f-stop 0 oppure 0.7–32. Focale positiva precede FOV nella proiezione;
  focus distance è distanza positiva lungo l'asse ottico nelle unità scena.
  Il wire non trasporta un gain aperture: il consumer deriva il parametro
  interno del kernel da f-stop.
  Iris blades nonzero sono rifiutati prima del render; highlight gain in [0, 8]
  viene conservato nel lowering e consumato dai kernel circolari.
- `resolved_sample_frames(canvas, blur)` usa lo scheduler temporale C3D, include
  frame interi e tutti i sottocampioni shutter (180°/8 nella acceptance).
  Il lowering Motion valuta camera e TRS/opacity dei layer agli stessi istanti.
  `transform_samples` V3 trasporta TRS world già in coordinate renderer,
  con origine canvas centrata anche per le primitive e timestamp double.
  Niente parent/animation/follow_path simultanei: l'authority è una sola.
- I consumer tipizzato e compatibility usano `sample_resolved_transform`:
  timestamp esatto → valore memorizzato; fra timestamp interpolazione
  TRS (slerp per orientamento), quindi NON equivalenza authored arbitraria.
  Il provider runtime camera è materializzato dal compilatore agli stessi
  istanti; il programma immutabile conserva valori, non callback o scena Motion.
- Campioni layer supportati: i layer visuali 2.5D (Image/Video/Color/Text/Shape/
  Particles/Adjustment) senza tracce extra; Light e Mesh restano esclusi perché
  hanno semantiche di consumo dedicate. Questo estende il contratto esistente
  senza una nuova authority di regia o una cache parallela.

#### Demolition debt — integer transform bake
- Owner: manutentore del confine Motion/RenderPlan.
- Reason: V2 non trasporta `transform_samples`; la sua lane dense resta il
  formato storico a keyframe interi e non è dichiarata exact sub-frame.
- Exit condition: il corpus compatibility/typed ha equivalenza verificata per
  i payload supportati e viene deciso il ritiro di V2.
- Equivalence gate: camera bake roundtrip + consumer typed raw pixel gate.
- Removal scope: emissione dense integer TRS in RenderPlanLoweringSamples,
  non le AnimationTrack authored generiche del renderer.
- Status: PARTIAL — la lane V3 è stata rimossa (emette sempre
  `transform_samples`); resta solo V2 legacy e l'equivalenza consumer. Il
  passaggio non elimina il renderer DoF C3D.

### Camera parented e shutter: note storiche del checkpoint precedente
- `makeCameraSampleProvider(rig, scene, fingerprint)` risolve il parent nel
  `MotionScene` al tempo frazionario richiesto. Il chiamante deve dedicare la
  scena al provider, mantenere rig e gerarchia immutabili e includere entrambi
  nel fingerprint; il provider serializza le proprie chiamate.
- `RenderPlanLoweringOptions::cameraSampleFrames` accetta gli istanti esatti
  forniti dallo scheduler C3D, che rimane l'unica autorità del calendario.
  Il JSON conserva la precisione double dei timestamp. Se la lista è vuota il
  bake senza shutter usa ancora i frame interi; tra campioni C3D interpola e
  non è garantita parità per curve arbitrarie. Con shutter il lowering consuma
  il calendario C3D e risolve anche i layer supportati agli stessi timestamp,
  come descritto nel contratto aggiornato sopra.
- Il pass `BokehPass` del renderer di prova CM3D non forza più alpha opaco;
  ciò non certifica un handoff RGBA animato GPU-native o la qualità dei bordi.
  `render/` resta attualmente un harness e non un renderer di produzione.

### F2 — Easing: tre vocabolari — CHIUSO come decisione (2026-11-06)
- C3D `easing.hpp` (enum → GLM), CM3D `motion/Easing.hpp` (CubicBezier class),
  CM3D `extras/core/Interpolations.hpp` (eredità threepp). I tre vocabolari
  restano deliberatamente separati: il lowering **bakea ogni easing in campioni
  lineari risolti** e il piano non trasporta easing per-key. La parità è
  pinnata da `tests/motion_renderplan_easing_parity.cpp`, che copre tutti gli
  11 tipi Motion contro il piano abbassato. Nessuna mappatura per-key va
  aggiunta al confine.

### F4 — Layer shadow: authored, serializzato ed emesso (CHIUSO 2026-11-11)
- `Layer.hpp:146-148` e `CompiledScene.hpp:36-38` trasportano
  `acceptsShadow`/`shadowOpacity`/`shadowBlur`: sono dati autorevoli del
  formato scena (import/update/validation/export) e NON codice morto.
- Chiusura completata: emissione `shadow_blur` e `shadow_opacity` implementata nel
  lowering V3 (`RenderPlanLoweringSamples.cpp`), serializzazione JSON
  (`RenderPlanEmission.cpp`), e validazione fail-closed su range ([0, 192] blur,
  [0, 1] opacity) e su schema V2 (`RenderPlanEmissionValidateLayer.cpp`).
- Il kernel C3D per-layer consuma questi campi (`ShadowNode` con override
  `shadow_blur`/`shadow_opacity` in `graph_builder_lighting_passes.cpp:111-122`).
- Test di conformità e non-regressione verificati in `chrononmotion_authoring`
  (`tests/motion_authoring_renderplan.cpp`, `tests/chronon3d_v3_bridge_regression.cpp`).

### F5 — Roving key: fuori scope V1, fail-closed (2026-11-06)
- `Keyframe` non ha un flag roving e l'emission rifiuta un key di piano con
  `roving=true` (`RenderPlanEmissionValidateTracks.cpp`). La semantica
  AE/EffectCraft non è definita abbastanza da aggiungere API: resta fuori scope
  V1. Il rifiuto è il contratto, non un TODO.

### F6 — Motion blur per-layer: richiede contratto gemello (2026-11-06)
- L'unico shutter authorato è quello di scena (`MotionScene::setShutter`,
  `MotionBlurPlan`). Un flag per-layer come in AE richiede un campo nel piano e
  un kernel C3D: non è implementabile in CM3D da solo. Resta dichiarato, non
  duplicato.

### S0 — Guardrail anti-rigrow della superficie camera (2026-10-31)

Regola vincolante per ogni nuova superficie camera. Non ridiscute le zone
CHIUSE C1–C7: rende meccanico il criterio con cui si accetta un nuovo file o
campo.

- **Lato CM3D (`motion/`)**: un nuovo header `Camera*` è ammesso solo come
  (a) preset-dato (`presets::`, tabella waypoint) o (b) servizio geometrico
  registrato in questa doc. Una mossa editoriale (recipe, sequence,
  trajectory, path, choreography, shot) è **contenuto**: entra in un preset o
  in un pack `ChrononTemplate`, non in un header di meccanismo. La firma
  `apply*(CameraRig&, ...)` fuori dal meccanismo unico non è ammessa.
- **Lato C3D**: il renderer possiede l'implementazione di proiezione, DoF e
  shutter; non possiede vocabolario editoriale (recipe/sequence/trajectory).
  Un nuovo campo camera è ammesso solo se (1) cambia i pixel, (2) ha un
  consumatore reale, (3) ha una sola unità authored sul confine. I dettagli
  di proiezione `camera_projection_{matrix,clip,frustum}` vivono in
  `math/detail/` e non sono API SDK.
- **Doppia verità vietata**: ogni campo trasportato ha gamme fail-closed
  identiche sui due lati (`camera_contract_problem` ↔ lowering validation),
  un solo punto di ammissione (`camera_contract.hpp` +
  `camera_projection_contract.hpp`) e un default che non arma kernel assenti.
- **Gate**: `chrononmotion_boundary` verde; `tools/check_public_headers.py`
  verde; evidenza consumer in
  `Chronon3d/docs/evidence/camera-surface-consolidation/`.

F3 (`planPosition` vs `planAnimationPosition`) è verificato localmente: il
canary Motion→RenderPlan→C3D mostra la card PNG rossa davanti al frame 0 e
dietro al frame 1 dopo l'attraversamento Z. S2 può quindi iniziare. La rimozione
wire S3 è implementata e i frame DoF raw software 30/75/149 sono byte-identici
al baseline; le due evidenze sono in
`Chronon3d/docs/evidence/camera-surface-consolidation/`.

Aggiornamento S2 (2026-11-02): `CameraSequence.hpp` e `CameraShake.hpp` sono
stati assorbiti in `CameraMotion.hpp`; handoff e shake mantengono i propri dati
e semantica sotto `motion::camera`, senza header pubblici separati. I consumer
implementation/test includono ora l'autorità camera consolidata. Passano
`chrononmotion_camera_sequence`, `chrononmotion_camera_shake`, authoring e
camera-bake (373 check authoring, 832 bake). La conta estesa 15→13 comprende
`AspectReframe` ed `EyeTrace`. Il target 13→≤6 e il confronto
lowering-per-lowering (S5) sono **demandati a v2.1** dalla chiusura
amministrativa del 2026-11-04
(`Chronon3d/docs/tickets/archive/TICKET-CAMERA-SURFACE-CONSOLIDATION.md`):
non sono lavoro aperto di questa milestone.

Per ogni piano V3 il lowering materializza `transform_samples` per schema:
usa il calendario shutter se attivo, altrimenti ogni frame di presentazione,
quindi non esiste più un path dense integer TRS V3. Il comportamento vale
anche per piani senza CameraRig; quando esiste un rig, camera e layer
condividono gli stessi timestamp. `requireResolvedLayerSamples` resta per
compatibilità sorgente e per i test di migrazione, ma non è più necessario per
V3. Template lo applica nel wrapper comune e i sei producer Motion censiti lo
richiedono esplicitamente, incluso Showreel camera-free. Image/Video/Color/
Text/Shape/Particle/Adjustment sono supportati; Light e Mesh restano esclusi
dai campioni risolti per le loro semantiche di consumo dedicate. I Null
controller non sono contenuti renderabili e non vanno serializzati come Color
trasparenti. Audit 2026-11-06: tutti i sei tool materializzati (`plan_showreel`,
`plan_map_flyover`, `plan_optical_studio`, `plan_documentary_showcase`,
`plan_particles_showcase`, `plan_word_rack_focus`) impostano il flag; il
default `false` resta per i piani V2. La demolition debt resta PARTIAL solo per
l'equivalenza del corpus consumer compatibility/typed e per la decisione sul
ritiro di V2.

## Regola di manutenzione

Ogni campo che passa il confine deve avere: (1) gamma fail-closed identica
sui due lati (C2), (2) un'unità authored unica (C1/C5), (3) default che non
armino feature non-kernelizzate (C4). Aggiornare questa doc nella stessa PR
che tocca il confine.
