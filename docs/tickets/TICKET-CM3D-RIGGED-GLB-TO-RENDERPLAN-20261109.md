# TICKET-CM3D-RIGGED-GLB-TO-RENDERPLAN-20261109 — Il percorso rigged GLB → inline mesh è solo una API di test

- **Stato:** PARTIAL (2026-11-11) — pipeline CLI chiusa localmente; restano evidenza 1080p multi-gesto, Vulkan e same-SHA
- **Priorità:** P1
- **Data:** 2026-11-09
- **Ambito:** ChrononMotion3D tools/lowering → RenderPlan V3 → Chronon3d
- **Owner:** ChrononMotion3D pipeline owner
- **Related:** [AUTORIG_GLB](../../docs/AUTORIG_GLB.md), [CLIP_LIBRARY](../../docs/CLIP_LIBRARY.md), [TICKET-CM3D-REMAINING-WORK-20261106](TICKET-CM3D-REMAINING-WORK-20261106.md) §4/§6
- **Tipo:** engine integration + CLI di produzione; nessun nuovo evaluator, nessuna dipendenza nuova

> Evidenza di codice letta sull'albero di lavoro al 2026-11-09. Non è una
> certificazione same-SHA: CM3D ha modifiche non committate.

---

## 1. Problema

La capacità esiste, il percorso di produzione no.

### Cosa c'è

- `applyRigGeometryFrame`, `appendRigGeometrySample` e
  `applyRigGeometryBake` sanno trasformare un bake di `RigProgram` in
  `inline_mesh.animation` V3, preservando UV/materiale della mesh base
  (`include/chrononmotion/motion/RenderPlanLowering.hpp:138-160`).
- `RigProgramEvaluator::bake()` produce frame con pose e geometria,
  con signature deterministica (`include/chrononmotion/motion/RigBaking.hpp:61-91`).
- `RigProgram` supporta skin, morph, IK, constraint, pose mixer, deformers,
  secondary motion.
- `autorig_glb.py` sa riggare un GLB statico e cuocere frame statici:
  `python3 tools/autorig/autorig_glb.py bake rigged.glb -o frames/ --clip wave --frames 48`
  (`docs/AUTORIG_GLB.md`).
- C3D sa renderizzare `inline_mesh.animation` e la fixture camera/mesh
  150 frame è passata in software e Vulkan (evidenza locale
  `Chronon3d/docs/evidence/camera-mesh-yaw-isolation-20261104/`).

### Cosa non c'era (stato pre-implementazione)

> Nota 2026-11-09: questa sezione fotografa il problema prima
> dell'implementazione. Il comando ora esiste; vedi §12 e l'evidenza.

- **Nessun comando di produzione** che prenda un GLB riggato + una clip o una
  sequenza di gesti + camera + materiali e produca un `.plan.json` V3 con
  `inline_mesh.animation`. `applyRigGeometryBake` è chiamato solo nei test
  (`tests/motion_authoring_renderplan.cpp:1337-1556`); i tool `plan_*`
  costruiscono le scene a mano e non accettano un personaggio riggato.
- **Nessun contratto per il personaggio testurizzato**: il loader statico C3D
  rifiuta un GLB con skin, il motion reader CM3D rifiuta i materiali
  (`docs/AUTORIG_GLB.md`, "Why it exists"). Per avere skin + UV + materiale
  bisogna combinare la mesh statica base con il motion-only, ma il
  matching topologico e la conservazione del materiale non sono impacchettati.
- **Nessun manifest** con hash di input, output, clip, camera, range frame e
  fingerprint del piano.
- **Nessun gate end-to-end** che parta da un GLB reale, produca il piano e
  renderizzi 150 frame 1080×1920 senza frame vuoti.
- `autorig_glb.py bake` produce GLB statici per il renderer OpenGL
  (`chrononmotion_render_glb`), non un RenderPlan C3D. Sono due percorsi
  diversi; il ponte tra loro non è un comando.

