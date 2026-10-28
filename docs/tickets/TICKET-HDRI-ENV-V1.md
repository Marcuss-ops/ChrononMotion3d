# TICKET-HDRI-ENV-V1 — Lettura HDRI e ambiente light come asset (contratto HDRI_ENV_V1)

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations 30 video: 9 video "advanced 3D" usano HDRI environment light).
Stato registrato in `docs/stato/chronon-motion3d.md` §5.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] `HdriReader` (equirect RGBE/HDR → cubemap per faccia + catena mip + SH 9) sul pattern di `GltfReader.cpp` con resolver esterno per i byte.
- [ ] Contratto `HDRI_ENV_V1` (`docs/HDRI_ENV_V1.md`) con limits numerici, byte budget e fail-closed exclusions.
- [ ] Serialization deterministica (stile `GltfReaderSerialization.cpp`) verso il bridge RenderPlan.

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
   `docs/BOUNDARY_C3D.md`): il consumatore a valle deve accettare i byte
   prodotti.
5. **Golden** — cubemap+SH di un HDRI di test deterministici su due run;
   `chrononmotion-showcase --all` invariato (nessuna regressione).
6. **Chiusura formale** — spuntare `## Accettazione`; `## Stato` → `COMPLETE`;
   spostare il file in `docs/tickets/archive/` (stessa convenzione di
   Chronon3d, `TICKET-PER-KEYFRAME-EASING.md`); aggiornare
   `docs/stato/chronon-motion3d.md` §5.

## Accettazione

- [ ] Contratto `HDRI_ENV_V1` pubblicato con limiti dichiarati e fixture di
      regressione (casi validi, corrotti, oltre budget → `std::invalid_argument`
      con prefisso `hdri:`).
- [ ] Golden: cubemap+SH di un HDRI di test deterministici su due run.
- [ ] Round-trip verso il bridge V3 validato dal CLI Chronon3D.
- [ ] Suite ChrononMotion3D verde (`chrononmotion-showcase --all` invariato).

## Dipendenze

- Nessuna a monte. A valle: `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Riferimenti

- `docs/GLTF_READER.md` (pattern del boundary da replicare).
- `docs/stato/chronon-motion3d.md` §3 (bridge RenderPlan V3).
