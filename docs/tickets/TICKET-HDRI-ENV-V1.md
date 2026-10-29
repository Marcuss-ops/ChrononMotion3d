# TICKET-HDRI-ENV-V1 — Lettura HDRI e ambiente light come asset (contratto HDRI_ENV_V1)

## Stato
PARTIAL (2026-10-28). The bounded in-memory Radiance RGBE reader, cubemap
mips, Lambert SH probe, deterministic binary asset, regression fixture, and
showcase golden are implemented. The payload format, V3 RenderPlan producer,
Chronon3D decoder, and focused producer/consumer tests are aligned. CLI runtime
consumption is now connected to Chronon3D's software diffuse SH lighting.
Software specular mip lookup is implemented downstream in Chronon3D. Visible
CLI rendering remains open; GPU sampling/parity is an external follow-up.

## TODO — cosa manca (agg. 2026-10-28)

- [x] `HdriReader` Radiance RGBE equirect → sei facce cubemap, mip chain e 9 SH con resolver esterno.
- [x] Contratto `HDRI_ENV_V1` con limiti hard, budget e fail-closed exclusions.
- [x] Serialization binaria deterministica little-endian e allineamento del formato con il ticket Chronon3D.
- [x] Fixture sintetica, test valid/corrupt/over-budget, e caso showcase golden stabile.
- [x] Campo `environment_light` nel writer V3, path/range validation,
      consumer decoder, asset resolution e software diffuse SH evaluation.
- [x] Decoder C3D del payload binario `HDRIENV1` con limiti e rifiuto dei
      payload troncati, non finiti, fuori ordine o con byte residui.
- [ ] CLI runtime round-trip con output visibile; SH diffusa e lookup speculare
      sono consumate dal renderer software downstream.
- [ ] Vulkan strict-native sampling e parità hardware (ticket downstream).

## Goal

Importare HDRI equirectangolari e produrre un environment asset (cubemap +
mip + probe SH) consumabile dal motore Chronon3D, seguendo lo stampo del
`GltfReader`: boundary in-memory, dependency-free, fail-closed, nessun I/O
filesystem/rete dal core.

## Scope (questa repo)

1. **`HdriReader`** in `src/chrononmotion/assets/` (stesso pattern di
   `GltfReader.cpp` + resolver esterno che fornisce i byte):
   - input equirect RGBE/HDR float (formato dichiarato nel contratto);
   - output: cubemap per faccia, catena di mip, coefficienti SH di
     irradianza (9), metadati (rotation di default, intensity).
2. **Contratto `HDRI_ENV_V1`** in `docs/HDRI_ENV_V1.md` (stile
   `GLTF_READER.md`): accepted input, limits numerici (dimensioni massime,
   byte budget), fail-closed exclusions, semantica del resolver.
3. **Serialization** deterministica (stile `GltfReaderSerialization.cpp`) per
   il passaggio al bridge RenderPlan.

## Fuori scope

- Il sampling IBL negli shader e il modello `EnvironmentLight`: Chronon3d,
  `TICKET-ENVIRONMENT-LIGHT-IBL.md` (riceve cubemap + SH da questo ticket).
- Filesystem e rete: restano del caller.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`).

1. **Contratto `docs/HDRI_ENV_V1.md` prima del codice** (stile
   `docs/GLTF_READER.md`): accepted input (equirect RGBE/HDR float, formato
   dichiarato), limits numerici (dimensioni massime, byte budget), fail-closed
   exclusions, semantica del resolver. Qui si fissa anche l'interfaccia
   cubemap+SH verso `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).
2. **`HdriReader`** in `src/chrononmotion/assets/` sullo stampo di
   `GltfReader.cpp`: boundary in-memory, dependency-free, nessun I/O dal core;
   equirect → cubemap per faccia + catena mip + coefficienti SH 9; errori
   `std::invalid_argument` con prefisso `hdri:`.
   Verifica: fixture di regressione (casi validi, corrotti, oltre budget).
3. **Serialization deterministica** (stile `GltfReaderSerialization.cpp`)
   verso il bridge RenderPlan.
   Verifica: stessi byte su due run.
4. **Round-trip verso il bridge V3 validato dal CLI Chronon3D** (confine
   `docs/BOUNDARY_C3D.md`): il decoder accetta il campo e i test ABI coprono
   schema/fingerprint/validazione. Il CLI non può renderizzare il payload finché
   il compiler valida l'asset e collega i coefficienti SH alla luce software;
   il render visibile CLI e il cubemap specular restano da completare.
