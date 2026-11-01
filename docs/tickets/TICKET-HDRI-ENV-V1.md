# TICKET-HDRI-ENV-V1 — Lettura HDRI e ambiente light come asset (contratto HDRI_ENV_V1)

<!-- protocollo-chiusura-completa:2026-10-29:v1 -->

## Protocollo di chiusura completa (2026-10-29)

> Autorità dedicata: sezione **55 — round-trip CLI visibile; GPU downstream** di `../../../PIANO-CHIUSURA-TICKET-APER-TI-2026-10-29.md` (fase consigliata **F2** · priorità **P2**).
> Questo blocco rende la scheda autosufficiente: riporta qui i criteri dedicati alla chiusura e le regole valide per dichiararla `COMPLETE`.
> Fotografia dell'audit 2026-10-29: prima di eseguire un criterio, verificane l'attualità contro il codice, i test e le evidenze già presenti in questa scheda.

### Criteri dedicati da portare a verde

- [ ] Eseguire round-trip CLI dell’asset HDRI: caricare input fixture, produrre/renderizzare output visibile e verificare diffuse SH + specular mip lookup contro golden.
- [ ] Verificare digest e compatibilità binaria `HDRIENV1`/RenderPlan tra producer Motion3D e consumer Chronon3D, incluso asset resolution e rigetto payload malformed.
- [ ] Per Vulkan strict-native/parità hardware, coordinarsi con `Chronon3d/TICKET-ENVIRONMENT-LIGHT-IBL.md`; allegare test downstream same-SHA. Se è oltre lo scope Motion3D, chiudere questa voce con dipendenza trasferita esplicitamente, non duplicare il codice GPU.

### Regola di chiusura (valida per questa e ogni scheda)

1. Separa sempre **implementazione locale**, **test locale**, **parità GPU o asset/riferimenti esterni**, **CI same-SHA** e **decisioni di prodotto**; una categoria non sostituisce l'altra.
2. Registra per ogni prova: comando completo, SHA (clean o dirty), preset build, host/device, exit status, contatori (test/assert/frame/hash/digest) e percorso dell'evidenza sotto `docs/evidence/`.
3. I gate esterni (runner, content pack, footage, host GPU, decisione di owner) non si chiudono localmente: lo Stato resta `PARTIAL`/`BLOCKED` o `WAITING EXTERNAL` con prerequisito e owner espliciti. Non trasformare mai `NOT RUN`/`FAIL` in `PASS`.
4. Non rimuovere fallback, API o capability fail-closed prima del relativo gate di equivalenza/census; non implementare feature proposte senza la decisione di scope.
5. Chiusura effettiva = tutti i criteri dedicati verdi (oppure formalmente ritirati con motivazione e owner) + aggiornamento di `## Stato` in questa scheda, degli indici canonici se la scheda è indicizzata, di `TODO-RIPRESA-100X-E-TICKET-APER-TI.md`, e archiviazione della scheda secondo le convenzioni del repository.

### Percorso rapido (ordine di esecuzione consigliato)

1. Riconfronta i criteri dedicati qui sopra con lo stato attuale della scheda: le voci già soddisfatte non si rieseguono, si ricollegano all'evidenza esistente.
2. Frammenta l'esecuzione in micro-step: chiudi prima le azioni locali raggiungibili nel checkout corrente, poi le decisioni di contratto/scope aperte, infine prepara e richiedi i gate esterni del piano.
3. Dopo ogni criterio, spontalo in questa sezione con il link all'evidenza; annota nella stessa sezione `NOT RUN`/`BLOCKED` dove la prova non è disponibile, senza cancellare il criterio.
4. Quando tutti i criteri dedicati sono verdi (o ritirati in modo formale), applica la Regola di chiusura (punto 5), aggiorna gli indici canonicali e archivia la scheda.

<!-- /protocollo-chiusura-completa:2026-10-29:v1 -->

## Stato
PARTIAL (updated 2026-10-29). The bounded in-memory Radiance RGBE reader, cubemap
mips, Lambert SH probe, deterministic binary asset, regression fixture, and
showcase golden are implemented. The payload format, V3 RenderPlan producer,
Chronon3D decoder, and focused producer/consumer tests are aligned. CLI runtime
consumption is now connected to Chronon3D's software diffuse SH lighting.
Software specular mip lookup is implemented downstream in Chronon3D. A
current-source software raw-sink probe shows a visible diffuse-SH change on all
20,000 opaque mesh pixels and the producer plan/asset validate successfully.
Quantitative specular mip golden coverage is now closed on the producer side
(2026-11-01): an analytic linear-gradient fixture pins the SH, the cubemap
radiance per texel and the per-mip mean energy, and the SH is additionally
checked against its closed form. The visible CLI round-trip against that golden
and strict-native GPU sampling/parity remain open. Details are in
[`ticket-roundtrip-current-source-20261029`](../evidence/ticket-roundtrip-current-source-20261029/README.md).

## TODO — cosa manca (agg. 2026-10-28)

- [x] `HdriReader` Radiance RGBE equirect → sei facce cubemap, mip chain e 9 SH con resolver esterno.
- [x] Contratto `HDRI_ENV_V1` con limiti hard, budget e fail-closed exclusions.
- [x] Serialization binaria deterministica little-endian e allineamento del formato con il ticket Chronon3D.
- [x] Fixture sintetica, test valid/corrupt/over-budget, e caso showcase golden stabile.
- [x] Campo `environment_light` nel writer V3, path/range validation,
      consumer decoder, asset resolution e software diffuse SH evaluation.
