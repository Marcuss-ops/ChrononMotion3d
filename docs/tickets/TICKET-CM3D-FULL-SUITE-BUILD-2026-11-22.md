# TICKET-CM3D-FULL-SUITE-BUILD-2026-11-22 — costruire ed eseguire i 70 test Motion

**Stato:** `OPEN`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** motion CI / build
**Origine:** CTest 2026-11-22 su `build/verify`
**Padre:** [TICKET-CM3D-EVAL-ALLOC-2026-11-22](TICKET-CM3D-EVAL-ALLOC-2026-11-22.md)

## Problema

`ctest --test-dir build/verify` elenca 70 test, ma la maggior parte è
`Not Run` perché gli eseguibili non sono stati costruiti. L'unico test
effettivamente eseguito e fallito è `chrononmotion_boundary`, dovuto ad
artefatti generati (vedi ticket dedicato).

Un `ctest` con 69 “Not Run” non è una certificazione della suite Motion: il
gate “suite motion/evaluation PASS” del ticket padre resta aperto.

## Evidenza

- `[CTEST]` 70 test totali.
- `[CTEST]` `chrononmotion_boundary` Failed per README in
  `out/threepp_certification/`.
- `[CTEST]` Molti test `Not Run`: `chrononmotion_offline_golden`,
  `chrononmotion_procedural_certification`, `chrononmotion_motion_*`,
  `chrononmotion_geospatial`, `chrononmotion_gltf`,
  `chrononmotion_scene_import`, `chrononmotion_interaction`,
  `chrononmotion_retarget`, ecc.
- `[BUILD]` `chrononmotion_smoke` passa 124 check, ma non sostituisce i 70 test.

## Piano di chiusura

1. **Build completa.** Costruire tutti i target di test, non solo la libreria:
   ```bash
   cd ChrononMotion3D
   cmake --build build/verify -j2
   ```
   Con RAM limitata usare `-j2`; non alzare la parallelizzazione senza misura.
2. **Elenco dei mancanti.** Estrarre i test senza eseguibile:
   ```bash
   ctest --test-dir build/verify -N
   ctest --test-dir build/verify --show-only=json-v1 > /tmp/cm3d-tests.json
   ```
   Verificare che ogni `add_test` abbia un target costruito.
3. **Dipendenze opzionali.** Classificare i test che richiedono GPU/asset
   esterni (glTF, geospatial, video) e dichiarare uno skip esplicito con
   motivazione, non un `Not Run` silenzioso.
4. **Esecuzione.** `ctest --test-dir build/verify --output-on-failure -j1`
   (o `-j2` dopo misura RAM), salvando il log.
5. **CI.** Aggiungere il preset/step che costruisce i test e fallisce se
   `ctest` riporta `Not Run` inattesi.
6. **Boundary.** Correggere prima il ticket artifact, altrimenti il rosso
   maschera la suite.

## Gate di accettazione

- [ ] Tutti i 70 eseguibili sono costruiti.
- [ ] `ctest` non riporta `Not Run` non motivati.
- [ ] 70/70 PASS, oppure skip espliciti e documentati per hardware/asset.
- [ ] Il log CTest è allegato al ticket.
- [ ] `chrononmotion_smoke` resta verde.

## Comandi di verifica

```bash
cd ChrononMotion3D
cmake --build build/verify -j2
ctest --test-dir build/verify -N | tail -5
ctest --test-dir build/verify --output-on-failure -j1 2>&1 | tee /tmp/cm3d-ctest.log
```

## Rischi e rollback

- La build completa può essere pesante: usare `-j2`, monitorare `free -h` e
  fermarsi se available scende sotto ~3 GiB.
- Non “chiudere” convertendo i test in skip permanenti: gli skip devono essere
  giustificati da capability mancante e visibili nel log.

## Definition of Done

La suite CTest Motion è completamente costruita ed eseguita, con 70/70 verdi o
skip espliciti, e il boundary test è verde. Solo allora il gate “suite” del
ticket CM3D-AUDIT-01 può essere spuntato.
