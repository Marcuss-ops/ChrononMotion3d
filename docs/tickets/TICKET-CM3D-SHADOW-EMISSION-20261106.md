# TICKET-CM3D-SHADOW-EMISSION — `shadow_blur`/`shadow_opacity` non arrivano al piano

- **Stato:** BLOCKED (2026-11-06) — dipende dal kernel shadow per-layer C3D
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D lowering ↔ Chronon3d shadow kernel
- **Owner:** Chronon3d (kernel) + ChrononMotion3D (emission)

## Problema

`Layer` authora e serializza `acceptsShadow`, `shadowOpacity`, `shadowBlur`
(`Layer.hpp:146-148`), ma il lowering emette solo
`castsShadow`/`shadowRadius`/`shadowBias`. Un piano con blur/opacità d'ombra
authorati non trasporta quei valori: il renderer non può applicarli.

## Causa

Mancano i kernel per-layer nel consumer. Aggiungere i campi al wire ora
significherebbe dichiarare una capability che nessun backend esegue, contro la
regola “default che non arma feature non-kernelizzate”.

## Criteri di accettazione

- [ ] C3D espone i kernel `shadow_blur` e `shadow_opacity` per-layer.
- [ ] Il lowering emette i due campi solo quando la capability è dichiarata.
- [ ] Test CM3D di round-trip e fail-closed sui valori fuori range.
- [ ] Test C3D pixel/parità sui due campi.
- [ ] `BOUNDARY_C3D.md` F4 aggiornato a CHIUSO con evidenza.

## Stato attuale

Lato CM3D: validazione, serializzazione e round-trip dei campi sono verdi;
l'emission resta `BLOCKED` sul kernel C3D. Non è un item reachable in questa
milestone e non va duplicato in `render/`.

## Evidenza

- `include/chrononmotion/motion/Layer.hpp`, campi shadow.
- `src/chrononmotion/motion/RenderPlanLowering.cpp:224-226` (solo i tre campi
  emessi).
- `docs/BOUNDARY_C3D.md`, F4.
