# TICKET-CM3D-PER-LAYER-BLUR — Lo shutter è solo di scena

- **Stato:** OPEN / contract-gated (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D authoring ↔ RenderPlan V3 ↔ Chronon3d
- **Owner:** ChrononMotion3D (modello) + Chronon3d (kernel)

## Problema

After Effects ha un motion blur abilitabile per layer, oltre allo shutter di
composizione. ChrononMotion3D ha solo `MotionScene::setShutter`
(`ShutterSettings`, angolo + campioni), mappato 1:1 su un unico
`temporal_motion_blur` di scena. Non esiste un flag per-layer, e il piano non
ha un campo dove esprimerlo.

## Perché non si risolve solo in CM3D

Un flag authoring senza un campo di piano e senza un consumatore che lo
esegua sarebbe una seconda verità: il lowering non potrebbe emetterlo e il
renderer non potrebbe applicarlo. Serve prima un contratto gemello:
`per-layer shutter` nel piano V3, capability dichiarata, kernel C3D e test
incrociati.

## Criteri di accettazione

- [ ] Contratto scritto: campo piano, unità (angolo/gradi), default, fail-closed.
- [ ] `Layer` authora il flag e lo serializza.
- [ ] Il lowering emette il campo solo con capability presente.
- [ ] C3D lo esegue e la parità software/Vulkan è misurata.
- [ ] Test CM3D di emissione e rifiuto dei valori fuori range.

## Stato attuale

Dichiarato in `BOUNDARY_C3D.md` F6. Non implementato: nessun campo inventato,
nessuna duplicazione dello shutter di scena.

## Evidenza

- `include/chrononmotion/motion/MotionScene.hpp`, `ShutterSettings`.
- `include/chrononmotion/motion/RenderPlanLowering.hpp`,
  `temporalMotionBlur`.
- `docs/BOUNDARY_C3D.md`, F6.
