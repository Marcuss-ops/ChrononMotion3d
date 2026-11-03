# Round-trip visibile CM3D → Chronon3D sul pair corrente — 2026-11-03

## Provenienza

- Chronon3D `cd593ffc379d98c5da5d7ea7bc091e6ee14ae65a`.
- ChrononMotion3D `a878bcde22741b7337790584aa9320900665326e`.
- Modifiche locali presenti durante il run (non committate, non influenti sui
  pixel misurati): in C3D il rifiuto fail-closed dei piani 3D senza camera
  (`src/render_plan/render_plan_relations_validator.cpp`) con la sua
  regressione; in CM3D la camera esplicita nella fixture di
  `tests/motion_authoring_renderplan.cpp`, che è ciò che rende rigenerabili i
  quattro piani dell'evidenza. Il CLI usato è del 2026-11-02 21:15, quindi
  **precedente** al validator: i render misurano il binario pre-modifica.
- CLI: `Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
  (2026-11-02 21:15), costruito dal sorgente sopra.
- Host: Linux x86_64, software backend. Nessun host GPU dedicato: qui non si
  certifica Vulkan né la CI same-SHA.
- Suite CM3D sul pair: `ctest --test-dir build/release-fast -j4` →
  **67/67 PASS, 37,97 s**.

Le due fixture sono quelle di `ticket-roundtrip-scaled-20261029/` (le stesse
usate dai checkpoint del 29-10 e del 02-11).

## Perché questo run esiste

Il checkpoint 2026-11-02 concludeva che il round-trip visibile «non è
riproducibile» perché il piano con e senza `environment_light` restava
byte-identico e i 31 campioni animati producevano un solo hash. Quelle due
misure erano state prese sulla rotta **stills** (`-o file.png`). Sul pair
corrente la rotta stills è effettivamente vuota per le mesh inline (vedi
"Difetto aperto" sotto), ma la rotta **`--video-sink raw`** — quella usata dai
probe del 29-10 e la stessa che alimenta l'encoder — disegna e anima
correttamente. Questo run misura quella rotta, che è l'autorità visiva.

## glTF animato — PASS

```sh
CLI=Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
R=$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf
$CLI render --plan "$R/animated-gltf.plan.json" --assets-root "$R" \
  --backend software --start-frame 0 --end-frame 30 \
  --video-sink raw --output /tmp/program.rgba -v error
CHRONON3D_VIDEO_PROGRAM_ROUTE=0 $CLI render ... (stessi argomenti) --output /tmp/legacy.rgba
```

- 31 frame RGBA8 640×360; **31/31 hash distinti**.
- Bounds alpha del soggetto, in movimento monotono verso destra e in alto:
  frame 0 `(444,87)-(464,106)`, frame 15 `(471,73)-(492,93)`,
  frame 30 `(498,60)-(518,79)`.
- Pixel opachi per frame: 216–255 (soggetto textured con alpha cutout).
- Hash di frame: f0 `a985d781286ccfd6…`, f15 `6b907d294c6496dd…`,
  f30 `1d2c1d44e847af7d…`.
- Rotta program e rotta legacy producono lo **stesso file**
  (`sha256 c2d3f06d70fbde20d15279470d4c41eabcd647177f8a8bed5c170d42ba07588b`):
  l'animazione non dipende dalla rotta.

## HDRI ambiente — PASS (contributo diffuso visibile)

```sh
R=$PWD/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/hdri
$CLI render --plan "$R/hdri.plan.json" --assets-root "$R" --backend software \
  --start-frame 0 --end-frame 3 --video-sink raw --output /tmp/h-hdri.rgba -v error
$CLI render --plan "$R/without-environment.plan.json" --assets-root "$R" --backend software \
  --start-frame 0 --end-frame 3 --video-sink raw --output /tmp/h-noenv.rgba -v error
```

- Con ambiente: `sha256 b403f65f8301e54b835ee206029091369c4f9b0f33d628c3dfd08ca3410f49de`.
- Senza ambiente: `sha256 0c660f2bd3eff3150dd0040789abe2291613b9af319df870203d4f77a4913a5f`.
- I due file **non** sono uguali. Frame 0: **2.965 pixel opachi** e **2.965
  pixel RGB differenti** tra i due piani, cioè il 100% del soggetto cambia
  quando la probe SH è presente.
- Il soggetto HDRI è statico: 4 frame, 1 solo hash distinto. Atteso
  (nessuna animazione nella fixture), non è un difetto.

## Nota per `TICKET-TEXT-QUALITY-V2` P0-0b — la rotta stills non è una superficie di misura

Con `-o file.png` (stills) sullo stesso pair:

- `hdri.plan.json` e `without-environment.plan.json` → 640×360, **0 pixel
  opachi**, stesso hash `b908999e5076…` (canvas vuoto).
- `animated-gltf.plan.json` → 31 file, **1 solo hash**, 0 pixel opachi.
- Piano minimo di controllo (un triangolo, nessuna texture, camera a z=-5,
  due campioni identici alla base) → anch'esso **0 pixel opachi**.

Il CLI lo dichiara già a ogni render still: «the image output lane (still /
PNG sequence) is not a pixel-truth surface»
(`apps/chronon3d_cli/utils/job/output_lane_pixel_truth.hpp`, scheda
`TICKET-TEXT-QUALITY-V2` P0-0b). Questa run aggiunge un fatto a quella scheda:
sul pair corrente la lane immagine non compone **nemmeno la geometria mesh**
(sul frame 0 della stessa fixture il 2026-10-29 la disegnava, 7.843 pixel). Non
è un problema delle fixture né dell'animazione, e non è un residuo del producer
CM3D: nessun criterio di questa evidenza passa per la lane immagine.

## Limiti

- Evidenza **locale**, non same-SHA: nessun check CI esiste sugli SHA citati e
  nessuna run è stata fatta su host GPU dedicato.
- La parità software/Vulkan e il sampling IBL strict-native restano esterni a
  questo run (vedi `Chronon3d/TICKET-ENVIRONMENT-LIGHT-IBL.md`).
- Nessun media è committato: i `.rgba` restano in `/tmp` e sono rigenerabili
  con i comandi sopra.
