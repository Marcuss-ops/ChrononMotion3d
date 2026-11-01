# ADR: pipeline camera e compositing sotto ChrononMotion3D

- **Stato:** accettato come direzione architetturale; migrazione non completata.
- **Data:** 2026-11-01.
- **Ambito:** camera, timeline dei campioni, layer visuali 2.5D, alpha/depth,
  DoF, shutter, compositing e acceptance video.
- **Sostituisce:** per l'ambito camera, le regole di ownership opposte nella
  sezione S0 e nel closeout camera di `BOUNDARY_C3D.md`. Le altre regole del
  documento restano valide finché non sono aggiornate esplicitamente.

## Decisione

ChrononMotion3D è l'unica autorità semantica e di prodotto per la pipeline
camera. Mantiene la scena authoring, la gerarchia e la valutazione world, il
calendario temporale, il modello layer, l'ordine e la semantica di alpha/depth,
la policy di DoF e shutter, la composizione e i criteri di accettazione. Ogni
decisione che cambia *quali pixel* deve produrre una scena appartiene qui.

Chronon3d è un backend di esecuzione pixel/GPU. Può contenere rasterizer,
shader, allocazione e transizioni delle risorse, sincronizzazione Vulkan,
kernel di blur/accumulo e primitive di compositing necessarie per eseguire il
piano già risolto da Motion. Non possiede `CameraRig`, timeline, gerarchia,
layer authoring, ordinamento editoriale, policy di ottica, fallback semantici o
una seconda interpretazione del RenderPlan. Il backend riceve dati risolti e
restituisce superfici/frame; non richiama Motion durante il render.

`render/` in ChrononMotion3D non diventa un secondo renderer di produzione.
Resta un harness di look-dev e certificazione. Non si portano in parallelo
threepp `EffectComposer`, GTAO/DoF o un catalogo di effetti nel prodotto per
aggirare un difetto del backend C3D. Un solo backend alla volta viene usato
dalla pipeline di produzione e viene selezionato dietro un'interfaccia di
esecuzione priva di semantica camera.

## Confine consentito

Il confine è un **piano di frame risolto**, materializzato interamente prima
dell'esecuzione. Contiene solo valori immutabili e versionati: dimensioni e
formato target, timestamp esatti, trasformazioni world dei contenuti, input
risorsa, ordine, alpha/blend, profondità camera-space quando disponibile,
parametri ottici ammessi e campioni shutter. Non contiene callback, puntatori
alla scena Motion, chiavi editoriali, preset, ricette o policy implicite.

Motion è l'unico producer del piano e della sua semantica. Il backend C3D
valida la capacità tecnica disponibile e fallisce con errore tipizzato se non
può eseguire un campo richiesto; non modifica silenziosamente il piano, non
cade su CPU senza dichiararlo e non inventa default ottici.

## Invarianti anti-duplicazione

1. Un solo modello authoring di scena/layer/camera: ChrononMotion3D.
2. Un solo calendario e una sola valutazione temporale: ChrononMotion3D.
3. Un solo piano risolto e una sola interpretazione delle sue unità.
4. C3D può implementare più backend/kernel, ma nessun secondo modello
   semantico o editoriale.
5. Il codice threepp in `render/` resta prova/look-dev; non viene promosso a
   produzione mentre esiste il backend C3D per la stessa pipeline.
6. Nessuna copia di nodi, layer, pass o policy in entrambi i repository. Una
   capability semantica vive in Motion; l'implementazione device-specific vive
   in C3D e viene invocata attraverso il piano risolto.

## Migrazione

La migrazione avviene per gate, mantenendo il backend corrente fino a quando
il sostituto ha prove equivalenti. Non si chiude il ticket camera dichiarando
la sola build come accettazione.

1. **Contratto unico:** definire il frame plan versionato e togliere dal
   contratto ogni stato/callback runtime; Motion produce camera e layer agli
   stessi timestamp esatti, inclusi i campioni shutter.
   **Fatto (2026-11-01):** `chrononmotion::plan::ResolvedFramePlan`,
   schema `chrononmotion.resolved-frame-plan.v1`, prodotto da
   `resolveFramePlan()` e verificato da `verifyFramePlan()`. Contratto in
   `docs/RESOLVED_FRAME_PLAN.md` e
   `contracts/chrononmotion.resolved-frame-plan.v1.json`; il documento del
   fixture è committato in `tests/frame_plan_golden.json`.
   Calendario unico: `frame` (anche negativo) piu' `sub_frame_units` interi in
   milionesimi di frame; `seconds` è derivato e verificato. Il test
   `chrononmotion_frame_plan` (179 check) confronta ogni campione layer, bit per
   bit, con `scene.evaluate(N)` allo stesso frame, quindi un piano spostato di
   mezzo frame fallisce. Restano fuori da questo gate i gate 2-6.
2. **Backend sottile:** C3D consuma soltanto il piano risolto. Spostare in
   Motion validazione, policy e calendario che oggi sono duplicati o decisi
   dal consumer; lasciare in C3D kernel, risorse e sincronizzazione GPU.
3. **Compositing alpha/depth:** implementare nel backend l'esecuzione delle
   primitive richieste dal piano con contributi per pixel; verificare
   sovrapposizioni e DoF assieme, CPU oracle e Vulkan strict-native.
4. **Shutter GPU:** accumulare i campioni sul device, con layout/lifecycle
   espliciti e nessuna lettura host. Il sink riceve solo superfici native
   valide; fallback e readback sono errori quando è richiesta la lane strict.
5. **Acceptance del prodotto:** stessa scena Motion e stessi asset per
   lowering offline e provider runtime, random-access, 150 frame 1920x1080,
   zero errori Vulkan, metriche raw/video, CI sullo stesso pair di SHA e review
   visiva.
6. **Rimozione del legacy:** eliminare i campi, adapter e pass semantici C3D
   soltanto dopo il gate equivalente; nessuna seconda API di compatibilità
   permanente.

## Stato e lavoro aperto

Il gate 1 (contratto del piano risolto) è chiuso e verificato localmente; i gate
2-6 no.

Questa decisione sposta la proprietà semantica, ma non finge che il codice sia
già migrato. I fix e i test presenti nel working tree restano evidenza locale
del backend e non cambiano da soli il confine. Prima di promuovere l'ADR a
`implemented`, devono passare tutti i gate del ticket camera C3D sul medesimo
pair di commit, oppure devono essere sostituiti da gate equivalenti della
nuova pipeline. In particolare, il canary CPU mesh/piano non certifica alpha+DoF
Vulkan, shutter strict-native, 1080p, CI o review visiva.

## Conseguenze

- Motion resta il posto dove si risolve il significato della scena e della
  camera; il backend non riceve decisioni editoriali.
- Il RenderPlan esistente è un trasporto/transizione e non diventa una seconda
  autorità. L'adapter C3D resta opzionale e isolato dal core Motion.
- I problemi di layout Vulkan, sink e lifetime rimangono lavoro backend C3D;
  non si duplicano in threepp.
- Finché i gate GPU non passano, la pipeline può essere implementata ma non
  dichiarata chiusa né accettata per produzione.
