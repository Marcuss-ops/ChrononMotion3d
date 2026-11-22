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
- [ ] Build/link `chrononmotion` e adapter Chronon3d sul commit del fix.
- [ ] Suite motion/evaluation esistente PASS (parenting, space switch,
  constraint, cycle detection).
- [ ] Confronto output JSON/pose pre/post invariato su una scena con space
  switch e constraint.
- [ ] Misura allocator (massif o contatori) su 30 frame: zero allocazioni
  steady-state per i due resolver e per `visitingSpaces`.
- [ ] Nota esplicita: build, suite e misura allocazioni sono `[NOT RUN]`
  finché la RAM non torna disponibile; il fix non è `DONE` senza i gate sopra.

## Non-goals

- Riscrivere il solver o cambiare l'ordine di valutazione.
- Toccare `evaluateLights` o il contratto `CompiledScene`.
