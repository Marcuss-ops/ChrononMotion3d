# TICKET-CM3D-BUILD-WERROR — `plan_optical_remake` non compila sotto `-Werror`

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D, build `release-fast` con adapter
- **Owner:** ChrononMotion3D build/tooling

## Problema

La build incrementale `cmake --build build/release-fast -j8` si fermava al
target `chrononmotion_plan_optical_remake` con tre errori trattati come errori
perché la configurazione usa `-Wall -Wextra -Werror`:

```
tools/plan_optical_remake.cpp:271:19: error: unused parameter ‘t’ [-Werror=unused-parameter]
tools/plan_optical_remake.cpp:40:8: error: ‘{anonymous}::lerp(...)’ defined but not used [-Werror=unused-function]
tools/plan_optical_remake.cpp:36:7: error: ‘{anonymous}::easeOut(float)’ defined but not used [-Werror=unused-function]
```

Il target non è coperto dai test CTest, ma fa parte del build completo: finché
non compila, la suite non può essere eseguita e la verifica del repository
resta incompleta.

## Causa

Codice editoriale rimasto dopo una rifattorizzazione: i due helper `easeOut` e
`lerp` non avevano più chiamanti (erano presenti solo le definizioni), e la
lambda del layer `glow_amber` conservava un parametro temporale che non usava
più. Non è un problema di contorno: è un target del tree che il gate `release`
deve compilare.

## Criteri di accettazione

- [x] `chrononmotion_plan_optical_remake` compila con `-Werror`.
- [x] Gli helper non usati sono rimossi, non silenziati con pragma o
      `[[maybe_unused]]`.
- [x] La lambda non conserva un parametro inutilizzato con nome.
- [x] La build completa `build/release-fast` termina senza errori; clean
      build `[737/737]`, exit 0.
- [x] `ctest --test-dir build/release-fast` passa 67/67 dopo la chiusura del
      drift golden T02.

## Soluzione applicata

In `tools/plan_optical_remake.cpp`:

1. rimossi `easeOut(float)` e `lerp(const Sample&, const Sample&, float)`,
   entrambi senza chiamanti;
2. la lambda del layer 5 (`glow_amber`) è passata da `[&](float t)` a
   `[&](float)`, perché il tempo non è usato in quel layer statico.

Nessuna modifica al comportamento del tool: i due helper erano morti e la
lambda restituisce lo stesso `Sample` costante.

## Evidenza

Comando:

```
free -h
cmake --build build/release-fast -j8
ctest --test-dir build/release-fast -j4 --output-on-failure
```

- RAM verificata prima della clean build: 12 GiB available, nessun OOM.
- Build: `chrononmotion_plan_optical_remake` linkato a `[221/737]`; clean
  build `--clean-first -j6` terminata con exit 0 a `[737/737]`.
- Prima esecuzione CTest: 65/67, con i soli due test showcase falliti per il
  golden stale (vedi `TICKET-CM3D-SHOWCASE-GOLDEN-20261106.md`, non per questo
  target).
- Dopo la rigenerazione del golden: **100% tests passed, 0 tests failed out
  of 67** (43.90 s) sulla clean build.
