# TICKET-PBR-MATERIALS-V1 — Materiali 3D PBR (smoothness/reflection/emissive)

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations: i tutorial "advanced 3D" regolano smoothness/reflection dei
materiali, #21/#13/#27). Stato registrato in `docs/stato/chronon-motion3d.md` §5.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] `Material` PBR (`base_color`, `metallic`, `roughness` → smoothness, `emissive`, `double_sided`) dai `materials` glTF con validazione fail-closed.
- [ ] Texture V1: solo `base_color_texture` e `metallic_roughness_texture` (normal/occlusion fuori scope).
- [ ] Contratto `MATERIAL_PBR_V1` (o estensione `THREEPP_*`) con limits, fixture e serialization deterministica verso il bridge.
- [ ] Interfaccia roughness ↔ IBL specular concordata con `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Goal

Estendere il modello materiali (`src/chrononmotion/materials/Material.cpp`,
oggi minimale) al sottoinsieme PBR richiesto dai tutorial AE: base color,
metallic/roughness (→ smoothness), reflection con HDRI environment, emissive.
Oggi `GLTF_READER` ignora i materiali per contratto ("any render intent
remains caller-owned").

## Scope (questa repo)

1. **Modello**: `Material` con `base_color`, `metallic`, `roughness`
   (smoothness = 1 - roughness, mappatura dichiarata), `emissive`,
   `double_sided`; letti dai `materials` glTF (PBR metallicRoughness) con
   validazione fail-closed.
2. **Texture**: solo `base_color_texture` e `metallic_roughness_texture` in V1
   (UV già importate dal reader statico); normal/occlusion map fuori scope.
3. **Contratto**: estensione `THREEPP_*` o nuovo `MATERIAL_PBR_V1` con limits
   e fixture; serialization deterministica verso il bridge.
4. **Coerenza con la luce**: il materiale espone i parametri che il motore
   consuma per IBL specular (interfaccia concordata con
   `TICKET-ENVIRONMENT-LIGHT-IBL.md`).

## Fuori scope

- Shader e resa dei materiali: Chronon3d.
- Clearcoat/sheen/transmission/ior completi: fuori V1.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`).

1. **Contratto** (`MATERIAL_PBR_V1` o estensione `THREEPP_*`, stile
   `docs/THREEPP_CERTIFICATION.md`): limits numerici, mappatura dichiarata
   smoothness = 1 - roughness, fixture di regressione.
2. **Modello `Material`** in `src/chrononmotion/materials/Material.cpp`:
   `base_color`, `metallic`, `roughness`, `emissive`, `double_sided` letti dai
   `materials` glTF (PBR metallicRoughness) con validazione fail-closed.
   Verifica: input invalidi respinti con errore dichiarato.
3. **Texture V1** — solo `base_color_texture` e `metallic_roughness_texture`
   (UV già importate dal reader statico); normal/occlusion map restano fuori
   scope come da Goal.
4. **Interfaccia roughness ↔ IBL specular** concordata con
   `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d) e documentata nel contratto:
   è il punto di contatto col motore, va fissato prima della serialization.
5. **Serialization deterministica** verso il bridge + golden di round-trip
   materiale → RenderPlan.
   Verifica: stessa golden su due run.
6. **Chiusura formale** — suite ChrononMotion3D verde, nessuna regressione sui
   golden esistenti; spuntare `## Accettazione`; `## Stato` → `COMPLETE`;
   spostare il file in `docs/tickets/archive/`; aggiornare
   `docs/stato/chronon-motion3d.md` §5.

## Accettazione

- [ ] Materiali glTF metallicRoughness importati e validati; input invalidi
      rifiutati fail-closed.
- [ ] Golden deterministica del round-trip materiale → RenderPlan.
- [ ] Interfaccia roughness/smoothness concordata col motore (documentata nel
      contratto).
- [ ] Suite ChrononMotion3D verde; nessuna regressione sui golden esistenti.

## Dipendenze

- `TICKET-HDRI-ENV-V1.md` per la reflection (desiderabile, non bloccante).
- A valle: `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Riferimenti

- `docs/GLTF_READER.md` ("Materials ... intentionally ignored only by
  absence: any render intent remains caller-owned").
- `docs/THREEPP_CERTIFICATION.md`.
