# TICKET-CM3D-DENSE-TRS — Il lowering V3 senza flag emetteva ancora dense integer TRS

- **Stato:** CLOSED (2026-11-06) per la lane V3; la lane V2 resta legacy per contratto
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `RenderPlanLowering`,
  `RenderPlanLoweringSamples`, producer V3, parità easing
- **Owner:** ChrononMotion3D motion lowering

## Problema

`lowerToRenderPlan` materializzava i `transform_samples` V3 solo quando il
caller impostava `RenderPlanLoweringOptions::requireResolvedLayerSamples`.
Senza il flag, un piano V3 ricadeva nel bake dense integer TRS: una
`AnimationTrack` per posizione/scala/rotazione/opacità con una chiave per ogni
frame intero. Il risultato era formalmente V3 ma con due autorità temporali
possibili e con l'easing authored ridotto a keyframe interi.

Il flag era stato aggiunto ai sei producer materializzati, ma il contratto V3
non lo rendeva obbligatorio: un nuovo producer, un test o un chiamante diretto
poteva ancora ottenere il path legacy. La demolition debt restava quindi
aperta anche se tutti i producer censiti erano verdi.

## Criteri di accettazione

- [x] Un piano V3 emette sempre `transform_samples`, anche senza
      `requireResolvedLayerSamples`.
- [x] Il bake dense integer TRS resta solo per V2, che non trasporta
      `transform_samples`.
- [x] Il flag resta disponibile per compatibilità sorgente e per i test di
      migrazione, ma non è più necessario per V3.
- [x] Un mesh statico V3 resta ammesso senza campioni risolti; un mesh con
      transform/opacità animate fallisce chiuso invece di perdere l'animazione.
- [x] La parità easing V3 legge i campioni risolti e verifica che non esista
      più `layer.animation`.
- [x] Il test di determinismo confronta anche i `transform_samples`.

## Soluzione applicata

- `RenderPlanLowering.cpp`: la condizione che materializza i frame di
  presentazione è ora
  `requireResolvedLayerSamples || plan.schema == kRenderPlanSchemaV3`.
  Il calendario shutter, quando presente, mantiene la precedenza.
- `RenderPlanLoweringSamples.cpp`: il blocco dense integer TRS viene eseguito
  solo con `options.schema == kRenderPlanSchemaV2`. V3 non crea più
  `AnimationTrack` di transform.
- `RenderPlanLowering.cpp`: il post-pass V3 salta i mesh statici, ma confronta
  le pose risolte e rifiuta un mesh con transform/opacità non costanti.
  Light e Mesh non ricevono mai `transform_samples`.
- `tests/motion_authoring_renderplan.cpp`: aggiunto il check V3 senza flag
  (31 campioni, nessuna animation), esteso `sameLoweredPlan` ai campioni
  risolti, e ripuliti i piani costruiti manualmente da un piano V3 abbassato
  quando cambiano tipo in Light/Mesh.
- `tests/motion_renderplan_easing_parity.cpp`: la parità dei 12 easing legge
  `transform_samples` invece di `animation`.

## Confine V2

V2 non ha il campo `transform_samples`. Eliminare anche il bake V2
richiederebbe un cambio di contratto del piano o la rimozione dello schema V2;
non è una modifica trasparente e non è stata fatta. La lane V2 resta quindi un
path legacy esplicito, non un fallback silenzioso di V3:

- V3: sempre risolto, nessuna animation di transform;
- V2: dense integer TRS, come da schema storico.

La demolition debt residua è l'equivalenza del corpus consumer
compatibility/typed e l'eventuale decisione di ritirare V2; non è più
l'emissione V3.

## Evidenza

- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLowering.cpp`,
  condizione `requireResolvedLayerSamples || plan.schema == kRenderPlanSchemaV3`
  e post-pass mesh.
- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLoweringSamples.cpp`,
  blocco dense limitato a `kRenderPlanSchemaV2`.
- `ChrononMotion3D/include/chrononmotion/motion/RenderPlanLowering.hpp`,
  commento del flag aggiornato.
- `ChrononMotion3D/tests/motion_authoring_renderplan.cpp`:
  V3 senza flag, determinismo dei campioni, mesh animato fail-closed.
- `ChrononMotion3D/tests/motion_renderplan_easing_parity.cpp`:
  **1152 check, 0 failure**.
- `chrononmotion_authoring`: **444 check, 0 failure**.
- `ctest --test-dir build/release-fast --output-on-failure -j4`:
  **67/67 PASS in 36,10 s**.
