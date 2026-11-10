# Resolved frame plan — contratto v1

**Stato:** implementato, gate 1 di `ADR-CAMERA-PIPELINE-OWNERSHIP`.
**Schema:** `chrononmotion.resolved-frame-plan.v1`
**Codice:** `include/chrononmotion/plan/ResolvedFramePlan.hpp`, `src/chrononmotion/plan/`
**Contratto macchina:** `contracts/chrononmotion.resolved-frame-plan.v1.json`
**Golden del documento:** `tests/frame_plan_golden.json`
**Test:** `chrononmotion_frame_plan` (179 check)

## Perché esiste

L'ADR sposta la proprietà semantica della camera in Motion: il backend non deve
più ricevere una scena più callback e ricavarne da solo camera, layer, shutter e
calendario. Il frame plan è quella risoluzione, materializzata in un valore:

- **un solo calendario** per camera, layer e shutter, invece di tre timestamp
  quasi uguali calcolati da tre posti diversi;
- **nessun puntatore** a `MotionScene`, nessuna track, nessuna policy da
  ri-derivare: chi esegue legge numeri;
- **un rifiuto tipizzato** per ogni regola, invece di una riparazione silenziosa.

`resolveFramePlan()` è l'unico produttore. Non esiste un secondo percorso.

## Cosa contiene

| campo | significato |
|---|---|
| `schema`, `space` | identità del contratto; un consumer accetta solo questi valori |
| `plan_id` | nome della scena (o quello richiesto) |
| `canvas`, `fps`, `frames` | canvas, rate razionale, prima frame e conteggio |
| `timeline[]` | una voce per frame: `frame`, `sub_frame_units`, `seconds` |
| `shutter` | angolo e finestra di campionamento in unità sub-frame, o `null` |
| `camera[]` | una posa per frame: posizione, quaternione, fov, ottica |
| `layers[]` | per layer: identità, genitore, ordine e una matrice mondo per frame |
| `composite_order[]` | solo in depth mode: per frame, gli id drawable visibili dal fondo al fronte |
| `digest` | FNV-1a-64 del contenuto risolto |

Non contiene: content bytes, font, pixel, geometria, materiali, luci, pass,
policy di authoring, timer o callback. Un consumer che ha bisogno di qualcosa
che non è qui sta chiedendo al contratto sbagliato, non al piano.

## Calendario

Il rate è **razionale** (`24000/1001`, `24/1`, `30000/1001`, …). Il tempo
sub-frame è un **intero** in milionesimi di frame (`kSubFrameUnits = 1e6`), non un
float: due producer che chiedono lo stesso istante arrivano allo stesso bit.

```
seconds = (frame + sub_frame_units / 1e6) * denominator / numerator
```

`seconds` è **derivato** e verificato contro questa formula: non è mai
l'autorità. L'autorità è `(frame, sub_frame_units)` più il rate razionale.

L'istante di valutazione usa la stessa coppia `(frame, frazione)` con il rate
della scena, senza moltiplicare per un float: è ciò che rende vero
l'invariante centrale del test, cioè che il campione al frame N è **bit per bit**
ciò che `scene.evaluate(N)` produce. Un piano mezzo frame fuori passa ogni check
di autoconsistenza e fallisce quello.

`frame` può essere negativo (pre-roll con shutter centrato): l'ordinamento del
timeline è stretto e crescente, non "non negativo".

## Spazio e matrici

- spazio: destrorso, Y up, -Z avanti, unità Motion; nessuna conversione di unità
  e nessun mirroring applicato dal producer (`chrononmotion-world/...`);
- quaternioni `(x, y, z, w)`, normalizzati, con look-at e roll già applicati;
- matrici mondo **row-major**, 16 float, traslazione agli indici 3, 7, 11.
  Motion memorizza column-major: il producer traspone, e il golden lo pinna.

## Ordine di compositing

Il campo `composite_order` è **opzionale** e compare solo quando la scena
dichiara `compositeMode: depth` (contratto in
[COMPOSITING.md](COMPOSITING.md)). Contiene una voce per frame, ciascuna con
gli id dei layer drawable visibili a quell'istante, **dal fondo al fronte**.

Regole verificate dal consumer:

- una voce per ogni entrata del `timeline`;
- nessun id duplicato, sconosciuto, Null o non visibile a quel frame;
- la lista contiene esattamente tutti i layer non-Null visibili a quel frame.

In `Stack` il campo è assente: `layers[]` è già l'ordine, e il documento resta
byte-identico a quello emesso prima del contratto. Il digest copre
`composite_order` quando presente. La decisione non è un flag del renderer: è
un valore risolto da `resolveCompositeStack()` sulla scena già valutata e sulla
posa camera mondo.

## Shutter

