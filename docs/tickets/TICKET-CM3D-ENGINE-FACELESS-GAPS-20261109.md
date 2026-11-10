# TICKET-CM3D-ENGINE-FACELESS-GAPS-20261109 — Registro dei gap engine per i formati faceless

- **Stato:** OPEN (2026-11-09)
- **Priorità:** P1 (programma; non una feature)
- **Data:** 2026-11-09
- **Ambito:** ChrononMotion3D + Chronon3d, solo capacità engine
- **Owner:** CM3D motion/rig lead + C3D render lead
- **Tipo:** registro di verità e coordinamento; non autorizza nuovi renderer,
  nuove authority o nuove dipendenze

> Evidenza di codice letta sull'albero di lavoro al 2026-11-09. Non è una
> certificazione same-SHA: entrambi i moduli hanno modifiche non committate.
> Questo file non duplica i ticket figli: li collega e dice la verità.

---

## 1. Verdetto secco**A livello di engine, per i sei formati faceless non manca "tutto".** Resta un pezzo nuovo aperto (E2); E1 è chiuso localmente e E3 ha un primo percorso di produzione:

1. **Vulkan consuma ora la texture MR e il materiale per-pixel della mesh
   inline**: fattori per-pixel, emissive e doppio lato sono sulla lane nativa, e
   la MR non viene più persa sui piani con `primitive`. → E1 CLOSED locally
   2026-11-10; certificazione same-SHA in E10.
2. **Non esiste un volto**: niente jaw/occhi/palpebre/brow, niente viseme,
   niente lip-sync. Il morph mixer c'è, ma non è un face rig. → E2 OPEN,
   obbligatorio solo se il personaggio deve recitare con la faccia.
3. **Il comando rigged GLB → RenderPlan esiste**
   (`chrononmotion_character_plan`) e ha passato un run reale da 150 frame a
   1080×1920. Restano due gesti, input malformati, base statica manuale,
   manifest FNV-1a vs SHA256 e same-SHA. → E3 PARTIAL.

Il resto o è già tracciato, o è una scelta di confine, o non è engine.
**Non si può dire "engine completo" finché E1 ed E3 non sono chiusi.**

---

## 2. Gap engine reali

| ID | Gap | Pri | Stato | Scheda | Impatto |
|---|---|---:|---|---|---|
| E1 | Vulkan inline mesh ignora la texture MR; i fattori scalari restano per-vertex | P1 | CLOSED locally 2026-11-10 — pixel gate PASS; same-SHA in E10 | [TICKET-VULKAN-INLINE-MESH-PBR-20261109](../../../Chronon3d/docs/tickets/TICKET-VULKAN-INLINE-MESH-PBR-20261109.md) | Night File/Deep Dive su lane GPU: texture MR divergente in silenzio |
| E2 | Nessun face rig / viseme / lip-sync | P1 se recita; P2 se faceless | OPEN | [TICKET-CM3D-FACIAL-RIG-VISEME-20261109](TICKET-CM3D-FACIAL-RIG-VISEME-20261109.md) | Personaggio con occhi/bocca: recitazione rigida, niente parlato |
| E3 | Comando rigged GLB → RenderPlan atterrato ma incompleto (multi-gesto, input malformati, evidenza del piano) | P1 | PARTIAL — comando atterrato; run reale 150 frame PASS; restano due gesti/input malformati/same-SHA | [TICKET-CM3D-RIGGED-GLB-TO-RENDERPLAN-20261109](TICKET-CM3D-RIGGED-GLB-TO-RENDERPLAN-20261109.md) | Il percorso esiste e produce un manifest, ma non è ancora coperto da regressione multi-gesto né da manifest SHA256/same-SHA |
| E4 | Rotta stills `-o file.png` non disegna mesh inline | P1 | OPEN, già tracciato | [TICKET-RENDER-STACK-HARDENING](../../../Chronon3d/docs/tickets/TICKET-RENDER-STACK-HARDENING-20261106.md) W3 | QA/contact sheet e review non possono usare still del personaggio |
| E5 | Depth per-pixel inline mesh → compositing; alpha+DoF su scena animata | P1 | OPEN, già tracciato | [TICKET-RENDER-STACK-HARDENING](../../../Chronon3d/docs/tickets/TICKET-RENDER-STACK-HARDENING-20261106.md) W3 | Occlusione tra personaggio e layer non certificata |
| E6 | Motion blur per-layer | P2 | OPEN, contract-gated | [TICKET-CM3D-PER-LAYER-BLUR](TICKET-CM3D-PER-LAYER-BLUR-20261106.md) | Non blocca i sei stili; è parity AE |
| E7 | Shadow blur/opacity per-layer | P2 | BLOCKED sul kernel C3D | [TICKET-CM3D-SHADOW-EMISSION](TICKET-CM3D-SHADOW-EMISSION-20261106.md) | Ombre 3D non fini; non blocca il 2D |
| E8 | Gate camera ADR 2–6 | P1 | OPEN, già tracciato | [TICKET-CM3D-REMAINING-WORK](TICKET-CM3D-REMAINING-WORK-20261106.md) §5 | Provider/consumer camera non certificati same-SHA |
| E9 | T15 ingestione GLB diretta in un job | P2 | PARTIAL, già tracciato | [TICKET-CM3D-REMAINING-WORK](TICKET-CM3D-REMAINING-WORK-20261106.md) §6 | Clip pre-renderizzata ≠ ingestione in-job |
| E10 | CI same-SHA C3D/CM3D | P0 programma | PARTIAL | [TICKET-MAIN-TRUTH-STAGE-1-2](../../../Chronon3d/docs/tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md), [TICKET-CM3D-CI-ADAPTER](TICKET-CM3D-CI-ADAPTER-20261106.md) | Nessun PASS stabile su coppia pulita |
| E11 | T05 ombre/luci produzione | P1 | PARTIAL | [TICKET-LIGHTS-SHADOWS-PARTICLES](../../../Chronon3d/docs/tickets/TICKET-LIGHTS-SHADOWS-PARTICLES.md) | Night File/Deep Dive: qualità ombre non certificata |
| E12 | Alpha video GPU-native continuo | P2 | PARTIAL | [TICKET-CM3D-REMAINING-WORK](TICKET-CM3D-REMAINING-WORK-20261106.md) §6 T13 | Solo se serve un personaggio con alpha video; i sei stili usano sfondi opachi |