**Conseguenza brutale (all'epoca):** ogni shot con un personaggio era un test
C++ o un tool bespoke. Non è riproducibile, non ha un manifest, non ha un errore
tipizzato e non si può mettere in una pipeline di produzione senza scrivere
codice ogni volta. La feature "c'è" nelle API; il prodotto "non c'è".

---

## 2. Cosa manca esattamente

### 2.1 Comando canonico

- [x] Aggiungere un tool/CLI (nome indicativo `chrononmotion_character_plan`)
      con input espliciti:
      `--rig <rigged.glb>`, `--motion <motion-only.glb|json>`, `--clip <name>`
      oppure `--sequence "Idle,G_Point,Idle,G_Shrug"`, `--fps`, `--frames`,
      `--camera` (posizione/target/fov oppure preset), `--out <plan.json>`,
      `--assets-root`.
- [x] Usare le authority esistenti: `loadGltfMotion`, `RigProgram`,
      `RigProgramEvaluator`, `applyRigGeometryBake`, `CameraRig`,
      `lowerToRenderPlan`. Nessun secondo evaluator, nessun secondo formato.
- [x] Supportare il caso **static base + motion-only**: dal 2026-11-11 la mesh statica è derivata automaticamente dal GLB riggato; `--mesh` è opzionale e accetta un GLB normale. La mesh statica fornisce
      UV/materiali/base color; il motion-only fornisce skin/clip; il matching
      topologico deve fallire chiuso se vertex count/indices non coincidono.
- [x] Supportare almeno due gesti concatenati con crossfade, non solo una clip
      singola (`--sequence A,B --crossfade <secondi>`).
- [x] Camera: usare il resolved frame plan o un provider camera esistente; non
      duplicare projection/DoF/shutter.

### 2.2 Output e validazione

- [x] Il piano deve contenere:
      `inline_mesh` con UV/indices/materiale, `inline_mesh.animation` con
      1..4096 campioni, camera risolta, canvas/fps/durata, output format.
- [x] Validare prima di scrivere: topologia, range frame, campioni monotoni,
      NaN/Inf, bounds, budget vertici/indici/byte, materiali mancanti.
- [x] Scrivere un manifest con SHA256 degli input, fingerprint del piano,
      clip/gesti, camera, frame range e versione schema.
- [x] Su errore: nessun `.plan.json` parziale, nessun fallback silenzioso.

### 2.3 Gate end-to-end

- [ ] Fixture con un GLB riggato reale (20-bone o skeleton importato) e due
      gesti; 1080×1920, 30 fps, 150 frame. Run locale 2026-11-11: 80 frame,
      320×480, 10 fps, `Idle2`+`Idle` con crossfade 0,25 s; la fixture 1080p
      resta aperta.
- [x] Render C3D software: 150/150 frame non vuoti, bounds in movimento,
      hash distinto per frame, nessun crash.
- [ ] Render C3D Vulkan strict-native: eseguibile solo dopo il ticket
      [TICKET-VULKAN-INLINE-MESH-PBR-20261109](../../../Chronon3d/docs/tickets/TICKET-VULKAN-INLINE-MESH-PBR-20261109.md);
      fino ad allora la lane GPU non può rivendicare la parità materiale.
- [x] Test CLI: input mancante, clip sconosciuta, topologia diversa, range
      fuori budget, camera invalida → errore tipizzato e exit non-zero.
- [x] Determinismo: due run con gli stessi input → stesso piano, stesso
      fingerprint, stesso manifest.

### 2.4 Documentazione

- [x] Aggiornare `docs/AUTORIG_GLB.md` con il percorso "rigged GLB → C3D
      RenderPlan", separato dal percorso OpenGL per frame.
- [ ] Aggiornare `TICKET-CM3D-REMAINING-WORK-20261106.md` §6/T15: non è
      "ingestione GLB diretta in un job" finché il producer non è un comando.
- [ ] Non dichiarare T15 chiuso con un test che chiama `applyRigGeometryBake`
      a mano.

---

## 3. Criteri di accettazione

- [x] Un solo comando produce un piano V3 valido da un GLB riggato + due gesti,
      senza modifiche C++ per shot (run locale 2026-11-11).
- [x] Il piano conserva UV/materiale/base color e contiene
      `inline_mesh.animation` con 150 campioni.
- [x] C3D software renderizza 150/150 frame non vuoti; il manifest è stabile
      tra due run.
- [x] Input malformati falliscono chiusi; nessun output parziale.
- [ ] Test CLI e lowering verdi; nessun test skippato. CLI verde il
      2026-11-11; la suite lowering non è stata rieseguita in questa sessione.
- [ ] Evidenza con piano, manifest, comandi, hash e frame campione.

**Non-goals:** GUI/DCC, retargeting di clip forestiere, cloth/hair, skinning
runtime in C3D, un nuovo renderer, un nuovo formato di scena. Il target è
un comando ripetibile sopra le API esistenti.

---

## 4. Impatto

| Formato | Impatto |
|---|---|
| 2D / 2.5D | Basso: il personaggio può restare SVG/puppet o mesh statica. |
| Night File / Deep Dive | Alto: senza questo tool, ogni personaggio 3D è un caso speciale. |
| Pipeline di produzione | Bloccante: non c'è manifest, non c'è regressione, non c'è errore tipizzato. |

**Regola:** una capacità che si può invocare solo da un test non è una feature
di produzione. Finché il comando non esiste, il motore può dire "so farlo" ma
non "lo faccio in modo ripetibile".

---

## 12. Implementazione (2026-11-09)

Comando di produzione atterrato: `chrononmotion_character_plan`
(`tools/rigged_glb_to_renderplan.cpp`, target CMake omonimo).

Input: `--rig` (GLB/glTF motion-only), `--mesh` opzionale (UV/materiale),
`--clip`, `--frames`, `--fps`, canvas, camera, materiale, texture.
Output: `.plan.json` V3 e manifest con digest FNV-1a degli input e del piano,
clip, frame range, geometry id, conteggi e `bake_signature`.

Usa solo authority esistenti: `loadGltfMotion`, `RigProgram`,
`RigProgramEvaluator::bake`, `applyRigGeometryBake`,
`renderPlanEmissionProblem`, `renderPlanToJson`. Legge i GLB estraendo il chunk
JSON e inlineando il BIN come data URI; normalizza gli attributi motion-only
(POSITION/NORMAL/JOINTS_n/WEIGHTS_n) e con `--strip-rig-materials` rimuove
materiali/texture/immagini dal documento rig.

Verifica end-to-end (fixture riggata a 3 vertici, 1 osso, clip 1 s):
- piano V3 con 12 campioni di animazione ai frame 0–11 e geometria in movimento;
- manifest con `bake_signature=14002292298252508981`;
- render software C3D: **exit 0**, MP4 640×360, 12 frame, **1682 pixel non
  neri** sul frame 6 (nessun frame vuoto).

### 12.1 Personaggio reale: 150 frame 1080×1920 (2026-11-09)

Evidenza durevole: [character-plan-lowpoly-20261109](../evidence/character-plan-lowpoly-20261109/README.md).

Input: `render/spike/assets/scene03/low-poly_man.glb` (5.088 vertici, 7.632
indici, 53 ossa), clip `Idle2` a 30 fps. La mesh statica `--mesh` è una copia
dello stesso GLB privata di skin/animazioni/materiali
(`.tmp/character/lowpoly_base.glb`); la texture 32×32 è stata estratta a parte.
Il tool non deriva ancora questa copia statica.

```bash
chrononmotion_character_plan \
  --rig ChrononMotion3D/render/spike/assets/scene03/low-poly_man.glb \
  --mesh .tmp/character/lowpoly_base.glb --strip-rig-materials \
  --clip Idle2 --frames 150 --fps 30 --width 1080 --height 1920 \
  --texture .tmp/character/lowpoly_diffuse.png \
  --base-color 1,1,1,1 --metallic 0 --roughness 0.96 \
  --camera 0,0.858,-2.2 --rotation 0,0,0 --fov 50 \
  --out .tmp/character/lowpoly_idle.plan.json \
  --manifest .tmp/character/lowpoly_idle.manifest.json
```

Risultato:
- piano V3 da 180.637.852 byte, 150 campioni, geometria animata;
  `bake_signature=15382456531100992501`, SHA-256
  `ed4df0155e4c197ffa52d5b7c7f05d2d207bc7ed354e2d5b11c5f7eb72a9d1b1`;
- manifest da 824 byte, SHA-256
  `95c26dedcc5b1ac0b41d0b409837a3b111d333ae1a32b23f089151a76044adb7`;
  piano e manifest **byte-identici** su due generazioni consecutive;
- render software C3D **exit 0**: MP4 H.264 1080×1920, 30 fps, 5,0 s, 150
  frame, 335.725 byte, SHA-256
  `b08bc719e49bbaa83484671d97f20ae4fa80ff218c5020073f0864c1ca6f69da`;
- **150/150 frame non vuoti**, 293.049–295.813 pixel non neri per frame a
  piena risoluzione (zero frame vuoti);
- bounds animati: x −0,256…0,258, y −0,009…1,716, z −0,129…0,112;
- movimento presente ma trattato come idle: MAD 0→37 = 0,2582, 0→75 = 0,1772,
  75→149 = 0,1742;
- 71,50 s, picco RSS 1.860.888 KB (~1,86 GB), backend software.

Bug reali esposti dall'asset e corretti nel tool:
- il nodo root `samplers` sopravviveva a `--strip-rig-materials`;
- i nodi con matrice 4×4 non erano leggibili dal percorso motion: ora sono
  decomposti in TRS, con fail-closed su shear/mirror;
- `asset.extras` (Sketchfab) non veniva rimosso e rompeva il loader statico.

Restano aperti:
- `--mesh` deve essere già statica: un GLB riggato usato come base richiede
  una normalizzazione manuale che il tool non produce;
- run con due gesti e test su input malformati;
- matching topologico automatico mesh statica ↔ rig (oggi è un errore esplicito
  se il conteggio vertici non coincide);
- certificazione same-SHA;
- il manifest usa digest FNV-1a, non gli SHA256 richiesti dai criteri di
  accettazione: il formato va corretto o il criterio va allineato
  esplicitamente;
- l'evidenza non include il piano da 180 MB (hash, manifest, comandi e frame
  sì): il criterio "evidenza con piano" resta aperto.

