# TICKET-CM3D-SHADOW-EMISSION — `shadow_blur`/`shadow_opacity` per-layer emission

- **Stato:** CLOSED (2026-11-11)
- **Data:** 2026-11-06 (aggiornato e chiuso 2026-11-11)
- **Ambito:** ChrononMotion3D lowering ↔ Chronon3d shadow kernel
- **Owner:** Chronon3d (kernel) + ChrononMotion3D (emission)

## Problema

`Layer` authorava e serializzava `acceptsShadow`, `shadowOpacity`, `shadowBlur`
(`Layer.hpp:146-148`), ma il lowering V3 verso RenderPlan non emetteva
`shadow_blur` e `shadow_opacity`, impedendo al renderer di ricevere i valori
per-layer configurati dall'autore.

## Causa

Mancava il plumbing di abbassamento nel producer CM3D verso il wire V3 e la
validazione fail-closed su schema V2 e su valori out-of-range. C3D possiede già
il kernel e la propagazione (`ShadowNode`, `graph_builder_lighting_passes.cpp:111-122`
con clamp a [0, 192] per blur e [0, 1] per opacity).

## Criteri di accettazione

- [x] C3D espone i campi `shadow_blur` e `shadow_opacity` per-layer nel wire V3 e li consuma in `ShadowNode`.
- [x] Il lowering CM3D emette i campi per V3 preservando i default e le configurazioni authored.
- [x] Validazione fail-closed: rigetto di valori non finiti o fuori range ([0, 192] blur, [0, 1] opacity) e rigetto su schema V2.
- [x] Test CM3D di lowering, serialization, round-trip e fail-closed sui valori fuori range.
- [x] `BOUNDARY_C3D.md` F4 aggiornato a CHIUSO con evidenza.

## Implementazione ed Evidenza

1. **Validazione Scena CM3D:** `src/chrononmotion/motion/MotionScene_validation.cpp` convalida che `shadowBlur` sia in `[0, 192]` e `shadowOpacity` sia in `[0, 1]`.
2. **Lowering V3:** `src/chrononmotion/motion/RenderPlanLoweringSamples.cpp` popola `casts_shadows`, `accepts_shadows`, `shadow_blur` e `shadow_opacity` per i layer V3.
3. **Validazione Emissione RenderPlan:** `src/chrononmotion/motion/RenderPlanEmissionValidateLayer.cpp` valida `shadow_blur` e `shadow_opacity` (finitezza e range), rifiutandoli se emessi in schema V2.
4. **Emissione JSON:** `src/chrononmotion/motion/RenderPlanEmission.cpp` emette `casts_shadows`, `accepts_shadows`, `shadow_blur` e `shadow_opacity` nel JSON wire format V3.
5. **Test di Regressione e Conformità:**
   - `tests/motion_authoring_renderplan.cpp`: test di lowering e fail-closed su blur/opacity non validi.
   - `tests/chronon3d_v3_bridge_regression.cpp`: verifica di conformità schema wire e rigetto fail-closed.
   - Suite `chrononmotion_authoring`: 454 checks, 0 failures PASS.
