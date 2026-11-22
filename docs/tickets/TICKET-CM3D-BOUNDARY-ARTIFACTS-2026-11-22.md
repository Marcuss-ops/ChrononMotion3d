# TICKET-CM3D-BOUNDARY-ARTIFACTS-2026-11-22 — il boundary test scansiona output generati

**Stato:** `OPEN`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** motion test infrastructure
**Origine:** CTest 2026-11-22, `chrononmotion_boundary`
**Padre:** [TICKET-CM3D-EVAL-ALLOC-2026-11-22](TICKET-CM3D-EVAL-ALLOC-2026-11-22.md)
**Correlato:** [TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22](../../../ChrononTemplate/docs/tickets/TICKET-REPO-ARTIFACTS-HYGIENE-2026-11-22.md)

## Problema

`chrononmotion_boundary` fallisce con 12 riferimenti a ffmpeg/NVENC trovati in
README generati sotto `out/threepp_certification/`. Il checker sta scansionando
artefatti di output come se fossero sorgenti Motion.

Il codice Motion non è stato toccato da quei README; il test rosso è rumore di
artefatti, ma resta un gate rosso e va chiuso.

## Evidenza

- `[CODICE]` `tests/check_motion_boundary.cmake:189`:
  ```text
  check_motion_boundary: 12 content-machinery reference(s) in a motion-side tree.
  ```
- File coinvolti:
  - `out/threepp_certification/T06-gpu-verify/README.txt` (2 `nvenc`)
  - `out/threepp_certification/T12-gpu-verify/README.txt` (ffmpeg + nvenc)
  - `out/threepp_certification/T14-gpu-verify/README.txt` (ffmpeg + nvenc)
  - `out/threepp_certification/T16-gpu-verify/README.txt` (ffmpeg + nvenc)
- I README sono output di certificazione, non sorgenti della libreria.

## Piano di chiusura

1. **Separare output e sorgenti.** Spostare `out/` fuori dal tree scansionato
   (per esempio sotto `build/`) o aggiungerlo a `.gitignore` se è generato.
2. **Scanner sui sorgenti tracciati.** In `check_motion_boundary.cmake`, usare
   `git ls-files` o una lista esplicita di directory (`include/`, `src/`,
   `tests/`, `tools/`) invece di `file(GLOB_RECURSE)` sull'intero repo.
3. **Mantenere il guardrail.** Il checker deve continuare a trovare token
   vietati nei sorgenti reali e a rispettare l'eccezione
   `boundary-guardrail: allowed`.
4. **Test di regressione.** Aggiungere un file temporaneo non tracciato con un
   token vietato e verificare che **non** faccia fallire il boundary; poi
   togliere il file.
5. **Pulizia.** Rimuovere o spostare solo gli artefatti non tracciati, dopo
   `git ls-files` (vedi ticket hygiene).

## Gate di accettazione

- [ ] `ctest --test-dir build/verify -R '^chrononmotion_boundary$'` PASS.
- [ ] Il checker continua a fallire su un sorgente reale con token vietato.
- [ ] `out/` non è più scansionato e non è tracciato.
- [ ] Nessun file sorgente rimosso o escluso per far passare il test.

## Comandi di verifica

```bash
cd ChrononMotion3D
ctest --test-dir build/verify -R '^chrononmotion_boundary$' --output-on-failure

# Controllo che gli output non siano tracciati:
git ls-files out/ | head
git status --short out/ | head
```

## Rischi e rollback

- Escludere `out/` non deve nascondere violazioni in `src/`/`tests/`/`tools/`.
- Se `out/` contiene fixture necessarie, spostarle in `tests/fixtures/` e
  aggiornare i riferimenti, non cancellarle.

## Definition of Done

Il boundary test è verde, scansiona solo sorgenti tracciati e continua a
rilevare una violazione introdotta artificialmente in un sorgente.
