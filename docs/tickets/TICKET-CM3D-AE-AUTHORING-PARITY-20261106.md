# TICKET-CM3D-AE-AUTHORING-PARITY — Parità di authoring AE nel perimetro CM3D

- **Stato:** COMPLETE (perimetro CM3D) — 2026-11-06
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D; estensione C3D per il falloff point
- **Owner:** ChrononMotion3D
- **Residuo aperto:** shadow map/PCF C3D (`GAP`), tracciato separatamente

## Problema

L'audit dei sei contratti After Effects (`docs/evidence/ae3d-parity-20261106/`)
ha separato i problemi di authoring CM3D dai kernel del renderer C3D:

1. il look-at a due nodi usava un up fisso e, attraversando il polo verticale,
   cambiava il roll di 180° e mancava il target di 0,1157 px;
2. l'angolo del cono AE è l'apertura totale, mentre il campo del piano è il
   semiangolo: senza una conversione l'utente doveva dimezzarlo a mano;
3. lo zoom AE e il FOV verticale erano già la stessa lente, ma non avevano una
   regressione CM3D dedicata;
4. il falloff point C3D non aveva una modalità AE Inverse Square Clamped;
5. la shadow map/PCF non esiste nel backend Vulkan di produzione.

I punti 1–4 sono chiusi. Il punto 5 richiede un sottosistema di rendering
separato e non viene dichiarato PASS.

## Lavoro completato

### 1. Look-at condizionato al polo

`CameraRig::lookAtPose` costruisce il frame dalla direzione di vista e usa un
fallback deterministico vicino alla verticale:

- l'asse destro “world-up × backward” resta la scelta normale;
- entro circa 2,56° dal polo si passa con smoothstep all'asse locale +X
  proiettato sul piano ortogonale alla vista;
- al polo esatto la camera guarda dritto verso il target, senza il fallback
  `z.z += 1e-4` di `Matrix4::lookAt`;
- occhio e target coincidenti conservano l'orientamento autore, senza NaN.

### 2. Zoom AE ↔ FOV

`pinholeFocal(viewportHeight, fovDegrees)` è la relazione
`zoom = H / (2·tan(FOV/2))`. Il test fissa la relazione e verifica che un piano
400×400 a `Z = zoom` proietti esattamente 400×400 px al centro del viewport.

### 3. Cono e feather AE

In `Lighting.hpp`:

- `aeSpotHalfAngleRadians(totalDegrees)` converte l'apertura totale AE
  `(0, 360]` nel semiangolo in radianti;
- `aeSpotPenumbra(featherPercent)` converte il feather `[0, 100]` in
  penumbra normalizzata `[0, 1]`;
- `SpotLight::setAeConeAngleDegrees()` e
  `SpotLight::setAeConeFeatherPercent()` applicano le due conversioni;
- valori non finiti o fuori range vengono rifiutati con
  `std::invalid_argument`.

### 4. Falloff point AE in C3D

`PointLightSource` ha ora `PointLightFalloff`:

- `SmoothInversePower` è il default e conserva la curva storica
  `1/(1+(d/radius)^decay)`;
- `AeInverseSquareClamped` usa `radius` come inizio del clamp: `1` fino al
  raggio, poi `(radius/d)^2`; `decay` è ignorato in questa modalità;
- `point_light_attenuation()` è la funzione CPU condivisa dal test e da
  `evaluate_point_light_irradiance()`;
- `lighting.comp` ha il ramo AE; il payload GPU resta di 16 float e usa il lane
  `w` già riservato per il modello;
- `LightLayerPlan`/decoder/schema accettano
  `falloff: "smooth_inverse_power" | "ae_inverse_square_clamped"`;
- CM3D `LightFalloff` è propagato da `Light`/`LightSample` a `LightRig`,
  serializzazione e lowering `LightRigRenderPlan`.

L'audit usa la funzione C3D di produzione, non una formula duplicata.

### 5. Test ed evidenza

- Nuovo target CM3D `chrononmotion_ae_parity` (`tests/motion_ae_parity.cpp`),
  registrato in CTest.
- Nuovo caso C3D in `tests/renderer/lighting/test_light_context.cpp`.
- Audit JSON reso valido (il campo `canvas` non era JSON) e descrizione del
  test 2 allineata al risultato.
- Evidenza aggiornata in `docs/evidence/ae3d-parity-20261106/README.md`.

## Criteri di accettazione

- [x] Il target resta centrato per un'orbita completa di 720 passi.
- [x] Attraversando il polo non c'è un flip istantaneo di 180°; il roll cambia
  con continuità e il target resta centrato.
- [x] Al polo esatto la direzione è `(0, -1, 0)` e la posa è finita.
- [x] Zoom AE e FOV verticale sono la stessa lente; il piano 400×400 proietta
  400×400 px.
- [x] Cono totale AE e feather percentuale producono semiangolo e penumbra;
  input invalidi sono rifiutati.
- [x] Il falloff point AE produce 1 a `r_start` e 0.25 a `2·r_start` nella CPU
  C3D e nel ramo shader.