Nota CLI: sulla route `--plan` il flag `--frames 0-2` è stato ignorato e ha
reso i 150 campioni del piano. Per un range esplicito usare
`--start-frame`/`--end-frame`.

### 12.2 Chiusura locale 2026-11-11

Modifiche atterrate in `tools/rigged_glb_to_renderplan.cpp`:

- **Base statica automatica**: se `--mesh` è assente, il tool deriva la mesh
  statica dallo stesso GLB riggato, in memoria (rimuove skin, morph, animazioni
  e materiali; conserva POSITION/NORMAL/TEXCOORD_0 e gli indici). `--mesh`
  resta opzionale e ora accetta anche un GLB riggato/testurizzato, non più una
  copia già ripulita a mano.
- **Matching topologico fail-closed**: la primitiva base viene scelta per
  vertex count e confronto esatto degli indici contro il binding del rig;
  ambiguità o mismatch falliscono prima di scrivere.
- **Due gesti con crossfade**: `--sequence A,B` e `--crossfade <secondi>`.
  La durata viene dalle clip, i pesi sono lineari e normalizzati, la geometria
  deformata viene fusa per vertice (LBS è lineare nelle posizioni).
- **Manifest v2 con SHA-256** di rig, mesh opzionale e piano, mantenendo
  FNV-1a come digest compatto. Il piano resta V3.
