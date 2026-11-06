# TICKET-CM3D-RESOLVED-SAMPLES-AUDIT — `plan_particles_showcase` aggirava i campioni risolti

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `tools/plan_particles_showcase.cpp`,
  `include/chrononmotion/motion/RenderPlanLowering.hpp`
- **Owner:** ChrononMotion3D motion lowering

## Problema

La politica del boundary è che ogni piano V3 materializzato richieda
`RenderPlanLoweringOptions::requireResolvedLayerSamples = true`, così i layer
viaggiano come `transform_samples` risolti e non come TRS a keyframe interi.
Cinque tool su sei lo facevano; `plan_particles_showcase` no. Il tool produce
tre piani V3 con layer particellari e li scrive senza il flag, quindi poteva
emettere il formato legacy proprio nel caso in cui il contratto chiede il
risolto.

Inoltre il commento del campo nell'header diceva “Materialized V3 producers
set this”, ma non distingueva il default `false` (necessario per i piani V2)
dal requisito dei producer.

## Criteri di accettazione

- [x] Tutti i tool che chiamano `lowerToRenderPlan` impostano il flag a `true`.
- [x] `plan_particles_showcase` compila e produce piani V3 con
      `transform_samples`.
- [x] Il default `false` resta disponibile per V2 e per i test di migrazione,
      documentato come tale.
- [x] La demolition debt residua è dichiarata consumer-only (equivalenza del
      corpus compatibility/typed), non “CM3D aperto”.

## Soluzione applicata

- `tools/plan_particles_showcase.cpp`: aggiunto
  `options.requireResolvedLayerSamples = true;` accanto allo schema V3.
- `include/chrononmotion/motion/RenderPlanLowering.hpp`: il commento ora dice
  che tutti i producer materializzati del tree impostano il flag e che il
  default `false` esiste solo per piani V2 e test di migrazione.
- `docs/BOUNDARY_C3D.md`: audit 2026-11-06 con l'elenco dei sei tool e stato
  della demolition debt.

## Aggiornamento 2026-11-06

Il flag non è più necessario per V3: `lowerToRenderPlan` materializza i
campioni risolti per schema, e il bake dense integer TRS resta solo nella lane
V2. Il flag resta per compatibilità sorgente e per i test di migrazione. La
rimozione V3 è tracciata in
`TICKET-CM3D-DENSE-TRS-20261106.md`; l'equivalenza del corpus consumer e
l'eventuale ritiro di V2 restano fuori da questa scheda.

## Evidenza

- `tools/plan_particles_showcase.cpp`, funzione `writePlan`.
- `include/chrononmotion/motion/RenderPlanLowering.hpp`,
  `requireResolvedLayerSamples`.
- `docs/BOUNDARY_C3D.md`, paragrafo “Per ogni piano materializzato V3”.
- Audit: `for f in tools/plan_*.cpp` non trova più producer che chiamano il
  lowering senza il flag.
