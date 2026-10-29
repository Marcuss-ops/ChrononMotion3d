# TICKET-PBR-MATERIALS-V1 — Materiali 3D PBR (smoothness/reflection/emissive)

## Stato
COMPLETE (2026-10-28). Static glTF import now creates renderer-independent
`PbrMaterial` data for base/emissive color, roughness, metalness, alpha and
double-sided state, with numeric validation. Exact scalar serialization and
external base-color texture identity now cross the MotionScene→RenderPlan V3
bridge; metallic-roughness URI reaches inline RenderPlan V3, where Chronon3D
samples base color, roughness G and metallic B in software. Provenienza: gap
analysis "AE + Premiere parity"
(MotionNations: i tutorial "advanced 3D" regolano smoothness/reflection dei
materiali, #21/#13/#27). Stato registrato in `docs/PROJECT_STATUS.md`.

## TODO — cosa manca (agg. 2026-10-28)

- [x] `PbrMaterial` imports glTF base color, metallic, roughness, emissive,
      alpha mode/cutoff and double-sided state with fail-closed validation.
- [x] `base_color_texture`: external URI, explicit CLAMP_TO_EDGE sampler and
      TEXCOORD_0 pass through `PbrMaterial`, `MeshAttachment` and RenderPlan V3.
- [x] `metallic_roughness_texture`: G/B channel sampling in the inline material.
      Embedded images, mip filters, wrap-repeat and extensions
      stay fail-closed; normal/occlusion are outside V1.
- [x] Contratto `MATERIAL_PBR_V1` con bounds scalar, semantica roughness,
      serialization deterministica e consumer V3 definito.
- [x] Interfaccia roughness ↔ IBL specular concordata: glTF roughness remains
      in [0,1], where 0 is mirror-like and 1 is fully rough; the planned
      Chronon3D material contract will use microfacet roughness directly
      (smoothness = 1 - roughness).

## Goal

Estendere il modello materiali (`src/chrononmotion/materials/Material.cpp`,
oggi minimale) al sottoinsieme PBR richiesto dai tutorial AE: base color,
metallic/roughness (→ smoothness), reflection con HDRI environment, emissive.
Static `GLTF_READER` imports this factor subset and external base-color image
identity; `loadGltfMotion` remains material-free and texture handling remains
out of scope for its V1 path.

## Scope (questa repo)

1. **Modello**: `Material` con `base_color`, `metallic`, `roughness`
   (smoothness = 1 - roughness, mappatura dichiarata), `emissive`,
   `double_sided`; letti dai `materials` glTF (PBR metallicRoughness) con
   validazione fail-closed.
2. **Texture**: `base_color_texture` supports an external mounted-root-relative
   URI with explicit clamp sampler and UV0; `metallic_roughness_texture`
   carries its mounted asset identity through inline V3 and samples G/B in the
   Chronon3D software renderer; normal/occlusion maps are out of scope.
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
2. **Modello `PbrMaterial`** in `include/chrononmotion/materials/PbrMaterial.hpp`:
   `base_color`, `metallic`, `roughness`, `emissive`, `double_sided` letti dai
   `materials` glTF (PBR metallicRoughness) con validazione fail-closed.
   Verifica: input invalidi respinti con errore dichiarato.
3. **Texture V1** — `base_color_texture` and `metallic_roughness_texture`
   external mounted assets are imported and emitted; the Chronon3D inline mesh
   consumer samples base color/alpha and metallic-roughness G/B. Normal and
   occlusion maps remain out of scope.
4. **Interfaccia roughness ↔ IBL specular** concordata con
   `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d) e documentata nel contratto:
   è il punto di contatto col motore, va fissato prima della serialization.
5. **Serialization deterministica** verso il bridge + golden di round-trip
   materiale → RenderPlan.
   Verifica: stessa golden su due run.
6. **Chiusura formale** — suite ChrononMotion3D verde, nessuna regressione sui
   golden esistenti; spuntare `## Accettazione`; `## Stato` → `COMPLETE`;
   spostare il file in `docs/tickets/archive/`; aggiornare
   `docs/PROJECT_STATUS.md`.

## Accettazione

- [x] Materiali glTF metallicRoughness importati e validati; input invalidi
      rifiutati fail-closed.
- [x] Golden deterministica del round-trip materiale → RenderPlan per i campi
      scalari V1 e riferimenti texture.
- [x] Texture glTF esterne per base color e metallic-roughness validate, portate
      nel RenderPlan V3 e consumate dal renderer software inline.
- [x] Interfaccia roughness/smoothness concordata col motore (documentata nel
      contratto).
- [x] Suite ChrononMotion3D mirata verde, inclusi authoring, glTF, HDRI,
      LightRig, rig pipeline e showcase.

## Evidenza

- `MaterialDescriptor` carries base color, metallic, roughness, emissive,
  opacity/transparent and double-sided intent. Validation rejects non-finite
  or negative color channels and out-of-range PBR scalars.
- `chrononmotion_authoring` exercises the roughness/metalness/double-sided
  descriptor and rejects non-finite roughness — PASS after `-j12` build.
- `chrononmotion_gltf` imports PBR factors, alpha mode/cutoff, emissive,
  double-sided state and mounted-root-relative base-color/metallic-roughness
  texture identities; invalid metallic factors, unsafe paths, embedded images
  and unsupported samplers fail closed.
- `docs/MATERIAL_PBR_V1.md` records field names, bounds, roughness convention,
  compatibility behavior, and the explicit texture boundary.
- Motion emits exact `metallic`, `roughness`, `emissive`, and `double_sided`
  values plus mask `alpha_cutoff` in RenderPlan V3. Chronon3D validates,
  fingerprints and carries them into software shading alongside the existing
  Phong-compatible values.
- `ctest --test-dir build/release-fast --output-on-failure -R
  '^chrononmotion_(hdri|light_rig|authoring|rig_pipeline|gltf|showcase_all)$'`
  — 6/6 PASS.
- La suite mirata è stata rilanciata dopo l'asserzione JSON PBR numerica con
  tolleranza: 6/6 PASS il 2026-10-28.
- Dopo il bridge della base-color texture, la build dei target `chrononmotion_authoring`
  e `chrononmotion_gltf` è PASS; il filtro CTest mirato completo è 6/6 PASS,
  includendo texture URI, bridge asset, path traversal, lowering V3 e showcase.
- Build `chrononmotion_authoring chrononmotion_gltf`: PASS; suite Motion mirata
  aggiornata dopo il fail-closed sul path: 6/6 PASS.
- Chronon3D `chronon3d_renderer_core_tests chronon3d_c_abi_tests -j12` build:
  PASS; renderer core incluse le prove pixel PBR: 1/1 PASS (10.74 s); C ABI:
  1/1 PASS (155.42 s).
- Chronon3D's software renderer samples both glTF metallic-roughness channels
  (roughness G and metallic B) for prepared and inline meshes; the inline path
  also combines base color/alpha texture with the lit PBR material.
- The roughness/smoothness convention is documented for the downstream
  Chronon3D PBR material contract and carried as exact V3 roughness.
- Static glTF material parsing, `meshAttachmentFromGltf` transfer, inline V3
  emission/validation, and software texture sampling are implemented.

## Dipendenze

- `TICKET-HDRI-ENV-V1.md` per la reflection (desiderabile, non bloccante).
- A valle: `TICKET-ENVIRONMENT-LIGHT-IBL.md` (Chronon3d).

## Riferimenti

- `docs/GLTF_READER.md` ("Materials ... intentionally ignored only by
  absence: any render intent remains caller-owned").
- `docs/THREEPP_CERTIFICATION.md`.
- `docs/MATERIAL_PBR_V1.md`.
