# TICKET-CM3D-FACIAL-RIG-VISEME-20261109 — Manca il volto: niente jaw/occhi/palpebre, niente viseme

- **Stato:** PARTIAL (2026-11-11) — engine-side morph-only face rig/viseme landed; asset generation and C3D render open
- **Priorità:** P1 se il personaggio deve recitare con la faccia; P2 se resta faceless
- **Data:** 2026-11-09
- **Ambito:** ChrononMotion3D rig/authoring → RenderPlan V3 → Chronon3d inline mesh
- **Owner:** ChrononMotion3D rig owner
- **Related:** [RIGGING_FINAL_V1](../../docs/RIGGING_FINAL_V1.md), [CLIP_LIBRARY](../../docs/CLIP_LIBRARY.md), [AUTORIG_GLB](../../docs/AUTORIG_GLB.md)
- **Tipo:** engine feature opzionale, add-on al rig 20-bone; nessuna dipendenza nuova

> Evidenza di codice letta sull'albero di lavoro al 2026-11-09. Non è una
> certificazione same-SHA: CM3D ha modifiche non committate.

---

## 1. Problema

Il rig core sa fare morfing e skinning, ma **non esiste un volto**.

### Cosa c'è

- `RigMorphTarget`, `morphTracks` e `MorphMixer` esistono e sono certificati:
  pesi 0/0.5/1, validazione topologica, morph multipli deterministici
  (`ChrononMotion3D/docs/RIGGING_FINAL_V1.md` §4;
  `include/chrononmotion/motion/RigBaking.hpp:30-56`).
- Il glTF motion import porta morph target e canali `weights`; il bake può
  applicarli alla geometria.
- Il rig 20-bone dell'autorig è un umanoide completo per il corpo: spine,
  chest, neck, head, head_top, braccia e gambe
  (`tools/autorig/autorig_glb.py:370-394`).
- La clip library ha 22 gesti di corpo/testa con overshoot e follow-through
  (`docs/CLIP_LIBRARY.md`).

### Cosa non c'è

- **Nessun osso facciale**: niente jaw, eye.L/R, upper/lower lid, brow.L/R,
  mouth corner. L'autorig si ferma a `head`/`head_top`.
- **Nessun face rig**: non c'è una gerarchia facciale, una pose library, né
  controlli di blink/gaze/look-at.
- **Nessun viseme**: non c'è un set di fonemi/visemi, una mappatura
  fonema→morph/jaw, né un formato di timing per la parlata.
- **Nessun lip-sync**: il motore non consuma un file di timing parole/fonemi e
  non produce curve facciali.
- I 22 gesti muovono testa e corpo, non la faccia. Gli spike con "facial core
  rigido" (`render/spike/green_screen_actor_rig.cpp:200`) sono harness di
  look-dev, non una feature del core.

**Conseguenza brutale:** se il GLB sorgente non arriva già con blendshape
facciali e se qualcuno non scrive i pesi a mano, il personaggio non può
blinkare, guardare, aprire la bocca o parlare. Il morph mixer non è un face
rig: è solo il meccanismo che potrebbe consumarne uno.

---

## 2. Cosa manca esattamente

### 2.1 Face rig opzionale

- [x] Definire un contratto `FaceRig` add-on, separato dal rig 20-bone del
      corpo. Il corpo deve restare valido e certificato anche senza face rig.
      Contratto: `docs/FACE_RIG_VISEME_V1.md`.
- [ ] Coprire come minimo: jaw open/close, eye L/R, upper/lower eyelid,
      brow L/R, mouth corner L/R. Se il personaggio è stylized, i controlli
      possono essere 8–12, non 52 blendshape FACS.
- [x] Scegliere una rappresentazione unica per il volto:
      **bone** (jaw/eyes/lids/brows) + **morph** (espressioni), oppure solo
      morph. Non duplicare la stessa autorità in due canali.
      Scelta: solo morph, indicizzati da `FaceRigMap`.
- [ ] Authoring: pose/control API, default neutro, range validati, fail-closed
      su morph mancanti o topologia incompatibile. Mapping JSON e validazione
      fatti; manca una pose/control API e un asset con morph.
- [x] Bake deterministico: il volto deve finire in `RigFrameGeometry`
      (posizioni/normali) e quindi in `inline_mesh.animation`; C3D non deve
      acquisire un runtime morph. Verificato dal test di bake; manca la prova
      su un asset con volto.

### 2.2 Viseme / lip-sync

- [x] Definire un set chiuso di 12–15 visemi (per esempio `AA, E, I, O, U, M,
      F, L, WQ, etc.`) con la mappatura ai controlli facciali. 13 valori
      inclusi `Neutral`, 12 fonemi non neutri.
- [x] Definire un formato di input **engine-side** per il timing:
      `{frame, viseme, weight}` oppure `{start_frame, end_frame, viseme}`.
      Il TTS/Whisper resta fuori: il motore consuma timing, non lo produce.
      Formato: `FacePerformance`/`--face-cues`.