La finestra è simmetrica attorno al frame, estremi inclusi: 180° a 12 campioni è
`-250000 … +250000` unità (`±0.25` frame). Gli offset sono relativi al centro
del frame, quindi un campione può cadere nel frame precedente.

Un angolo così stretto da non contenere campioni distinti a risoluzione 1e-6
viene **rifiutato**, non collassato in offset duplicati.

## Regole di rifiuto

`verifyFramePlan()` restituisce il primo problema con codice e dettaglio
localizzato (`camera[3].lens.f_stop is 0.2, expected 0 (not authored) or
[0.7, 32]`). Codici: `schema`, `space`, `canvas`, `frame_rate`, `timeline_empty`,
`timeline_size`, `timeline_order`, `timeline_sub_frame`, `timeline_seconds`,
`shutter_shape`, `shutter_offsets`, `shutter_coverage`, `camera_count`,
`camera_time`, `camera_pose`, `camera_optics`, `layer_order`, `layer_parent`,
`layer_kind`, `layer_asset`, `layer_samples`, `layer_timestamp`,
`layer_transform`, `layer_opacity`, `composite_order`, `digest`.

Casi che restano espliciti e non vengono "aggiustati":

- **opacità composta fuori da [0, 1]** → rifiuto. È un errore di authoring; un
  clamp qui sarebbe una bugia eseguita dal renderer.
- **layer fuori vita** → resta nel piano con `visible: false`. Il piano non
  elimina layer: la decisione editoriale è già avvenuta, il backend la esegue.
- **controller esclusi** (`includeNullLayers: false`) → i figli vengono
  ri-basati sull'antenato incluso più vicino. Le matrici mondo sono già
  composte, quindi il genitore è gerarchia informativa, mai ricalcolo.
- **rate richiesto diverso da quello della scena** → rifiuto: due rate in un
  piano sono il modo in cui un frame finisce mezzo frame fuori in un consumer.

## Determinismo

- `resolvedFramePlanToJson(plan, indent)` produce byte identici per lo stesso
  piano; i numeri sono il decimale più corto che rilegge lo stesso float, in
  notazione decimale (non `1.8e+02`) per le grandezze del contratto.
- `computeFramePlanDigest()` è FNV-1a-64 sulla forma canonica (indent 0, **senza**
  il campo `digest`), quindi un consumer può ricalcolarlo e accorgersi di un
  piano manomesso.
- Il writer rifiuta un piano non valido: non esiste un percorso che emetta un
  documento fuori contratto.
- Due run su scene identiche producono lo stesso documento e lo stesso digest;
  il documento è committato come golden e rigenerato solo con `--dump-golden`.

## Cosa deve fare un consumer

1. leggere `schema` e `space`, e rifiutare qualunque altro valore;
2. ricalcolare `digest` e confrontarlo;
3. eseguire i numeri così come sono: nessuna ri-derivazione di fov da focale,
   nessuna ri-composizione di gerarchie, nessun ricalcolo di calendario;
4. su un `visible: false`, saltare il layer senza rimuoverlo dal conteggio;
5. espandere la finestra shutter con i propri campioni, agli istanti indicati.

## Evidenza locale

`chrononmotion_frame_plan`: 179 check, 0 failure.

- frame grid: confronto bit per bit contro `scene.evaluate(N)` per ogni layer e
  ogni frame, incluso pre-roll negativo;
- traslazione row-major verificata in modo indipendente (indici 3/7/11);
- opacità composta 0.5 × 0.5 = 0.25;
- shutter: estremi `±250000`, ordine stretto, simmetria entro 1 unità;
- determinismo: due scene identiche → stessi byte, stesso digest; ri-resolve
  sulla stessa scena non cambia nulla;
- golden: modifica di un byte del documento committato → fallisce indicando riga
  e byte;
- 29 casi negativi, uno per regola dichiarata, con codice atteso;
- producer: rifiuto di canvas nullo, zero frame, rate non corrispondente,
  finestra shutter troppo stretta, opacità fuori range, genitore camera
  inesistente; e il writer rifiuta un piano invalido con prefisso di contratto.

## Cosa resta aperto (gate 2-6 dell'ADR)

Questo documento chiude il gate 1: contratto versionato e risoluzione unica in
Motion. Restano:

2. il consumer C3D che legge solo questo piano (validazione, policy e calendario
   fuori dal consumer);
3. compositing alpha/depth per-pixel verificato assieme al DoF, CPU oracle e
   Vulkan strict-native;
4. shutter accumulato su GPU senza readback host;
5. acceptance di prodotto: 150 frame 1920×1080, metriche raw/video, CI sullo
   stesso pair di SHA, review visiva;
6. rimozione del legacy solo dopo il gate di equivalenza.

Nessuno di questi si chiude scrivendo documentazione, e nessuno è dichiarato
passato qui.
