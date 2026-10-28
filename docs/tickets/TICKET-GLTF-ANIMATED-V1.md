# TICKET-GLTF-ANIMATED-V1 — glTF animato: skins, morph targets, animation channels

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations: 9 video importano GLB animati in AE 2026). Stato registrato
in `docs/stato/chronon-motion3d.md` §5.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Estendere `GLTF_READER` (animations channels/sampler, skins, morph targets) con limits dichiarati e fixture di regressione per famiglia.
- [ ] Runtime: canoni → `CLIP_LIBRARY`, skin/morph → `RIGGING_FINAL_V1`/`AUTORIG_GLB` (no render-frame path).
- [ ] Bake su `inline_mesh.animation` V3 secondo il contratto geometria/skin concordato con `TICKET-INLINE-MESH-TEXTURED-ALPHA` (Chronon3d) prima di implementare.
- [ ] Showcase case dedicati (skinned + morph) stile golden `camera_dolly_parallax`.

## Goal

Estendere `GLTF_READER` oltre il contratto static-only attuale. Oggi animazioni,
skins, morph targets e weights sono **rifiutati fail-closed per contratto**
(`docs/GLTF_READER.md`, "Fail-closed exclusions"); `GltfReaderAnimations.cpp`
importa solo la motion data proprietaria Chronon, non i canoni di animazione
standard glTF. Il contratto stesso prescrive: "Expanding it requires
additional limits and regression fixtures for the new feature family; do not
route it through Chronon3D's render-frame path".

## Scope (questa repo)

1. **Estensione contratto `GLTF_READER`** (o nuovo `GLTF_READER_V2` dichiarato):
   - `animations.channels` (TRS/weights, sampler con interpolation
     LINEAR/STEP/CUBICSPLINE dichiarata), skins (joints/weights/inverse bind
     matrices), morph targets (POSITION/NORMAL deltas).
   - Nuovi limiti numerici dichiarati (canoni, chiavi, joints per skin) e
     fixture di regressione per ogni famiglia.
2. **Runtime**: canoni → `CLIP_LIBRARY` (`docs/CLIP_LIBRARY.md`), skin/morph →
   `RIGGING_FINAL_V1`/`AUTORIG_GLB`; nessun accesso al render-frame path.
3. **Bake** su `inline_mesh.animation` dello schema V3 (1..4096 campioni,
   `frame` 0..1000000, `positions` 3D, più UV/normali dove richiesto dal
   contratto geometria/skin concordato con Chronon3d).
4. **Certificazione**: showcase case dedicati (stile golden
   `camera_dolly_parallax`/`dolly_zoom`).

## Fuori scope

- Il rendering del soggetto textured/alpha: Chronon3d,
  `TICKET-INLINE-MESH-TEXTURED-ALPHA.md` (contratto da accordare insieme).
- Estensioni glTF non standard (compression, altri primitive modes): restano
  esclusi.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`).

1. **Fase 0 obbligatoria: accordo dell'interfaccia** col ticket gemello
   Chronon3d `TICKET-INLINE-MESH-TEXTURED-ALPHA.md` sul contratto
   geometria/skin per frame (attributi variabili per frame, budget). Il
   contratto attuale lo impone: senza accordo non si implementa.
2. **Estensione contratto `GLTF_READER`** (o `GLTF_READER_V2` dichiarato) in
   `docs/GLTF_READER.md`: animations channels/sampler (interpolation
   LINEAR/STEP/CUBICSPLINE dichiarata), skins (joints/weights/inverse bind
   matrices), morph targets (POSITION/NORMAL deltas); nuovi limits numerici
   (canoni, chiavi, joints per skin) e fixture di regressione per famiglia.
   Verifica: input invalidi → `std::invalid_argument` con prefisso `glTF:`.
3. **Routing runtime** secondo la prescrizione del contratto: canoni →
   `CLIP_LIBRARY` (`docs/CLIP_LIBRARY.md`), skin/morph → `RIGGING_FINAL_V1`/
   `AUTORIG_GLB`; mai passati dal render-frame path.
4. **Bake su `inline_mesh.animation`** dello schema V3 (1..4096 campioni,
   `frame` 0..1000000, `positions` 3D, più UV/normali dove richiesto
   dall'accordo del passo 1), deterministico.
   Verifica: bake riproducibile su due run.
5. **Certificazione showcase** — case dedicati (skinned + morph) stile golden
   `camera_dolly_parallax`/`dolly_zoom`; suite esistente invariata (nessuna
   regressione sul statico).
6. **Chiusura formale** — `chrononmotion-showcase --all` verde; spuntare
   `## Accettazione`; `## Stato` → `COMPLETE`; spostare il file in
   `docs/tickets/archive/`; aggiornare `docs/stato/chronon-motion3d.md` §5.

## Accettazione

- [ ] Contratto esteso con limits + fixture per animations/skins/morph; casi
      invalidi rifiutati con `std::invalid_argument` prefisso `glTF:`.
- [ ] Clip importata → bake deterministico su `inline_mesh.animation`,
      riproducibile su due run.
- [ ] Modelli di test (skinned + morph) certificati nello showcase; suite
      esistente invariata (nessuna regressione sul statico).
- [ ] Interfaccia verso il bridge V3 concordata col ticket Chronon3d gemello.

## Dipendenze

- `TICKET-PBR-MATERIALS-V1.md` per l'aspetto visivo del soggetto (desiderabile).
- `TICKET-INLINE-MESH-TEXTURED-ALPHA.md` (Chronon3d) per la resa a valle.

## Riferimenti

- `docs/GLTF_READER.md` (stato attuale: fail-closed su queste famiglie).
- `docs/CLIP_LIBRARY.md`, `docs/RIGGING_FINAL_V1.md`, `docs/AUTORIG_GLB.md`.
