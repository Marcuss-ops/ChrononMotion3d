# Frontiera CM3D ↔ Chronon3d: stato di unificazione (blur / DOF / easing / 2.5D)

> Doc unica di verità del confine tra `ChrononMotion3D` (authoring motion,
> senza renderer, fork di threepp) e `Chronon3d` (renderer Vulkan/software che
> consuma i RenderPlan). Ogni voce ha evidence `path:line` nel tree al
> 2026-10-28. Le zone **CHIUSE** sono implementate e verificate; le zone
> **FUTURE-SCOPE** sono dichiarate e NON vanno reimplementate in parallelo.

## Ruoli (perché non c'è duplicazione di renderer)

| | CM3D | Chronon3d |
|---|---|---|
| Ruolo | authoring/motion (no renderer) | renderer, consuma RenderPlan |
| Canale verso l'altro | lowering → `chrononmotion_chronon3d_adapter` (opt-in, CMakeLists.txt:159-190) | decode + compile + render graph |

## Zone CHIUSE

### C1 — Apertura: f/stop come unica verità authored
- `CameraRig.hpp:209-233`: bridge "DOF unification"; il legacy gain non è più
  authored né trasportato; `gain = 0.672/f_stop` è solo unità interna della
  preview GL. Il quantificatore 0.672 esiste SOLO lato CM3D (grep cross-repo
  2026-10-28: nessun altro sito).

### C2 — Lowering fisico V3 implementato
- `RenderPlanLoweringCamera.cpp:99-118`: `focal_length_mm`, `f_stop`,
  `dof_f_stop`, `dof_iris_blades`, `dof_highlight_gain` emessi dal rig;
  `dof_aperture` solo derivato via `blurGainFromFStop`, mai authored.
- Fail-closed speculare: `lowerCameraPose` (range f/stop [0.7, 32], iris [0, 12],
  gain [0, 8], DOF con aperture ⇒ focus distance > 0) e decoder C3D
  `render_plan_decoder_support.cpp:244-257` valida le stesse gamme.

### C3 — Kernel shaped-bokeh: fail-closed su TUTTI i percorsi render
- `dof.hpp:14-40`: `reject_unsupported_dof_shape` lanciano su
  `iris_blades`/`highlight_gain` > 0 (software + Vulkan `dof_blur.comp`,
  bokeh circolare separabile solo).
- Call site: `per_pixel_dof_node.cpp:342`.

### C4 — Default di sequenza coerenti col contratto (fix 2026-10-28)
- `CameraFocusSequence.hpp`: `highlightGain` default **0.f** (era 1.f — un
  preset di default produceva un piano che il renderer rifiuta);
  `irisBlades` già 0. Pin di contratto nel test
  `tests/motion_camera_focus_sequence.cpp` (default FocusSequence → pose
  highlightGain == 0).

### C5 — Semantic of dof_aperture pinned (fix 2026-10-28)
- `render_plan_layer_types.hpp` (CameraPlan::dof_aperture): commento LEGACY
  TRANSPORT FIELD — solo derivato da f_stop, mai authored; `dof_f_stop` è
  l'unica unità authored.

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

### Contratto camera/layer risolti — aggiornamento 2026-10-30

- Motion risolve local → world con parent/Null, look-at, roll ed easing. Il
  confine riflette Z (`S = diag(1,1,-1)`); per la camera `R_c3d = S R_motion S`.
  Le rotazioni renderer sono gradi, ricostruite nell'ordine `qZ*qY*qX`.
- `camera_contract.hpp` è l'ammissione condivisa producer/decoder/compiler:
  FOV verticale 10–120°, focale 0 oppure 12–200 mm, sensore 1–100 mm,
  f-stop 0 oppure 0.7–32. Focale positiva precede FOV nella proiezione;
  focus distance è distanza positiva lungo l'asse ottico nelle unità scena.
  `dof_aperture` rimane gain legacy derivato, non un secondo f-stop authored.
  Iris blades e highlight gain diversi da zero sono rifiutati prima del render.
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
- Supporto iniziale campioni layer: Image/Color/Text/Shape senza tracce extra;
  altre capability falliscono esplicitamente. Questo estende il contratto
  esistente senza una nuova authority di regia o una cache parallela.

#### Demolition debt — integer transform bake
- Owner: manutentore del confine Motion/RenderPlan.
- Reason: i job senza shutter e senza lista esplicita mantengono il formato
  storico a keyframe interi; non sono dichiarati exact sub-frame.
- Exit condition: tutti i producer esigono campioni risolti e il corpus
  compatibility/typed ha equivalenza verificata per i payload supportati.
- Equivalence gate: camera bake roundtrip + consumer typed raw pixel gate.
- Removal scope: emissione dense integer TRS in RenderPlanLoweringSamples,
  non le AnimationTrack authored generiche del renderer.
- Status: ACTIVE. Il passaggio non elimina il renderer DoF C3D.

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

### F2 — Easing: tre vocabolari
- C3D `easing.hpp` (enum → GLM), CM3D `motion/Easing.hpp` (CubicBezier class),
  CM3D `extras/core/Interpolations.hpp` (eredietà threepp). Il mapping va
  definito come contratto lato lowering quando servono easing per-key
  (oggi il lowering bake linear samples — RenderPlanEmission.hpp:43-44).

### F4 — Layer shadow: authored e serializzato, non lowering-rizzato
- `Layer.hpp:146-148` e `CompiledScene.hpp:36-38` trasportano
  `acceptsShadow`/`shadowOpacity`/`shadowBlur`: sono dati autorevoli del
  formato scena (import/update/validation/export) e NON codice morto — ma
  il lowering al RenderPlan consume solo
  `castsShadow`/`shadowRadius`/`shadowBias` (RenderPlanLowering.cpp:224-226).
  Chiusura = aggiungere emissione `shadow_blur/shadow_opacity` al lowering
  quando C3D espone i relativi per-layer kernel; NON duplicare i campi altrove.

## Regola di manutenzione

Ogni campo che passa il confine deve avere: (1) gamma fail-closed identica
sui due lati (C2), (2) un'unità authored unica (C1/C5), (3) default che non
armino feature non-kernelizzate (C4). Aggiornare questa doc nella stessa PR
che tocca il confine.