- [x] Decoder C3D del payload binario `HDRIENV1` con limiti e rifiuto dei
      payload troncati, non finiti, fuori ordine o con byte residui.
- [x] Golden quantitativo speculare (2026-11-01): fixture analitico
      `linear-gradient-radiance` (L = base + slope·d), `tests/hdri_specular_golden.json`
      con digest del payload, 27 coefficienti SH, 13 sonde di radianza cubemap su
      tutte e sei le facce e il fondo della catena mip, media per mip come
      invariante di energia. `chrononmotion_hdri` 244 check / 0 failure, due run
      e due `--dump-golden` byte-identici. La chiusura include la verifica in
      forma chiusa: DC SH misurato 6.107/7.220/8.334 contro 6.125/7.239/8.353
      analitici, bande L1 1.501/1.290/0.858 contro 1.500/1.286/0.857 e bande
      superiori a zero entro 5e-3, cioè il probe contiene solo l'asse del
      proprio canale.
- [ ] Ripetere il round-trip CLI visibile con un binario Chronon3D compilato
      dal source corrente e confrontare SH/specular **con la golden qui sopra**;
      il raw probe esistente è diagnostico. Manca solo il lato renderer: il
      riferimento numerico del probe ora è committato in questa repo.
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
- [x] Golden quantitativo speculare: radianza cubemap per texel e per mip,
      energia media per livello e SH contro la forma chiusa del fixture.
- [x] Suite ChrononMotion3D showcase invariata, con il nuovo caso HDRI.

## Dipendenze

- Nessuna a monte. A valle: `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Evidenza locale (2026-11-01, golden speculare)

- `cmake --build build/release-fast --target chrononmotion_hdri -j4` — PASS
  (`-Wall -Wextra -Werror`).
- `./build/release-fast/chrononmotion_hdri` — **PASS: 244 HDRI_ENV_V1 checks**,
  due run consecutive identiche.
- `./build/release-fast/chrononmotion_hdri --dump-golden` — due run
  byte-identiche e identiche a `tests/hdri_specular_golden.json`
  (digest fixture `6abbdbb00f6e6498`, 8239 byte, face 16, 5 mip).
- Controllo negativo: alterare di 1e-2 una radianza o un coefficiente SH nel
  golden fa fallire il test con il messaggio dedicato; il file ripristinato
  torna verde. La golden non è quindi vacua.
- `ctest -j4` completa: **66/66 PASS** (incluso `chrononmotion_hdri`).

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

## Round-trip CLI aggiornato (2026-10-29)

- L'harness producer genera un RenderPlan V3 e un asset `HDRIENV1` a partire
  dalla fixture RGBE. Il consumer `chronon3d_cli validate` lo accetta con
  mounted asset root; il digest contenuto del piano e SHA dell'asset sono
  registrati in [ticket-roundtrip-20261029](../evidence/ticket-roundtrip-20261029/README.md).
- Il software PNG route produce una faccia visibile, ma la CLI dichiara tale
  route non pixel-truth. Il video software/NVENC produce 4/4 frame decodificabili,
  ma il frame HDRI coincide con il controllo glTF half-black/half-green e non
  isola il contributo della probe. Non sono stati confrontati golden SH/specular
  né misurati MAE/p99.
- Stato `PARTIAL`: i byte e la risoluzione asset sono verificati; restano il
  round-trip CLI autorevole con confronto contro golden, il sampling strict
  Vulkan downstream e la certificazione CI/hardware. Questa scheda non duplica
  l'implementazione IBL GPU di Chronon3D.

## Round-trip visibile aggiornato (2026-10-29)

- Fixture CLI centrata e scalata: producer rebuild PASS, 349 checks, CTest
  authoring 1/1 PASS. Con il raw sink, il controllo senza `environment_light`
  ha `(6,71,32,255)` al pixel `(320,180)`; con SH rosso diventa
  `(25,71,32,255)` e cambiano tutti i 20.000 pixel opachi della mesh.
- Il CLI Release usato per questo probe è compilato da `67fd33f7f`, mentre la
  source HEAD Chronon3D è `014eeb4df`; il risultato è quindi diagnostico finché
  il probe non viene ripetuto sul binario current-source. L'export NVENC di quel
  binario non ha pixel fedeli. Strict-native Vulkan, parità hardware e CI
  same-SHA restano aperti. Dettagli:
  [ticket-roundtrip-scaled-20261029](../evidence/ticket-roundtrip-scaled-20261029/README.md).

## Rerun current-source (2026-10-29)

- Il CLI corrente accetta piano e asset HDRI. Nel software raw sink la probe
  SH rossa cambia tutti i 20.000 pixel opachi rispetto al piano di controllo;
  i digest e i valori campione sono registrati nell'evidenza dedicata.
- Questo dimostra il contributo SH diffuso, non il golden quantitativo speculare
  né la parità Vulkan. La fedeltà dell'export encoded è un difetto downstream
  attuale e resta aperta insieme a strict-native GPU e CI same-SHA.
- Evidenza: [ticket-roundtrip-current-source-20261029](../evidence/ticket-roundtrip-current-source-20261029/README.md).