- [x] `chrononmotion_ae_parity` e `chrononmotion_view` passano in CTest.
- [x] `check_motion_boundary` e `check_render_scope` passano.
- [ ] Shadow map/PCF C3D: richiede un pass 3D dedicato; resta `GAP`.

## Risultati audit (dopo il lavoro)

| # | Contratto | Stato | Nota |
|---|-----------|-------|------|
| 1 | Proiezione 1:1 a Z=0 | PASS | zoom 2666.666667 px, bbox 400×400, coverage 160000; GPU readback non eseguito |
| 2 | Camera a due nodi / look-at | PASS | orbita: errore max 0.000244 px; polo: 0.000000 px, roll step 0.0000°, view step 0.027976° |
| 3 | Falloff point | PASS | modalità AE: I(r)=1.000000, I(2r)=0.250000; default smooth invariato |
| 4 | Cono e feather spot | PASS | 60° → 30.000001° half-angle, feather 0.5 → inner 15°; max step 0.005355 |
| 5 | DoF thin-lens | PASS | CoC 0 a fuoco; near/far 1.5; errore relativo max 7.3e-8; kernel C3D |
| 6 | Shadow map e PCF | GAP | assente nel backend Vulkan di produzione; `ShadowNode` è 2.5D proiettato |

Sintesi: `PASS=5 PARTIAL=0 FAIL=0 GAP=1`. L'audit esce con codice 1 per il solo
GAP 6. Non viene dichiarato risolto.

## Verifiche eseguite

```shell
cd ChrononMotion3D
cmake -S . -B build/chronon/dev -DCHRONONMOTION_BUILD_TESTS=ON
cmake --build build/chronon/dev --target chrononmotion_ae_parity -j 2
./build/chronon/dev/chrononmotion_ae_parity
# 28 checks, 0 failures

./build/chronon/dev/chrononmotion_view
# 147 checks, 0 failures

./out/ae3d_parity/ae3d_parity_audit --json out/ae3d_parity/audit.json
# JSON valido; PASS=5 PARTIAL=0 FAIL=0 GAP=1; exit 1 atteso per il GAP 6

cmake -DSOURCE_DIR="$PWD" -P tests/check_motion_boundary.cmake
# 324 files scanned, boundary intact
cmake -DSOURCE_DIR="$PWD" -P tests/check_render_scope.cmake
# render sibling scope intact

# C3D mirato
glslangValidator -V ../Chronon3d/src/backends/vulkan/shaders/lighting.comp -o /tmp/lighting.spv
# PASS
# header-only falloff test: PASS
```

Due esecuzioni consecutive dell'audit hanno prodotto JSON byte-identico,
SHA-256 `eb0a5074f9437fc211ca94a38612f0ae50320aa61cb9caac0afcecb4bdde5d8d`.

### Limite di build C3D

Il target completo `chronon3d_renderer_core_tests` è stato configurato, ma il
rebuild mirato ha superato il timeout di 600 s e il filesystem aveva circa 8 GB
liberi. Non viene dichiarata alcuna esecuzione completa della suite C3D. La
validazione C3D qui è limitata al test header-only del falloff e alla
compilazione dello shader.

## File toccati

### ChrononMotion3D

- `include/chrononmotion/motion/Lighting.hpp`
- `include/chrononmotion/motion/LightRig.hpp`
- `src/chrononmotion/motion/CameraRig.cpp`
- `src/chrononmotion/motion/Lighting.cpp`
- `src/chrononmotion/motion/LightRig.cpp`
- `src/chrononmotion/motion/LightRigRenderPlan.cpp`
- `tests/motion_ae_parity.cpp` (nuovo)
- `tests/motion_light_rig.cpp`
- `CMakeLists.txt`
- `tools/ae3d_parity_audit.cpp`
- `docs/evidence/ae3d-parity-20261106/README.md`
- questo ticket

### Chronon3d

- `include/chronon3d/rendering/light_context.hpp`
- `include/chronon3d/rendering/lighting_eval.hpp`
- `include/chronon3d/render_plan/render_plan_layer_types.hpp`
- `src/render_plan/render_plan_decoder_layer.cpp`
- `src/render_plan/render_plan_compiler_scene_materializers.cpp`
- `src/render_graph/lighting_hashing.cpp`
- `src/render_graph/nodes/lighting_node.cpp`
- `src/backends/vulkan/shaders/lighting.comp`
- `schemas/json/chronon.render-plan.v2.schema.json`
- `schemas/json/chronon.render-plan.v3.extensions.schema.json`
- `tests/renderer/lighting/test_light_context.cpp`

## Residui fuori perimetro

- **Shadow map / PCF / Shadow Diffusion:** richiede un pass 3D dedicato, una
  depth target in light space e il sampling nel lighting. `ShadowNode` è una
  proiezione 2.5D e non va spacciata per una shadow map. Tracciato in
  `Chronon3d/docs/tickets/TICKET-SHADOW-MAP-PCF-20261106.md`.
- **VMA / sincronizzazione / hot reload:** infrastruttura Vulkan; nessun
  consumatore CM3D. L'unica collocazione possibile sarebbe `render/` (hot
  reload, P2), fuori da questo ticket.
