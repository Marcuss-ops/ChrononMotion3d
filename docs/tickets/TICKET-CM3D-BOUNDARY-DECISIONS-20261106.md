# TICKET-CM3D-BOUNDARY-DECISIONS — S2/S5, F2 e roving key non avevano una decisione

- **Stato:** CLOSED (2026-11-06) per la parte decisionale; S2/S5 restano
  `DEFERRED v2.1`
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `docs/BOUNDARY_C3D.md`,
  `docs/stato/chronon-motion3d.md`
- **Owner:** ChrononMotion3D motion boundary

## Problema

Il boundary mescolava tre stati diversi e li presentava tutti come “aperti”:

1. **S2 (13→≤6 header camera) e S5 (lowering-vs-lowering)** erano ancora
   descritti come lavoro corrente, mentre la scheda
   `TICKET-CAMERA-SURFACE-CONSOLIDATION` è stata chiusa amministrativamente il
   2026-11-04 e li ha demandati a v2.1.
2. **F2 (tre vocabolari di easing)** diceva “il mapping va definito”, senza
   decidere se il lowering dovesse davvero trasportare easing per-key.
3. **Roving key** non aveva né implementazione né una dichiarazione di scope:
   l'emission rifiuta `roving=true`, ma il documento non lo diceva.

Senza una decisione esplicita, ogni sessione successiva poteva riaprire gli
stessi tre punti.

## Criteri di accettazione

- [x] S2/S5 risultano `DEFERRED v2.1`, non “open”, con il link alla scheda di
      chiusura amministrativa.
- [x] F2 è chiuso come decisione: il lowering bakea lineare; il piano non
      trasporta easing per-key; la parità è pinnata dal test sui 11 tipi.
- [x] Roving key è dichiarato fuori scope V1 e fail-closed, non un TODO.
- [x] Le decisioni sono registrate anche in `docs/stato/chronon-motion3d.md`
      §8, che è l'autorità di stato.

## Soluzione applicata

- `docs/BOUNDARY_C3D.md`:
  - F2 riscritto come “CHIUSO come decisione (2026-11-06)” con il riferimento a
    `tests/motion_renderplan_easing_parity.cpp`.
  - S2 aggiornato: 13→≤6 e S5 demandati a v2.1.
  - Aggiunti F5 (roving key) e F6 (per-layer motion blur).
  - F4 (shadow) marcato BLOCKED sul kernel C3D, non un item reachable.
- `docs/stato/chronon-motion3d.md` §8: registrate le stesse decisioni.

## Evidenza

- `docs/BOUNDARY_C3D.md`, sezioni F2, F4, F5, F6 e aggiornamento S2.
- `docs/stato/chronon-motion3d.md` §8.
- `Chronon3d/docs/tickets/archive/TICKET-CAMERA-SURFACE-CONSOLIDATION.md:3`
  (S2/S5 demandati a v2.1).
- `src/chrononmotion/motion/RenderPlanEmissionValidateTracks.cpp:60`
  (`roving` rifiutato).