- [x] Consentire co-articolazione/blend tra visemi adiacenti, con default
      documentato e nessun pop a frame 0.
- [x] Validare: visema sconosciuto, frame non monotono, peso fuori range,
      topologia morph diversa → errore tipizzato.
- [x] Non legare il formato a un motore TTS o a un modello vocale specifico.

### 2.3 Blink, sguardo, recitazione

- [x] Blink procedurale: curva apri/chiudi, durata e offset configurabili;
      palpebra superiore e inferiore non devono compenetrare l'occhio.
      Implementato come pulse morph; la compenetrazione resta da verificare
      sull'asset.
- [x] Gaze/look-at: eye L/R verso un target, con limiti di rotazione e
      recovery al neutro; opzionale micro-saccade deterministico.
      Implementato come morph left/right con `horizontal` in [-1,1]; il
      look-at 3D e le micro-saccadi restano fuori.
- [ ] Pose library minima: neutral, blink, look left/right/up/down, brow
      raise, mouth open, smile/frown. Non serve un editor FACS completo.

### 2.4 Test e resa

- [ ] Canary volto: un personaggio con occhi/palpebre/bocca, frame 0..149,
      blink a frame 20 e 80, sguardo a frame 40, visema a frame 60.
- [x] Test di bake: due run identiche → stesse posizioni/normali/hash.
- [x] Test di topologia: morph target con vertex count diverso → rifiuto.
      Il contratto rifiuta indici fuori range e morph assenti; il mismatch di
      vertex count resta responsabilità del reader.
- [ ] Render C3D software della sequenza: frame non vuoti, volto visibile,
      nessun pop tra frame. La parità Vulkan è separata (dipende dal materiale,
      non dal face rig).
- [x] Documentare i limiti: no speech recognition, no TTS, no FACS completo,
      no rughe/simulazione pelle.

---

## 3. Criteri di accettazione

- [ ] Face rig opzionale disponibile su un rig 20-bone; la suite body/canary
      esistente resta verde senza face rig.
- [x] Un input di timing viseme produce una sequenza facciale continua e
      deterministica, con almeno 12 visemi e blend. Verificato in unit test.
- [x] Blink e gaze funzionano senza compenetrazioni e senza pop.
      Verificato in unit test sui morph; la compenetrazione geometrica non è
      certificata su un asset facciale.
- [ ] Il bake produce `inline_mesh.animation` con il volto deformato; C3D
      software renderizza 150/150 frame non vuoti.
- [x] Test di rifiuto per morph/topologia/visema invalidi; nessun default che
      armi una feature non supportata.
- [ ] Evidenza con piano, asset, comandi, hash e frame campione.

**Non-goals:** speech-to-text, TTS, allineamento forzato, FACS 52, simulazione
pelle/rughe, hair/cloth, retargeting facciale da video, un editor GUI.

## 5. Chiusura engine-side 2026-11-11

Implementati `include/chrononmotion/motion/FaceRig.hpp`,
`src/chrononmotion/motion/FaceRig.cpp` e `tests/motion_face_rig.cpp`:

- contratto morph-only con 13 valori (12 visemi non neutri), mappa per indice
  morph e blink/gaze separati;
- cue con co-articolazione lineare che termina sul frame del cue,
  anticipazione del primo visema e hold finale;
- pulse blink/gaze deterministici, clamp `[0,1]` e rifiuto fail-closed di
  geometria/morph/indici/cue/fps invalidi;
- scrittura di `morphTracks` densi, poi bake esistente in
  `RigFrameGeometry`; nessun runtime morph in C3D;
- integrazione CLI `--face-map`/`--face-cues` (single clip) e manifest con hash
  input, conteggi, morf0 e signature.

Verifica: `ctest -R chrononmotion_face_rig` → **28 check, 0 failure**; il test
CLI rifiuta una mappa su un rig senza morph. Non sono chiusi: asset con volto,
pose/control API, render C3D 150/150 e review visiva. Il fixture low-poly non
ha morph target, quindi il successo CLI su un volto reale non è provato.

---

## 4. Impatto

| Caso | Priorità | Motivo |
|---|---:|---|
| Faceless narration con occhi chiusi/sagoma | P2 | Può restare senza volto; non blocca. |
| Night File / Deep Dive con personaggio che guarda e parla | P1 | Senza volto, la recitazione è rigida e il lip-sync è impossibile. |
| 2D puppet / SVG | P2 | Il volto può essere un rig separato o un morph del puppet; non è lo stesso contratto. |

**Regola:** non dire "il motore sa animare un personaggio" e poi scoprire che
il volto è un asset esterno non integrato. Se il volto serve, è un ticket
engine; se non serve, va dichiarato non-goal, non nascosto.