5. **Golden** — cubemap+SH di un HDRI di test deterministici su due run;
   `chrononmotion-showcase --all` invariato (nessuna regressione).
6. **Chiusura formale** — spuntare `## Accettazione`; `## Stato` → `COMPLETE`;
   spostare il file in `docs/tickets/archive/` (stessa convenzione di
   Chronon3d, `TICKET-PER-KEYFRAME-EASING.md`); aggiornare
   `docs/stato/chronon-motion3d.md` §5.

## Accettazione

- [x] Contratto `HDRI_ENV_V1` pubblicato con limiti dichiarati e fixture di
      regressione (casi validi, corrotti, oltre budget → `std::invalid_argument`
      con prefisso `hdri:`).
- [x] Golden: cubemap+SH di un HDRI di test deterministici su due run.
- [x] Campo V3 emesso, decoder/fingerprint consumer testati, asset binario
      validato e SH diffusa collegata al lighting software.
- [ ] CLI runtime render dell'asset dopo supporto renderer.
- [x] Suite ChrononMotion3D showcase invariata, con il nuovo caso HDRI.

## Dipendenze

- Nessuna a monte. A valle: `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Evidenza locale (2026-10-28)

- `cmake --build build/release-fast --target chrononmotion_hdri chrononmotion_authoring -j12` — PASS.
- `ctest --test-dir build/release-fast --output-on-failure -R 'chrononmotion_(authoring|hdri)'` — 2/2 PASS.
- Motion V3 producer bridge test covers attachment of decoded SH, asset
  reference emission, and traversal rejection.
- Chronon3D focused consumer tests
  `chronon3d_c_abi_tests --test-case='HDRI*' --no-skip` — 3/3 cases, 34/34
  assertions PASS after software SH integration, including binary decode,
  mounted-root resolution, SH consistency, and compiler preparation.
- Chronon3D CLI target `cmake --build build/chronon/linux-video-fast-dev
  --target chronon3d_c_abi_tests chronon3d_cli -j12` — PASS after software SH
  integration; focused HDRI tests pass 3/3 cases, 34/34 assertions.
- Chronon3D full C ABI suite
  `ctest --test-dir ../Chronon3d/build/chronon/linux-video-fast-dev
  --output-on-failure -R '^chronon3d_c_abi_tests$'` — PASS, 1/1 target after
  adding the HDRI asset runtime boundary.
- `ctest --test-dir build/release-fast -R '^chrononmotion_(hdri|light_rig|authoring|rig_pipeline|gltf|showcase_all)$' --output-on-failure` — 6/6 PASS.
- Chronon3D software HDRI specular: `chronon3d_renderer_core_tests` PASS
  (1/1, 8.42 s), con campionamento cubemap/mip guidato dalla roughness.
- Chronon3D `chronon3d_c_abi_tests` PASS (1/1, 354.45 s) e target CLI build
  con `-j12` PASS dopo l'integrazione.
- Two consecutive `chrononmotion-showcase --dump-golden` outputs are byte-for-byte equal and match `tests/showcase_golden.json` (63/63 cases).
- Runtime CLI visible rendering and Chronon3D GPU consumption are not yet
  claimed. The compiler now prepares the environment and software shading
  consumes diffuse SH plus roughness-selected specular cubemap mips. GPU parity
  remains open.

## Verifica software CLI aggiornata — 2026-10-29

- `chrononmotion_(hdri|light_rig|authoring|rig_pipeline|gltf|showcase_all)` in
  `build/release-fast` — 6/6 PASS.
- Il CLI Chronon3D ha compilato il piano con e senza `environment_light` e ha
  completato l'esportazione raw software. Entrambi i file contengono quattro
  frame uniformi neri (frame da 64×64 RGBA); quindi non sono pixel proof e non
  dimostrano l'effetto visivo dell'HDRI. Il PNG still mostra geometria, ma la
  lane still non è una superficie di misura attendibile.
- Rimane necessario completare il consumo visibile dell'inline mesh nel
  percorso video/software. Nessuna parità Vulkan o GPU è dichiarata.

## Riferimenti

- `docs/GLTF_READER.md` (pattern del boundary da replicare).
- `docs/stato/chronon-motion3d.md` §3 (bridge RenderPlan V3).
