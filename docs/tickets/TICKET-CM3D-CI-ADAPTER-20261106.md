# TICKET-CM3D-CI-ADAPTER — La CI non riproduceva la suite completa con adapter

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `.github/workflows/ci.yml`
- **Owner:** ChrononMotion3D CI

## Problema

La corsia `interop-adapter` compilava con `CHRONON3D_INCLUDE_DIR` e poi
eseguiva solo:

```
ctest --preset dev -R 'authoring|parity|canary|renderplan|bridge|geospatial|rig_pipeline'
```

Quindi i 67/67 documentati non erano riprodotti dalla CI: i test fuori dal
filtro potevano regredire senza che la corsia adapter lo vedesse. Inoltre il
commento della corsia principale citava ancora `include/threepp/motion/`, un
namespace che non esiste più.

## Criteri di accettazione

- [x] Il commento CI nomina `include/chrononmotion/motion/`.
- [x] La corsia adapter esegue la suite completa, non solo il sottoinsieme.
- [x] Il sottoinsieme resta come primo segnale rapido, senza sostituire la
      suite completa.
- [x] La modifica non richiede nuovi segreti o servizi.

## Soluzione applicata

Aggiunto il passo:

```yaml
- name: Test (full suite with adapter)
  if: steps.sibling.outputs.present == 'true'
  run: ctest --preset dev --output-on-failure
```

dopo il passo filtrato, e corretto il namespace nel commento.

## Evidenza

- `.github/workflows/ci.yml`, job `interop-adapter`.
- La build locale con adapter registra 67 test; la suite completa è quella che
  il nuovo passo esegue quando il checkout sibling è disponibile.
- Lane senza adapter verificata localmente il 2026-11-06 con
  `build/release-noadapter`: build `chrononmotion-showcase` 117/117, test
  showcase 2/2 PASS (case 59 `Disabled`), `--all` → 62/62 PASS, 1/63 SKIP,
  0 FAIL, 53.970 asserzioni. Il manifest a 63 entry è quindi compatibile con
  entrambe le configurazioni.
