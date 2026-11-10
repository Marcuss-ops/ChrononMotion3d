# Ordine di compositing — contratto v1

**Stato:** implementato (2026-11-10).
**Codice:** `include/chrononmotion/motion/Compositing.hpp`, `src/chrononmotion/motion/Compositing.cpp`.
**Contratto macchina:** campo opzionale `composite_order` di
`contracts/chrononmotion.resolved-frame-plan.v1.json`.
**Test:** `chrononmotion_compositing`.

## Perché esiste

ChrononMotion3D possiede spazio e tempo; non possiede i pixel. Ma l'ordine in cui
i layer visibili vengono dipinti è una decisione di **spazio**, non di renderer:
se dipende dall'ordine di inserimento, la stessa scena può mostrare il testo
davanti all'oggetto in un tool e dietro in un altro.

In After Effects lo stesso problema ha due risposte:

- un layer 2D sta nella pila del Timeline, e la pila è l'ordine;
- un layer 3D viene ordinato per profondità rispetto alla camera.

Questo contratto rende le due risposte esplicite e deterministiche, senza
aggiungere un renderer e senza duplicare la logica nel consumer.

## Le due modalità

| `CompositeMode` | regola | quando usarla |
|---|---|---|
| `Stack` (default) | ordine di authoring, dal fondo al fronte: il layer aggiunto dopo sta davanti | composizioni 2D/2.5D, e compatibilità con ogni piano già emesso |
| `Depth` | i layer drawable sono ordinati per profondità camera-space dell'anchor, dal più lontano al più vicino | composizioni 3D: oggetto davanti, testo dietro, anche se il testo è stato aggiunto dopo |

```cpp
using namespace chrononmotion::motion;

MotionScene scene("hero", 30.f);

Layer& object = scene.addMesh(1, "hero", attachment);   // z = 0
Layer& title  = scene.addContent(2, "title", ContentRef::text("title", {800.f, 200.f}));
title.transform.position.z = -4.f;                      // dietro l'oggetto

scene.setCompositeMode(CompositeMode::Depth);
```

Con la camera a `+Z` che guarda `-Z`, il testo risolve una profondità maggiore e
viene dipinto per primo; l'oggetto lo copre. Nessuna modifica all'ordine dei
layer, nessun `moveLayer`.

## Cosa risolve

`resolveCompositeStack(scene, pose)` legge lo stato **già valutato** della scena e
restituisce `CompositeStack::backToFront`, una lista di `CompositeDraw`:

| campo | significato |
|---|---|
| `layerId` | identità del layer |
| `stackIndex` | posizione nella lista autoriale, usata come tie-break |
| `viewDepth` | profondità lungo l'asse ottico; positiva davanti alla camera |

Regole:

1. i controller Null non disegnano e non entrano nello stack;
2. i layer fuori dalla loro lifetime non entrano nello stack;
3. in `Depth` si ordina per `viewDepth` decrescente (il più lontano per primo);
4. a parità di profondità vince l'ordine autoriale (`stackIndex` crescente), poi
   l'id: il risultato non dipende dalla stabilità dell'algoritmo di sort;
5. un layer a o dietro la camera (`viewDepth <= 1e-4`) viene dipinto per primo,
   quindi coperto da tutto il resto;
6. un anchor o una posa non finiti sono un errore, non un layer saltato.

La profondità è quella dell'**anchor** del layer: è la stessa granularità che AE
usa per ordinare i layer 3D. Due quad che si intersecano restano un problema
per-pixel del renderer.

## Nel frame plan

Quando la scena è in `Depth`, `resolveFramePlan()` aggiunge:

```json
"composite_order": [
  [2, 1],
  [2, 1],
  [1, 2]
]
```

Una voce per frame, dal fondo al fronte, con esattamente i layer drawable
visibili a quell'istante. Il verifier rifiuta (`composite_order`) una lista con
un id sconosciuto, duplicato, un layer Null, un layer non visibile, una voce
mancante o un frame mancante. Il digest copre anche questo campo.

In `Stack` il campo è **assente**: `layers[]` è già l'ordine, e il documento
resta byte-identico a quello emesso prima di questo contratto. Per questo la
modalità è opt-in e il golden del frame plan non cambia.

## Cosa non è

- non è un renderer: non rasterizza testo, mesh o materiali;
- non è occlusione per-pixel: due superfici che si attraversano richiedono la
  depth del backend (gate 3 dell'ADR camera, fuori da CM3D);
- non è uno z-buffer e non sostituisce il compositing alpha/depth del consumer;
- non è un secondo sistema di layer: usa `Layer`, `MotionScene` e
  `CameraPose` esistenti.

## Fail-closed

`resolveCompositeStack` rifiuta con `std::invalid_argument`:

- una scena mai valutata (`hasEvaluated() == false`), perché leggerebbe la posa
  del frame precedente;
- una posa camera non finita;
- una matrice mondo con traslazione non finita;
- una profondità non finita.

`MotionScene::setCompositeMode` accetta solo i due valori del contratto.

## Evidenza locale

`chrononmotion_compositing` copre:

- stack mode: ordine autoriale e `stackIndex`;
- depth mode: oggetto aggiunto prima, testo dietro, ordine `[testo, oggetto]`;
- attraversamento: frame 0 `[oggetto, testo]`, frame 24 `[testo, oggetto]`;
- layer dietro la camera dipinto per primo;
- lifetime: il layer nascosto esce dallo stack;
- parità di profondità: vince l'ordine autoriale;
- scena non valutata, anchor non finito, modalità sconosciuta: rifiuto;
- round-trip del file scena con `compositeMode`;
- frame plan: una voce per frame, verifica, digest, assenza del campo in stack
  mode, e cinque rifiuti `composite_order`.