- **Validazione CLI**: sequenza di un solo clip, `--frames` con sequenza,
  `--clip`+`--sequence`, crossfade troppo lungo, FOV/camera non finiti,
  mesh con topologia diversa, clip sconosciuta e rig illeggibile falliscono
  con exit non-zero e senza piano parziale.

Evidenza locale (non same-SHA):

| Voce | Valore |
|---|---|
| Input | `render/spike/assets/scene03/low-poly_man.glb`, 5.088 vertici, 7.632 indici, SHA-256 `b635aa1ab2267e85afbd4a666450465547fdad27a0d1bab17fea4b6c206f5d50` |
| Clip | `Idle2` (4,958 s) + `Idle` (3,292 s), crossfade 0,25 s |
| Output | 80 frame, 320×480, 10 fps, 97.251.753 byte, SHA-256 `fca83e6ece14eafcb262b5434ede381964dc91ea8cafdcdfdd598e0a1c98c1be` |
| Manifest | v2, SHA-256 `dcdaaaf9f0e8d189867a0bafdbe8235453082788be2d7e985a8c81413509ff10` |
| Base | La derivazione automatica e `--mesh` sullo stesso GLB producono lo stesso piano: SHA-256 `a527761fad2451636e9ff330c81b449f4f50a2b9734ea58991d4b7483dda0b9e` |
| Determinismo | Due run della sequenza → stesso SHA-256 `10f0af2691ff01559b638a2b46ff7b5d6f4e2ac3c55626b2bb7b0902d26234e2` |
| Texture | PNG 32×32 estratto dallo stesso GLB; piano con `asset=lowpoly_diffuse.png` |

Test CLI: `tests/check_character_plan_cli.cmake` (registrato come
`chrononmotion_character_plan_cli` quando i test sono attivi) e la stessa
matrice eseguita manualmente sul binario: tutti i casi falliscono chiusi.

Restano aperti, e non vanno dichiarati chiusi:

- run 1080×1920 a 30 fps con due gesti e 150 frame (la sequenza dura 8 s →
  240 frame a 30 fps);
- render C3D Vulkan: il CLI locale non parte (`libchronon3d_c.so.2: file too
  short`) e `vulkaninfo` segnala un ICD non disponibile;
- certificazione same-SHA su coppia pulita;
- evidenza con il piano completo (97 MB locali, non versionati);
- la texture resta un sidecar: il tool non estrae ancora l'immagine dal GLB.
