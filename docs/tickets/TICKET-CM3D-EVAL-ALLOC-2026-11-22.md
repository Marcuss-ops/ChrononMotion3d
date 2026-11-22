# TICKET-CM3D-EVAL-ALLOC-2026-11-22 — zero-alloc steady state in MotionScene evaluation

**Stato:** `IN PROGRESS` (2026-11-22)
**Owner:** motion evaluation
**Origine:** audit Chronon 2026-11-22, voce CM3D-AUDIT-01
**File:** `src/chrononmotion/motion/MotionScene_evaluation.cpp`

## Problema

`MotionScene::evaluate()` chiama `evaluatePose()` due volte per frame
(`frame - 1` e `frame`). Dentro `evaluatePose()`:

- due `std::function` (`spaceAt`, `worldAt`) vengono costruite a ogni pass;
- ogni layer costruisce un `std::vector<Layer::Id> visitingSpaces`;
- la ricorsione `spaceAt`/`worldAt` cresce quei vettori a ogni livello.

Il risultato è heap churn per frame e per layer sul percorso caldo, senza
alcun bisogno: la gerarchia è già validata e l'ordine è cachato.

## Fix implementato (quick win)

- sostituite le due `std::function` con un resolver locale (`SpaceResolver`)
  che chiama direttamente i metodi di `MotionScene` e `Layer::sample`;
- `visitingSpaces` è ora uno scratch unico, dichiarato fuori dal loop dei
  layer, con `clear()` e `reserve(order_.size())` per riusare la capacità;
- nessun cambio di semantica: stesse eccezioni, stesso ordine, stessi valori.

## Gate

- [x] `[OK 2026-11-22]` Syntax check del translation unit:
  `g++ -std=c++20 -fsyntax-only` con `ulimit -v 4000000` → status 0.
- [x] `[OK 2026-11-22]` Build/link `chrononmotion` in
  `ChrononMotion3D/build/verify` (Ninja, `-j2`) sul commit del fix.
- [x] `[OK 2026-11-22]` `chrononmotion_smoke` PASS (`124 checks, 0 failures`).
- [ ] Suite motion/evaluation esistente PASS (parenting, space switch,
  constraint, cycle detection). CTest `build/verify`: 70 test, 1 FAIL
  (`chrononmotion_boundary`, dovuto a README generati in
  `out/threepp_certification/` con riferimenti ffmpeg/NVENC) e molti
  `Not Run` perché gli eseguibili non sono stati costruiti.
- [ ] Confronto output JSON/pose pre/post invariato su una scena con space
  switch e constraint.
- [ ] Misura allocator (massif o contatori) su 30 frame: zero allocazioni
  steady-state per i due resolver e per `visitingSpaces`.
  `[NOT RUN]` — `valgrind`/`heaptrack` non sono installati; `perf` da solo non
  fornisce un conteggio allocazioni per sito.
- [ ] Nota esplicita: build, suite e misura allocazioni sono `[NOT RUN]`
  finché la RAM non torna disponibile; il fix non è `DONE` senza i gate sopra.

## Come chiudere la misura allocator

1. Installare un profiler (`valgrind` o `heaptrack`) oppure aggiungere un
   contatore di allocazioni nel test:
   ```bash
   sudo apt-get install -y valgrind heaptrack
   ```
2. Aggiungere un probe dedicato, per esempio
   `tests/alloc/evaluation_alloc_probe.cpp`, che costruisca una scena con
   space switch + constraint e chiami `evaluate()` per 30 frame.
3. Eseguire il probe con `valgrind --tool=massif` o `heaptrack` e confrontare
   lo snapshot dopo il warmup con il frame 30: nessuna crescita per frame.
4. In alternativa, usare un allocator hook nel probe e asserire zero
   allocazioni dopo il warmup per i due resolver e `visitingSpaces`.
5. Registrare il comando e l'output nel ticket; un semplice `ulimit -v` non
   basta a dimostrare zero allocazioni.

Ticket figli aperti: [boundary artifacts](TICKET-CM3D-BOUNDARY-ARTIFACTS-2026-11-22.md)
e [full suite](TICKET-CM3D-FULL-SUITE-BUILD-2026-11-22.md).

## Non-goals

- Riscrivere il solver o cambiare l'ordine di valutazione.
- Toccare `evaluateLights` o il contratto `CompiledScene`.