### Nota di correzione

`TICKET-CM3D-REMAINING-WORK` §4.9 e §7 dicevano che manca il **sampling
software** della texture metallic-roughness inline. Sul tree attuale era
**falso**: `software_mesh_processor.cpp:101-132` la decodifica e la passa a
`render_mesh_filled_lit`. Il gap residuo è **Vulkan** (E1). **Corretto il
2026-11-09** nello stesso ticket padre (§4.9 e §7 ora puntano a E1); non va
ripetuto come se il software fosse cieco.

---

## 3. Cose che non sono un gap engine

| Voce | Verdetto | Perché |
|---|---|---|
| Skinning runtime in C3D | **Confine, non buco** | C3D non evalua ossa; CM3D bakea le posizioni in `inline_mesh.animation`. Per video offline è sufficiente. Sarebbe un buco solo per runtime/interattivo. |
| Authoring SDF/canvas (look Deep Dive) | **Fuori engine** | C3D ha `light.sdf_bevel`, ma non è un modellatore SDF né un runtime canvas/three.js. Il look si bakea offline in mesh/materiali; il renderer sa disegnarli. Non aggiungere un DCC al motore. |
| TTS / Whisper / SFX | **Fuori engine** | Il motore consuma timing e audio; non deve sintetizzare voce o allineare parole. |
| Retargeting completo | **Escluso per scelta** | `RIGGING_FINAL_V1` lo dichiara fuori scope; i 22 gesti sono procedurali e non richiedono un clip pack forestiero. Se serve Mixamo, è un ticket nuovo, non un "gap nascosto". |
| Cloth / hair / soft body / ragdoll | **Escluso** | Non serve ai sei stili; l'avatar è stylized/rigid. |
| Lottie animato / SVG text | **Già tracciato, non centrale** | [TICKET-LOTTIE-ANIMATED](../../../Chronon3d/docs/tickets/TICKET-LOTTIE-ANIMATED.md), [TICKET-SVG-RENDER-PLAN](../../../Chronon3d/docs/tickets/TICKET-SVG-RENDER-PLAN.md). I sei stili non dipendono da Lottie; l'SVG serve per puppet/2.5D e ha un perimetro noto. |
| Drop shadow 2D | **C'è** | `light.drop_shadow` è nell'effect catalog; non è un buco per Paper Atlas. |
| Testo/caption | **C'è, con gate noti** | MTSDF, timed text, caption preset; i residui sono in `TICKET-TEXT-*`, non bloccano i formati faceless. |
| GPU locale `vkCreateInstance -9` | **Ambiente/harness** | [TICKET-REGISTRY-HARNESS-VK-INSTANCES](../../../Chronon3d/docs/tickets/TICKET-REGISTRY-HARNESS-VK-INSTANCES.md); non è una feature mancante. |

