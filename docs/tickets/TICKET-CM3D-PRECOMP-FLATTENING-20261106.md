# TICKET-CM3D-PRECOMP-FLATTENING — Le precomp e le luci annidate non arrivavano al RenderPlan

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D — `RenderPlanLowering`, composizioni annidate,
  lowering dei campioni risolti
- **Owner:** ChrononMotion3D motion lowering

## Problema

`MotionScene::compile()` valutava correttamente le precomp annidate e
produceva `CompiledPrecomp` con world/opacity/visible e la scena locale, ma
`lowerToRenderPlan` costruiva il piano solo dai layer top-level. Un layer
precomp è un controller nullo: `RenderPlanLoweringSamples` lo saltava, quindi
il contenuto annidato non raggiungeva mai il RenderPlan. Il consumer vedeva
un piano formalmente valido ma senza i layer della composizione.

Il caso non era coperto da un test end-to-end: la fondazione verificava solo
la compilazione della scena annidata. Le luci di una precomp erano rifiutate
con `std::invalid_argument` invece di essere flattenate: il piano era corretto
per i layer visivi ma perdeva l'illuminazione della composizione.

## Criteri di accettazione

- [x] Ogni layer renderizzabile di una precomp compare nel piano con identità
      univoca e deterministica.
- [x] Transform, opacità e visibilità del parent vengono composti lungo tutta
      la catena, inclusi i controller intermedi.
- [x] Il time remap della precomp e il time remap locale del layer vengono
      applicati sul timeline corretto.
- [x] Due istanze della stessa precomp producono layer distinti con mapping
      dei contenuti riutilizzabile.
- [x] I riferimenti interni a track matte e maschere testuali vengono
      rimappati nello spazio ID del piano.
- [x] Le luci di una precomp vengono flattenate con ID path-qualified,
      composizione del parent e attenuazione per opacità/visibilità; le luci
      ambient sono saltate con warning perché non hanno un layer consumer.
- [x] I casi non rappresentabili falliscono chiusi, senza drop silenziosi:
      content+precomp sullo stesso layer, shutter annidato, camera annidata,
      cicli, profondità eccessiva, limiti layer, time-dependent content sotto
      un time remap della precomp.
- [x] I campioni risolti V3 seguono lo stesso percorso di composizione e non
      richiedono una seconda valutazione della scena top-level.

## Soluzione applicata

- `flattenScene` costruisce un albero di istanze precomp e una lista piatta di
  `FlattenedLayer`. I layer top-level mantengono l'ID autore; i figli di una
  precomp usano un ID path-qualified, ad esempio `50/101` e `50/60/102`.
- La risoluzione per frame valuta la scena top-level una volta, poi ogni scena
  annidata al frame locale ottenuto da `Layer::mapFrame`, e compone
  `parentWorld * childWorld`, `parentOpacity * childOpacity`,
  `parentVisible && childVisible`.
- I campioni includono la matrice world finale oltre alla posa TRS: il
  post-pass V3 può emettere `transform_samples` con la convenzione di
  rotazione del consumer senza ricalcolare la gerarchia.
- `contentLayers` continua a funzionare con l'ID autore per i layer top-level
  e come fallback per i figli di precomp. Se un ID figlio collide con un ID
  top-level, il fallback viene disabilitato e serve la chiave path-qualified.
- `track_matte.source`, `path_mask.text_source` e `masks[].text_source`
  vengono rimappati quando il riferimento punta a un sibling della stessa
  precomp.
- Il controllo dei campioni e il budget layer usano il numero di layer
  flattenati, non il solo conteggio top-level.
- `flattenScene` raccoglie anche `FlattenedLightInfo` per ogni istanza. Il
  `LightState` viene valutato nella scena locale, poi `composeLightState`
  applica il world del parent a posizione/direzione, moltiplica l'intensità
  per l'opacità composta e propaga la visibilità. L'ID emesso è
  `motion_light_<path>_<id>`; ogni istanza della stessa precomp ha quindi una
  luce distinta. Le luci ambient non hanno un `LightLayerPlan` equivalente e
  vengono saltate con un warning esplicito.

## Confini fail-closed

Il flattening visivo resta separato dalla raccolta luci: non viene inventato
un kernel o un contratto consumer per:

- shutter autore in una precomp annidata;
- controller camera dentro una precomp;
- video o particelle sotto un time remap della precomp, perché la loro
  evoluzione temporale non è esprimibile con la sola trasformazione bakeata;
- time remap concatenati su un layer che ha già un proprio time remap;
- riferimenti a layer esterni alla precomp attraverso un ID nudo ambiguo.

Questi casi lanciano `std::invalid_argument` con un diagnóstico esplicito.
Nessun layer viene eliminato in silenzio.

Il flattening compone il timing autore CM3D (transform, opacity, lifetime,
time remap della precomp). I timeline interni a un `LayerPlan` fornito dal
consumer non vengono rimappati: il contratto li tratta come timeline di
output. Un piano che debba essere retimato con la precomp va quindi fornito
già bakeato; non viene inventata una seconda semantica temporale.

## Evidenza

- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLowering.cpp`:
  `flattenScene`, `FlattenedLightInfo`, `composeLightState`,
  `resolveInstance`, composizione dei campioni risolti.
- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLoweringSamples.cpp`:
  identità flattened, fallback dei content plan, rimappatura dei riferimenti.
- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLoweringInternal.hpp`:
  `FlattenedLayer`, `findContentLayer`, world nei campioni risolti.
- `ChrononMotion3D/tests/motion_authoring_renderplan.cpp`:
  `testPrecompRenderPlanFlattening`, con due istanze, time remap, lifetime,
  due luci point annidate con ID path-qualified, attenuazione per opacità,
  warning ambient, V3 resolved samples, collisione ID, track matte e casi
  negativi.
- `cmake --build build/release-fast -j6`: build riuscita.
- `chrononmotion_authoring`: **444 check, 0 failure**.
- `ctest --test-dir build/release-fast --output-on-failure -j4`:
  **67/67 PASS in 36,10 s**.

## Aggiornamento luci annidate (2026-11-06)

La precedente chiusura trattava le luci di una precomp come non
rappresentabili. Il lowering ora le tratta come contenuto della composizione:

- la luce viene valutata nella scena locale, quindi il parent precomp applica
  il proprio world a posizione e direzione;
- l'intensità viene moltiplicata per l'opacità composta e azzerata quando la
  catena è invisibile;
- il layer conserva l'animazione di posizione/intensità/colore/direzione sul
  timeline esterno, con il time remap locale della precomp;
- l'ID è path-qualified (`motion_light_50_120`), quindi due istanze non
  collidono;
- le luci ambient sono saltate con un warning, non con un errore che blocca
  l'intera composizione.

Il test copre una point light dentro due istanze della stessa precomp, con
rotazione/posizione del parent, time remap, opacità `0.5` e un'ambient che
produce il warning senza diventare layer.
