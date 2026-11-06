# TICKET-CM3D-DOC-TRUTH — Il ledger e il README descrivevano un tree diverso

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `CHRONONMOTION.md`, `README.md`,
  `docs/PROJECT_STATUS.md`
- **Owner:** ChrononMotion3D docs

## Problema

Tre affermazioni autorevoli erano false o superate:

1. `CHRONONMOTION.md` elencava **“No serialization”**, ma il tree corrente ha
   `MotionScene::toJson`, `SceneExport`, `SceneImport` e il formato
   `chrononmotion.scene` con validazione fail-closed.
2. `README.md` diceva che “physical lens metadata and animated camera DoF fail
   closed”. In realtà `lowerCameraPose` emette focale, sensore, f-stop, focus
   distance, max blur e highlight gain, e `bakeResolvedCameraSamples` lo fa per
   ogni frame: la DoF animata è rappresentata per campione.
3. I conteggi erano incoerenti: “125 file / 6.8%” nel ledger contro “130 file /
   6.9%” nel README, “twelve test executables” contro i 50 target
   `add_executable` del CMakeLists e i 67 test registrati.

`PROJECT_STATUS.md` era già marcato come snapshot, ma la riga “Updated
2026-11-01” lo presentava ancora come documento operativo.

## Criteri di accettazione

- [x] La serializzazione è descritta per ciò che è: scena, non contenuto.
- [x] Il README descrive la DoF statica **e** animata per campione, con il solo
      fail-closed corretto sugli iris non supportati.
- [x] I conteggi sono espressi in modo non deperibile: “~130 retained upstream
      files”, “~17.8k (6.9%)”, “target registrati da CMakeLists”.
- [x] `PROJECT_STATUS.md` dichiara di non essere aggiornabile e rimanda a
      `docs/stato/chronon-motion3d.md`.
- [x] Nessuna nuova affermazione quantitativa senza una data o un riferimento
      verificabile.

## Soluzione applicata

- `CHRONONMOTION.md`: riscritto il punto 5 come “Serialization is scene data
  only”; allineati i conteggi della tabella; rimossi “eleven headers, twelve
  translation units” e “twelve test executables”.
- `README.md`: corretto il paragrafo Status e il paragrafo build dell'adapter;
  il conteggio dei file ora dice “about 130 retained upstream files”.
- `docs/PROJECT_STATUS.md`: aggiunto il banner “Historical snapshot — do not
  update” con il link all'autorità corrente.

## Evidenza

- `CHRONONMOTION.md`, punti 5 e tabella “What was kept”.
- `README.md`, sezione Status e sezione Build.
- `docs/PROJECT_STATUS.md`, righe 1–15.
- `src/chrononmotion/motion/RenderPlanLoweringCamera.cpp:35-80` (prova che la
  DoF per campione è emessa).

## Follow-up 2026-11-06

Tre residui non coperti dalla prima chiusura sono stati corretti nello stesso
giorno:

- `CHRONONMOTION.md` diceva ancora che la migrazione applier→preset tipizzati e
  il target S2 ≤6 header “remain open”; ora dichiara che sono demandati a v2.1
  dal closeout amministrativo del 2026-11-04, coerente con `BOUNDARY_C3D.md`.
- L'indice canonico `docs/stato/README.md` era stale: i nuovi ticket CM3D non
  erano indicizzati. Rigenerato con
  `python3 ops/tools/reconcile_tickets.py --write`; `--check` passa con 73
  schede.
- `TICKET-TEXT-2_5D-CAMERA.md` mancava dal registro §9 di
  `docs/stato/chronon-motion3d.md`; aggiunto con stato OPEN/PARTIAL e il
  rimando all'inventario completo
  `TICKET-CM3D-REMAINING-WORK-20261106.md`.