---

## 4. Formato per formato

| Formato | Engine basta? | Cosa manca davvero |
|---|---|---|
| **Inside Out 2D** | Sì | Niente engine: arte/kit, shot-list, caption/audio. |
| **Stick Story 2D** | Sì | Niente engine: arte/kit e timing. |
| **Paper Atlas 2D** | Sì | Drop shadow/texture già presenti; manca il kit artistico. |
| **Tall Tale 2.5D** | Sì, con gate noti | Layer/camera/SVG/DoF; E4/E5 sono QA, non bloccano un pilota. |
| **Night File 3D** | Quasi | E1 (PBR Vulkan), E11 (ombre produzione), E3 (pipeline). Facoltativo E2. Il software può fare un pilota. |
| **Deep Dive 3D** | Quasi | E1, E3, E2 se il personaggio recita. Il look SDF/x-ray va bakeato/approx: non è un authoring engine. |

---

## 5. Definizione di "engine pronto"

Si può dire "engine pronto per i faceless shorts" solo quando:

- [x] E1 chiuso localmente il 2026-11-10 con pixel gate software/Vulkan su materiale MR (`Chronon3d/docs/evidence/inline-mesh-pbr-vulkan-20261110`).
- [ ] E3 chiuso con un comando che produce un piano V3 e 150 frame non vuoti.
- [ ] E4/E5 chiusi o formalmente deferiti con owner e impatto.
- [ ] E2 chiuso **oppure** dichiarato non-goal di prodotto per il formato.
- [ ] E10 same-SHA verde sulla coppia congelata.
- [ ] E11 chiuso o formalmente deferito per i formati che usano ombre 3D.

Fino ad allora: "il core c'è, la produzione no". Non è la stessa frase.

---

## 6. Regole di onestà

- Un PASS software non è un PASS Vulkan.
- Un campo serializzato non è una feature: serve un consumatore che cambi i
  pixel e un test di parità.
- Il morph mixer non è un face rig; `applyRigGeometryBake` chiamato da un test
  non è un tool di produzione.
- Una texture MR presente e ignorata in silenzio è peggio di un errore: il
  piano sembra valido e il frame è sbagliato.
- Non si chiude un ticket con un contact sheet al posto del playback, né con
  un albero dirty al posto di una coppia same-SHA.

---

## 7. Evidenze e collegamenti

- [TICKET-VULKAN-INLINE-MESH-PBR-20261109](../../../Chronon3d/docs/tickets/TICKET-VULKAN-INLINE-MESH-PBR-20261109.md)
- [TICKET-CM3D-FACIAL-RIG-VISEME-20261109](TICKET-CM3D-FACIAL-RIG-VISEME-20261109.md)
- [TICKET-CM3D-RIGGED-GLB-TO-RENDERPLAN-20261109](TICKET-CM3D-RIGGED-GLB-TO-RENDERPLAN-20261109.md)
- [TICKET-RENDER-STACK-HARDENING-20261106](../../../Chronon3d/docs/tickets/TICKET-RENDER-STACK-HARDENING-20261106.md)
- [TICKET-CM3D-REMAINING-WORK-20261106](TICKET-CM3D-REMAINING-WORK-20261106.md)
- [TICKET-LIGHTS-SHADOWS-PARTICLES](../../../Chronon3d/docs/tickets/TICKET-LIGHTS-SHADOWS-PARTICLES.md)
- [TICKET-MAIN-TRUTH-STAGE-1-2](../../../Chronon3d/docs/tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md)
- [TICKET-REGISTRY-HARNESS-VK-INSTANCES](../../../Chronon3d/docs/tickets/TICKET-REGISTRY-HARNESS-VK-INSTANCES.md)
- `ChrononMotion3D/docs/RIGGING_FINAL_V1.md`
- `ChrononMotion3D/docs/AUTORIG_GLB.md`
- `ChrononMotion3D/docs/CLIP_LIBRARY.md`
- `Chronon3d/docs/stato/chronon3d.md` (root `docs/stato/chronon3d.md`)
- `Chronon3d/docs/evidence/camera-mesh-yaw-isolation-20261104/`
