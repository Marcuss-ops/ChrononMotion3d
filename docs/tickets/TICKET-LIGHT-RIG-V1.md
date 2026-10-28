# TICKET-LIGHT-RIG-V1 — Light rig come asset di scena (preset editoriali)

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations #25 "Cinematic Lighting": 4 spot su null rotatable per un rig
cinematico). Stato registrato in `docs/stato/chronon-motion3d.md` §5.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Modello `LightRig` (sorgenti tipizzate + transform di rig rotabile) e contratto `LIGHT_RIG_V1` con cap sorgenti, ordine deterministico e fixture.
- [ ] ≥3 preset editoriali (key+rim+fill; 4-spot edge; soft top) con id stabile stile `presets::catalog()`.
- [ ] Bake verso il formato luce multipla atteso da `TICKET-SPOTLIGHT-RIG.md` (Chronon3d); rotazione del rig animata con easing per-keyframe.
- [ ] Showcase case con golden + firma stabile (stile `camera_dolly_parallax`).

## Goal

Trattare il light rig come asset di scena autorevole (come il rig motion
esistente): più sorgenti (key, spot, fill, rim) governate da un controller
comune rotabile, con preset editoriali pronti all'uso, analoghi ai preset
`LightingRig::SaaSBlue()`/`PurpleNeon()` del motore ma multi-sorgente e
animabili.

## Scope (questa repo)

1. **Modello**: `LightRig` con sorgenti tipizzate (ogni sorgente: tipo,
   transform, colore, intensità, parametri spot) e transform di rig (il
   "null" del tutorial: rotazione/posizione del rig muove tutte le sorgenti).
2. **Contratto**: `LIGHT_RIG_V1` (cap sorgenti dichiarato, ordine
   deterministico, semantica di parenting) con fixture di regressione.
3. **Preset editoriali**: ≥3 rig cinematografici (key+rim+fill; 4-spot edge;
   soft top) catalogati con id stabile (stile `presets::catalog()`).
4. **Bake verso RenderPlan**: emission del rig nel formato luce multipla
   atteso da `TICKET-SPOTLIGHT-RIG.md` (Chronon3d); rotazione del rig animata
   con easing per-keyframe.
5. **Certificazione**: showcase case (golden + firma) stile
   `camera_dolly_parallax`.

## Fuori scope

- Il rendering delle luci/ombre: Chronon3d
  (`TICKET-SPOTLIGHT-RIG.md`, `TICKET-SHADOW-NATIVE-CATCHER.md`).
- HDRI come sorgente: `TICKET-HDRI-ENV-V1.md` + ticket IBL Chronon3d.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`).

1. **Contratto `LIGHT_RIG_V1`** (pattern di `docs/GEO_CAMERA_V1.md`): cap
   sorgenti dichiarato, ordine deterministico, semantica di parenting, fixture
   di regressione. Fissare qui anche il formato di bake atteso da
   `TICKET-SPOTLIGHT-RIG.md` (Chronon3d), anche solo come interfaccia scritta.
2. **Modello `LightRig`** — sorgenti tipizzate (tipo, transform, colore,
   intensità, parametri spot) + transform di rig rotabile (il "null" del
   tutorial).
   Verifica: costruzione e serializzazione deterministiche.
3. **≥3 preset editoriali** (key+rim+fill; 4-spot edge; soft top) con id
   stabile, stile `presets::catalog()`.
4. **Bake verso RenderPlan** nel formato luce multipla atteso da
   `TICKET-SPOTLIGHT-RIG.md`; rotazione del rig animata con easing
   per-keyframe (usare la resolver condivisa già in main).
   Verifica: assert geometriche — la rotazione del rig aggiorna tutte le
   sorgenti coerentemente; payload oltre cap respinto fail-closed.
5. **Certificazione showcase** — case con golden + firma stabile su due run
   (stile `camera_dolly_parallax`).
6. **Chiusura formale** — `chrononmotion-showcase --all` invariato; spuntare
   `## Accettazione`; `## Stato` → `COMPLETE`; spostare il file in
   `docs/tickets/archive/`; aggiornare `docs/stato/chronon-motion3d.md` §5.

## Accettazione

- [ ] Contratto `LIGHT_RIG_V1` con limits e fixture; payload oltre cap
      respinto fail-closed.
- [ ] ≥3 preset editoriali certificati nello showcase (golden + firma stabile
      su due run).
- [ ] Rotazione del rig aggiorna tutte le sorgenti in modo coerente (assert
      geometriche sul bake).
- [ ] Suite ChrononMotion3D verde (`chrononmotion-showcase --all` invariato).

## Dipendenze

- `TICKET-SPOTLIGHT-RIG.md` (Chronon3d) per l'esecuzione a valle; l'emissione
  del piano può svilupparsi in parallelo dichiarando l'interfaccia.

## Riferimenti

- `docs/GEO_CAMERA_V1.md` (pattern di contratto camera da replicare per il rig).
- `Chronon3d/include/chronon3d/rendering/lighting_rig.hpp` (preset esistenti
  a sorgente singola).
