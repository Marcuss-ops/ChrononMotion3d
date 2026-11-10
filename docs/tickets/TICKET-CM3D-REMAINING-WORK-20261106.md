# TICKET-CM3D-REMAINING-WORK — Cosa manca davvero per chiudere ogni feature

- **Stato:** OPEN — tracking (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D e confini Chronon3d / CI / host GPU
- **Owner:** ChrononMotion3D motion boundary, con owner C3D per i gate backend
- **Regola:** questo ticket non è una feature e non si chiude con una build. Si
  chiude solo quando ogni riga della matrice è `CLOSED` con evidenza datata,
  oppure è formalmente ritirata con owner e motivazione.

## Perché esiste questo ticket

I ticket chiusi il 2026-11-06 sono chiusure reali, ma sono in gran parte
**igiene, decisioni e golden**: build `-Werror`, verità documentale, audit dei
producer, decisioni di boundary, manifest showcase. Non chiudono le feature
grosse. Lo stato di autorità dichiara ancora questi gate aperti:

- `RenderPlan / highlight bridge` — **PARTIAL**;
- gate camera ADR **2–6** — **OPEN**;
- `THREEPP_CERTIFICATION` — **PARTIAL** su T01–T12 e T15, T13 parziale;
- `MATERIAL_PBR_V1` — sampling software V3 **fatto**; resta la lane Vulkan (E1);
- demolition debt del bake integer TRS — **V3 rimossa; resta la lane V2 legacy e l'equivalenza consumer**;
- S2/S5 — **DEFERRED v2.1**.

Il testo 2.5D è **C3D-owned** e non è lavoro CM3D: resta fuori da questo
backlog (§1).

Questo documento elenca, per ogni voce, il lavoro esatto che manca. Non
trasforma un `PARTIAL` in `PASS`, non dichiara chiuso un gate esterno e non
propone scoreggiatoie.

## Matrice di stato reale

| # | Feature / gate | Stato reale | Chiudibile solo in CM3D? | Dipendenza | Scheda |
|---|---|---|---|---|---|
| 1 | Build `-Werror` + target `plan_optical_remake` | CLOSED (clean build 737/737; 67/67) | Sì | nessuna | `TICKET-CM3D-BUILD-WERROR` |
| 2 | Golden showcase | CLOSED | Sì | nessuna | `TICKET-CM3D-SHOWCASE-GOLDEN` |
| 3 | Verità documentale | CLOSED, follow-up indice/S2 in §12 | Sì | indice canonico | `TICKET-CM3D-DOC-TRUTH` |
| 4 | Decisioni boundary F2/F4/F5/F6, S2/S5 | CLOSED per la parte decisionale | Sì | nessuna | `TICKET-CM3D-BOUNDARY-DECISIONS` |
| 5 | CI adapter full suite | CLOSED come codice; esecuzione GitHub in §12 | No | GitHub Actions | `TICKET-CM3D-CI-ADAPTER` |
| 6 | Producer resolved samples | CLOSED; V3 auto-risolta, demolition V2 in §8 | Parziale | corpus consumer | `TICKET-CM3D-RESOLVED-SAMPLES-AUDIT` |
| 7 | Testo 2.5D con camera | C3D-OWNED — nessun lavoro CM3D | N/A | C3D render/gel + GPU | `TICKET-TEXT-2_5D-CAMERA` |
| 8 | Shadow blur/opacity per-layer | BLOCKED | No | kernel shadow C3D | `TICKET-CM3D-SHADOW-EMISSION` |
| 9 | Motion blur per-layer | OPEN / contract-gated | No | contratto V3 + kernel C3D | `TICKET-CM3D-PER-LAYER-BLUR` |
| 10 | RenderPlan / highlight bridge | PARTIAL | No | C3D + CI same-SHA + GPU | §4 |
| 11 | Gate camera ADR 2–6 | OPEN | No | backend C3D + GPU + CI | §5 |
| 12 | `THREEPP_CERTIFICATION` T01–T15 | PARTIAL (T14 PASS, T13 parziale) | No | GPU NVIDIA + review umana | §6 |
| 13 | `MATERIAL_PBR_V1` sampling software V3 | OPEN lato C3D | No | renderer software C3D | §7 |
| 14 | S2/S5 camera surface | DEFERRED v2.1 | Sì, ma fuori milestone | decisione 2026-11-04 | §9 |
| 15 | Demolition debt integer TRS | PARTIAL — V3 rimossa; resta V2 legacy + equivalenza consumer | Parziale | contratto V2 / corpus consumer | §8 |
| 16 | Verifiche di chiusura (CI same-SHA, review umana; clean build/no-adapter fatti) | OPEN | Parziale | GitHub / host GPU | §12 |
| 17 | Precomp flattening verso RenderPlan | CLOSED (2026-11-06), luci annidate incluse | Sì | nessuna | `TICKET-CM3D-PRECOMP-FLATTENING` |

> La riga 7 è **C3D-owned**: resta solo come riferimento di confine, non è
> lavoro CM3D.

## 1. Testo 2.5D con camera — C3D-OWNED, nessun lavoro CM3D

Decisione del 2026-11-06: **ChrononMotion3D non deve fare nulla sul testo.**
Shaping, font stack, atlas MTSDF, raster, materiale gel, compositing, shadow,
DoF, lane software/Vulkan, sei stress test, golden raw, profiling 4K/120 e
review visiva sono tutti C3D. In CM3D il testo è solo un layer 2.5D con
`ContentRef` Text e usa il `Transform` comune di image/video; non esiste un
path testo separato. La scheda `TICKET-TEXT-2_5D-CAMERA.md` resta il
riferimento C3D, ma non contiene lavoro CM3D: niente fixture, niente golden,
niente modifiche di codice o test lato CM3D. L'emissione text già esistente
resta solo come contratto di trasporto che C3D consuma.

## 2. Shadow blur/opacity per-layer — BLOCKED

Scheda: `TICKET-CM3D-SHADOW-EMISSION.md`; boundary F4.

### Cosa è già fatto

- `Layer` authora e serializza `acceptsShadow`, `shadowOpacity`,
  `shadowBlur`.
- Import/update/validation/export e round-trip scena sono verdi lato CM3D.
- Il lowering emette solo `castsShadow`, `shadowRadius`, `shadowBias`.

### Cosa manca esattamente

1. **Kernel C3D per-layer** per `shadow_blur` e `shadow_opacity`. Non basta
   un campo nel wire: serve un consumatore che cambi i pixel.
2. **Capability dichiarata** dal consumer: il lowering deve emettere i due
   campi solo se la capability è presente; altrimenti deve fallire chiuso o
   non emettere, secondo il contratto.
3. **Unità e range condivisi**: blur in unità scena/pixel? opacità in [0,1];
   stessi limiti su producer e decoder, un solo punto di ammissione.
4. **Test CM3D**: emissione con capability, rifiuto senza capability, range
   non finiti/fuori range, round-trip scena.
5. **Test C3D**: pixel/parità per blur e opacità, inclusi bordi e
   sovrapposizioni; nessun alone o clipping.
6. **F4 aggiornato a CHIUSO** con evidenza `path:line` e pair di commit.

### Divieto di workaround

Non aggiungere `shadow_blur`/`shadow_opacity` al wire senza il kernel C3D.
Non mappare l'opacità sull'alpha del layer o il blur sul blur della scena.
Non dichiarare la feature chiusa perché il campo è serializzato.

## 3. Motion blur per-layer — OPEN / contract-gated

Scheda: `TICKET-CM3D-PER-LAYER-BLUR.md`; boundary F6.

### Cosa manca esattamente

1. **Contratto scritto**: nome del campo, unità (angolo shutter in gradi,
   campioni), default, interazione con lo shutter di scena, fail-closed.
2. **Modello authoring**: flag per-layer su `Layer`, serializzazione e
   import/export, senza duplicare `MotionScene::setShutter`.
3. **Lowering**: emissione del campo solo con capability presente; nessun
   default che armi un kernel assente.
4. **Kernel C3D**: accumulo shutter per-layer sul device, layout/lifecycle
   espliciti, nessuna lettura host nella lane strict.
5. **Parità**: software vs Vulkan, con soglia preregistrata.
6. **Test CM3D**: emissione, rifiuto fuori range, interazione con shutter di
   scena; test C3D di pixel/parità.
7. **F6 aggiornato a CHIUSO** con evidenza.

### Divieto di workaround

Non riusare lo shutter di scena come finto per-layer. Non aggiungere un flag
authoring che il piano non trasporta. Non dichiarare la feature chiusa con
solo il campo serializzato.

## 4. RenderPlan / highlight bridge — PARTIAL

Fonte: `docs/stato/chronon-motion3d.md` §3; `docs/BOUNDARY_C3D.md`.

### Cosa è già chiuso

- **Precomp visive:** flattening ricorsivo nel piano con ID path-qualified,
  composizione di transform/opacità/visibilità, time remap locale, rimappatura
  dei riferimenti interni e resolved samples V3; i casi non rappresentabili
  falliscono chiusi. Evidenza in
  `TICKET-CM3D-PRECOMP-FLATTENING-20261106.md`.
- Emissione V3 di maschere path, maschere ordinate con track parametri,
  track matte, effect parameter tracks; valori tipizzati e ordine
  deterministico.
- Rifiuto fail-closed di payload malformati e campi V2-incompatibili.
- Regressione adapter C3D: validazione/decodifica, matte, determinismo,
  maschere malformate, parametri non supportati, rifiuto V2.
- Rig-to-render inline mesh V3 a topologia corrispondente, UV/material e
  normali rigenerate; payload fuori budget respinto.
- Schema V3 inline mesh animation (1..4096 campioni, frame 0..1000000,
  positions 3D).
- C ABI 276/276, renderer-core 201/201, CLI software 1 px/231 px, raw sink
  corretto.

### Cosa manca esattamente

1. **CI same-SHA**: le prove sono locali, con alberi dirty o pair non
   congelati. Serve un job che usi lo stesso SHA di CM3D e C3D, senza
   modifiche non committate.
2. **Alpha edge e occlusione+DoF su scena animata completa**: il canary
   mesh/piano passa, ma non certifica alpha+DoF Vulkan su sequenza completa.
3. **Fault-injection nativa end-to-end**: errori GPU/allocazione/sync non
   sono iniettati e verificati sulla lane strict.
4. **Depth per-pixel della mesh inline verso il compositing C3D**: la mesh
   testurizzata/alpha è disegnata, ma la depth per-pixel nel compositing non
   è certificata.
5. **Copertura PBR/produzione oltre la fixture**: il percorso PBR è provato
   su GLB campione, non su materiali di produzione.
6. **Review umana** del video completo e dei bordi alpha.
7. **CTest completa sul pair corrente**: localmente 67/67, ma il pair non è
   same-SHA e gli alberi non sono puliti.
8. **Rotta stills del consumer**: `-o file.png` non disegna la mesh inline
   (frame vuoto senza errore); è un difetto C3D tracciato in
   `docs/stato/chronon3d.md` §11. Non è un residuo CM3D, ma blocca
   l'accettazione del bridge.
9. **`MATERIAL_PBR_V1`**: il sampling software C3D della texture
   metallic-roughness V3 è **fatto**; resta la lane Vulkan, che ignora la
   texture MR e valida il materiale solo per-vertex
   ([E1](../../../Chronon3d/docs/tickets/TICKET-VULKAN-INLINE-MESH-PBR-20261109.md), §7).
10. **Ticket C3D aperti**: `TICKET-RENDER-PLAN-FEATURE-ACCEPTANCE` e
    `TICKET-RENDER-PLAN-REMAINING-FEATURES` restano PARTIAL.

### Divieto di workaround

Non dichiarare same-SHA usando un albero dirty. Non sostituire la review
umana con un contact sheet. Non promuovere un canary CPU a prova Vulkan. Non
ri-baselinare un golden per nascondere un bordo alpha.

## 5. Gate camera ADR 2–6 — OPEN

Fonte: `docs/ADR-CAMERA-PIPELINE-OWNERSHIP.md`. Il gate 1 è chiuso; i gate
2–6 no.

### Cosa manca esattamente

1. **Gate 2 — backend sottile.** C3D deve consumare solo il piano risolto.
   Spostare in Motion validazione, policy e calendario oggi duplicati o
   decisi dal consumer; lasciare in C3D kernel, risorse e sincronizzazione
   GPU. Nessuna seconda interpretazione del piano.
2. **Gate 3 — compositing alpha/depth.** Implementare nel backend
   l'esecuzione delle primitive del piano con contributi per pixel;
   verificare sovrapposizioni e DoF insieme, con oracle CPU e Vulkan
   strict-native.
3. **Gate 4 — shutter GPU.** Accumulare i campioni sul device, con
   layout/lifecycle espliciti e nessuna lettura host. Il sink strict riceve
   solo superfici native valide; fallback e readback sono errori.
4. **Gate 5 — acceptance del prodotto.** Stessa scena Motion e stessi asset
   per lowering offline e provider runtime; random access; 150 frame
   1920×1080; zero errori Vulkan; metriche raw/video; CI sullo stesso pair
   di SHA; review visiva.
5. **Gate 6 — rimozione legacy.** Eliminare campi, adapter e pass semantici
   C3D solo dopo il gate equivalente; nessuna API di compatibilità
   permanente.

### Divieto di workaround

Non promuovere l'ADR a `implemented` perché il gate 1 è chiuso. Non
duplicare projection/DoF/shutter in `render/` per aggirare C3D. Non
dichiarare il gate 5 chiuso con una build o un exit code.

## 6. `THREEPP_CERTIFICATION` — PARTIAL

Fonte: `docs/THREEPP_CERTIFICATION.md`. La certificazione richiede il
percorso di produzione, ispezione di immagini/video e gate numerici; una
classe o uno shader presenti non sono un PASS. Tutti i casi Linux-only
richiedono NVIDIA EGL/GL e NVENC reali, non llvmpipe/software.

### Cosa manca, caso per caso

- **T01 Geometry & camera — PARTIAL.** Manca una golden affidabile di
  ordinamento facce/clipping e una scena di qualità produzione. Il clip
  NVIDIA 150 frame e il random access passano.
- **T02 UV & textures — PARTIAL.** Manca la golden full-frame
  dell'orientamento UV, una tolleranza di filtering tra driver GPU e una
  camera/framing di qualità professionale. I confronti mip/nearest e i
  150 frame passano.
- **T03 PBR materials — PARTIAL.** Manca una golden esterna affidabile
  (Khronos sample GLB) e la qualità di presentazione. Il baseline di
  encoder-drift è misurato; non è una certificazione PBR.
- **T04 Lighting — PARTIAL.** Esiste solo l'orbita base della luce; mancano
  illuminazione di produzione, ombre e review del playback completo.
- **T05 Shadow mapping — PARTIAL.** Il percorso threepp/NVIDIA passa; manca
  un pass shadow nativo C3D e la revisione della composizione di produzione.
  Il `ShadowNode` CPU attuale è rifiutato da strict-native con
  `UnsupportedCapability`.
- **T06 Full GLB import — PARTIAL.** Mancano golden di orientamento,
  texture sampling e PBR; manca la prova end-to-end di re-decode e
  compositing C3D.
- **T07 Skeleton animation — PARTIAL.** Manca la verifica del timing dei
  precomp annidati e la review completa del playback/art direction.
- **T07-BLEND — PARTIAL.** Il crossfade è dimostrato; mancano playback
  completo e art direction professionale.
- **T07-HUMANOID-SHADOW — PARTIAL.** I gate tecnici passano; mancano review
  del playback e qualità materiale/ombre.
- **T08 Morph targets — PARTIAL.** Manca il caso misto skin+morph, una
  validazione più ampia dei target e un asset più espressivo.
- **T09 Camera & depth — PARTIAL.** Mancano una golden affidabile di
  camera/clipping proiettata e una composizione di produzione.
- **T10 Depth of field — PARTIAL.** Mancano soggetti più grandi, review del
  playback e integrazione nel compositing C3D.
- **T11 Post-processing — PARTIAL.** Manca l'integrazione del bloom nella
  composizione di produzione C3D e la review visiva.
- **T12 Documentary composition — PARTIAL.** Mancano art direction, dettaglio
  texture, review dell'intera sequenza e integrazione con grafica Chronon
  più ricca.
- **T13 3D + Chronon compositing — PASS opaque, PASS alpha-PNG, PARTIAL
  animated alpha video.** Manca il decode video alpha GPU-native continuo,
  una rotta di encode alpha nativa, l'isolamento della provenienza del testo
  e una sorgente alpha-capable GPU-native. qtrle CPU non certifica.
- **T14 Headless GPU rendering — PASS.** Nessun blocker di qualità; restano
  solo note di telemetria (VRAM/utilizzazione non attribuite per processo).
- **T15 Production integration — PARTIAL.** T12 è un clip pre-renderizzato,
  non ingestione GLB diretta dentro un singolo job Chronon. Serve
  l'ingestione in-job e la review di produzione.

### Divieto di workaround

Non promuovere un caso a PASS perché il video esiste. Non usare un render
software/llvmpipe per un gate GPU. Non usare un contact sheet al posto della
review del playback. Non dichiarare T13 completo senza decode alpha
GPU-native.

## 7. `MATERIAL_PBR_V1` — OPEN lato C3D (solo lane Vulkan)

Fonte: `docs/MATERIAL_PBR_V1.md`.

- Il producer CM3D emette i fattori PBR, le texture base-color e
  metallic-roughness, il fingerprint e i limiti condivisi.
- Il renderer software C3D campiona base-color e alpha, applica il cutoff
  MASK e campiona la texture metallic-roughness V3 inline (canale G roughness,
  B metallic: `software_mesh_processor.cpp:101-132`). La formulazione
  precedente di questo ticket diceva il contrario ed era stale.
- **Manca:** la stessa cosa sulla lane **Vulkan** strict-native, che usa solo i
  fattori scalari per-vertex, ignora la texture MR e non copia emissive
  ([E1](../../../Chronon3d/docs/tickets/TICKET-VULKAN-INLINE-MESH-PBR-20261109.md)).
  Non è un lavoro CM3D.
- **Fuori scope dichiarato:** normal e occlusion texture. Non vanno
  aggiunte senza un ticket e un contratto dedicati.

## 8. Demolition debt — integer transform bake — PARTIAL (V3 rimossa)

Fonte: `docs/BOUNDARY_C3D.md`, sezione “Demolition debt — integer transform
bake”.

### Cosa è già fatto

- Tutti e sei i producer materializzati impostano
  `requireResolvedLayerSamples = true`.
- Il lowering V3 materializza sempre i campioni risolti, anche senza il flag:
  il calendario shutter ha la precedenza, altrimenti usa ogni frame di
  presentazione. Il blocco dense integer TRS in `RenderPlanLoweringSamples`
  viene eseguito solo per V2.
- Un mesh V3 statico resta ammesso; un mesh con transform/opacità animate
  fallisce chiuso. Light e Mesh non ricevono `transform_samples`.
- `sameLoweredPlan` confronta anche i campioni risolti e la parità easing V3
  non legge più `animation`.
- Il default `false` resta per compatibilità sorgente e per i piani V2.

### Cosa resta esattamente

1. **Equivalenza del corpus compatibility/typed** per tutti i payload
   supportati. Non basta che i producer siano stati aggiornati o che V3 non
   usi più il bake denso.
2. **Equivalence gate**: camera bake roundtrip + consumer typed raw pixel
   gate, con evidenza same-SHA.
3. **Decisione sul ritiro di V2**: V2 non trasporta `transform_samples`;
   eliminare anche la sua lane dense richiede un cambio di contratto o la
   rimozione dello schema. Fino a quella decisione la lane V2 resta legacy
   esplicita, non un fallback di V3.

### Divieto di workaround

Non dichiarare chiusa l'equivalenza consumer perché V3 non emette più il bake
denso. Non eliminare il formato V2 prima dell'equivalenza e della decisione di
contratto. Non confrontare solo gli SHA dei piani senza il gate pixel
consumer.

## 9. S2/S5 camera surface — DEFERRED v2.1

Fonte: `docs/BOUNDARY_C3D.md`; closeout amministrativo 2026-11-04.

- **Stato attuale:** 15→13 header dopo l'assorbimento di `CameraSequence` e
  `CameraShake` in `CameraMotion`; restano 13.
- **S2:** target 13→≤6 header; migrazione degli applier ai preset tipizzati.
- **S5:** confronto lowering-vs-lowering.
- **Decisione:** demandati a v2.1, non lavoro aperto della milestone
  corrente. Non sono “chiusi”: sono rinviati con owner e data.
- **Per chiuderli definitivamente:** riaprire la milestone v2.1, ridurre la
  superficie a ≤6 header, migrare gli applier, implementare S5, aggiornare
  census/evidenza e rifare i test camera/bake.

### Nota di coerenza

`CHRONONMOTION.md` diceva ancora “the applier-to-typed-preset migration and
target of ≤6 remain open”. È stato corretto in questo stesso intervento:
ora dice che sono demandati a v2.1, coerente con `BOUNDARY_C3D.md` e con lo
stato di autorità.

## 10. Cosa NON è un residuo

- **F2 easing:** chiuso come decisione; il lowering bakea lineare, il piano
  non trasporta easing per-key, la parità è pinnata da
  `motion_renderplan_easing_parity.cpp` (11 tipi).
- **F5 roving key:** fuori scope V1, fail-closed; il rifiuto è il contratto.
- **S2/S5:** rinviati a v2.1, non lavoro corrente.
- **Testo volumetrico/ray-tracing, materiali C4D, text-on-path, AO,
  parità universale AE:** fuori perimetro dichiarato.
- **Normal/occlusion texture PBR:** fuori scope dichiarato.

## 11. Ordine di chiusura consigliato

1. **P0 — doc e indice:** rigenerare `docs/stato/README.md`, correggere S2 e
   aggiungere il ticket testo al registro (§12).
2. **P1 — verifiche CM3D:** clean build `release-fast`; lane senza adapter;
   CTest completa sul tree pulito.
3. **P2 — emissione CM3D pronta:** test di shadow blur/opacity e per-layer
   blur con capability simulata, senza dichiarare la feature chiusa.
4. **P3 — C3D:** kernel shadow per-layer, contratto shutter per-layer,
   backend sottile, compositing alpha/depth, shutter GPU.
5. **P5 — bridge/ADR:** CI same-SHA, review umana, fault injection,
   rimozione legacy.
6. **P6 — THREEPP:** certificazione GPU/review dei casi PARTIAL.
7. **P7 — S2/S5:** solo in v2.1.

**Testo 2.5D:** C3D-owned; non entra nell'ordine CM3D (§1).

## 12. Verifiche di chiusura e gap di verifica

### Fix applicati il 2026-11-06

- [x] Indice canonico rigenerato con
      `python3 ops/tools/reconcile_tickets.py --write`; `--check` verde.
- [x] `CHRONONMOTION.md` allineato a “S2/S5 deferred v2.1”.
- [x] `TICKET-TEXT-2_5D-CAMERA.md` registrato come **C3D-owned**;
      nessun lavoro CM3D.
- [x] Questo ticket aggiunto al registro e all'indice.
- [x] Dense TRS V3 rimosso e documentato in
      `TICKET-CM3D-DENSE-TRS-20261106.md`; la lane V2 resta legacy.

### Gap ancora aperti

- [x] **Clean build** `build/release-fast` (`--clean-first`) eseguita il
      2026-11-06: `[737/737]`, CTest 67/67 in 43.90 s.
- [x] **Lane senza adapter** eseguita il 2026-11-06 su
      `build/release-noadapter`: build 117/117; `chrononmotion_showcase_all`
      e `chrononmotion_showcase_catalog` PASS, `renderplan_roundtrip`
      Disabled; `--all` → 62/62 PASS, 1/63 SKIP, 0 FAIL, 53.970 asserzioni.
- [ ] **GitHub CI same-SHA** non eseguita: la corsia adapter è codice, non
      un risultato CI.
- [ ] **Pair CM3D/C3D pulito e congelato** non disponibile: le prove bridge
      sono locali e con alberi dirty.
- [ ] **Review umana** dei video T13/T15 e della resa gel non eseguita.

## 13. Definizione di “chiuso definitivamente”

Una riga è chiusa solo quando:

1. tutti i criteri della sua scheda sono `[x]` con evidenza datata;
2. l'evidenza è riproducibile sul pair di commit dichiarato;
3. il test negativo (fail-closed) esiste e passa;
4. il gate pixel/parità o la review visiva richiesta è passato;
5. nessun workaround è usato per aggirare un kernel, una lane o una CI
   mancante;
6. la scheda e l'indice canonico sono aggiornati.

Fino a quel momento, lo stato resta `OPEN`, `PARTIAL` o `BLOCKED`, non
`CLOSED` per convenienza.
