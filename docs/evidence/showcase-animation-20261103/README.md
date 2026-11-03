# Showcase animato — 2026-11-03

Tre clip MP4 960×540, **240 frame a 60 fps** (4,000 s), prodotte dal binario
`Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
(backend `software`, `--video-sink raw`). Ogni clip dà un'animazione a un ticket
che finora aveva solo prove statiche.

| File | Ticket | Cosa si vede | Byte |
|---|---|---|---|
| `out/01-oggetto-sotto-luce.mp4` | `TICKET-HDRI-ENV-V1` | tre solidi (cubo, icosaedro, gemma) sotto `environment_light` + `lighting_rig`, probe che ruota 358°, dolly di camera | 266 328 |
| `out/02-materiali.mp4` | look-dev materiali (metallo/plastica/gemma) | tre solidi che ruotano su se stessi con `environment_light` 0→240° e dolly di camera | 336 782 |
| `out/03-gltf-animato.mp4` | `TICKET-GLTF-ANIMATED-V1` | la fixture glTF animata (`ticket-roundtrip-scaled-20261029/gltf`) riprodotta in ping-pong per 240 frame, con spin in piano e dolly di camera | 202 093 |

Verificato con `ffprobe`: `h264`, 960×540, `r_frame_rate=60/1`,
`nb_frames=240`, `duration=4.000000` su tutte e tre.

SHA-256:

```
1052b16c9b6bceeb59b9ca33802afaca28206ab39d25a24a0108c0f958b27820  01-oggetto-sotto-luce.mp4
e4acae253aafa68ff68d9de236f9acf5cab593923656e383a07e190ab549127c  02-materiali.mp4
9ef651b1f5b44005aae9d9fcc9a7bb218b4f8c4c78e629ee91c9630eece6d113  03-gltf-animato.mp4
```

## Dove sono

Caricate su Drive con `RenderingGen/bin/drive-upload` (verifica SHA-256 lato
uploader) nella sottocartella `ChrononMotion3D_Showcase_60fps_20261103`:

| File | Drive id | Byte |
|---|---|---|
| `01-oggetto-sotto-luce.mp4` | `1WErMHEpT8bzfZsr3IJGHZJgvbI5HniLJ` | 266 328 |
| `02-materiali.mp4` | `1NQmjzSi7hLfJYs7V1gkedC6s0x8G92Z0` | 336 782 |
| `03-gltf-animato.mp4` | `14zxzDJqWhwyMQ9Xwmm6crNOgUQDlRwK6` | 202 093 |

## Perche' 60 fps

Il percorso software costa ~0,21 s per frame a 1056×588: le tre clip a 240
frame si rendono in **2 min 30 s** totali (~50 s l'una), quindi raddoppiare il
campionamento rispetto alla prima versione (30 fps / 120 frame) e' pagabile. La
durata resta 4 s: raddoppia il *sampling*, non il movimento, che e' derivato dal
tempo (secondi) e non dal numero di frame:

- `bob_track` fa un periodo ogni 2 s, `sway_track` usa frazioni di clip;
- la camera campiona ~12 posizioni per clip (`dolly(..., step=frames//12)`);
- la fixture glTF e' bakeata a 30 campioni/s, quindi il clip 03 la ricampiona a
  tempo reale **interpolando** i due campioni vicini: senza interpolazione meta'
  dei frame usciva duplicata (misurato: 18-19 coppie identiche su 239 prima
  della correzione, **0** dopo, su tutte e tre le clip).

## Riproduzione

```bash
python3 make_showcase.py    # riscrive assets/ e plans/
./render.sh                 # render + encode in out/
```

Overridabili a runtime: `FPS=60 FRAMES=240 ./render.sh`.

Il render passa da `--video-sink raw` perche' questo build collega un avcodec
senza encoder (`avcodec_find_encoder_by_name('libx264')` fallisce): l'engine
scrive RGBA puro, ffmpeg fa solo l'encode. Il canvas autore e' 1056×588 con
overscan, i frame consegnati sono il ritaglio 960×540 (`crop=960:540:48:24`).

## Cosa si muove davvero in questo build

Verificato frame per frame sui raw prodotti (non dedotto dal codice):

- **Funziona** `camera_samples` (posizione della camera per-frame),
  `inline_mesh.animation` (posizioni bakeate per vertice) e
  `layers[].animation.tracks` (position/rotation/opacity, keyframe o dense):
  i solidi del clip 01 si muovono su track propri, in aggiunta al dolly e al
  probe.
- Le immagini `type: "image"` sono piani 2.5D (`position` = angolo alto-sinistro
  in pixel) e non possono portare `inline_mesh`; le mesh sono 3D con origine al
  centro canvas e `+Y` in alto.
- Le mesh inline non campionano texture (`render-plan.inline-mesh v1`): il
  colore viene da `color` + `material`, quindi la risposta della luce si vede
  sulle facce, non su una mappa.
- Il primo frame consegnato e' pulito: `mean |dG/dx|` 0,184 sul frame 0 contro
  0,183 sul frame 1 (clip 03), 0,158/0,205 (clip 01). Nessun frame viene
  scartato o tagliato.

## Limiti osservati nel render

1. **La camera non puo' ruotare su una mesh con animazione bakeata.** Basta che
   `camera_samples` cambi la *rotazione* perche' la mesh venga disegnata sempre
   piu' piccola fino a collassare in un punto e poi ricrescere. Riprodotto con
   un yaw totale di **0,02°** (identico al caso 1,2°): l'ampiezza non conta,
   conta che la rotazione cambi. Isolamento a frame fissi sul clip 03:

   | variante di `camera_samples` | clip 03 |
   |---|---|
   | campione singolo / camera ferma | corretto |
   | 24 campioni, posizione e rotazione costanti | corretto |
   | 24 campioni, solo posizione animata | corretto |
   | 24 campioni, rotazione animata (anche 0,02°) | collasso a un punto |

   Con `pan=(14,8)` e `turn=(0,0)` il clip 03 e' venuto pulito (0 coppie di
   frame identiche su 239, larghezza del triangolo 185→281 px con il dolly).
   I clip 01 e 02 ruotano la camera e sono sani, quindi il difetto non e'
   "camera ruotata" in assoluto: serve un ticket sul percorso di proiezione
   della mesh quando la camera e' animata in rotazione.
2. **Un layer `image` 2D sotto una mesh 3D bucherella la mesh.** Con il fondale
   come `type: "image"` il triangolo del clip 03 usciva coperto al ~50% da
   pixel di fondale in pattern casuale: 7 564 px di mesh invece di 20 255,
   gli altri lasciati al colore del fondo. Stesso fondo, stessa mesh, ma come
   `plane3d` (`enable_3d: true`): 20 255 px pieni. Il clip 03 quindi usa un
   fondale 3D; essendo illuminato, il fondo risulta piu' scuro dei clip 01/02
   (angolo 18,20,26 contro 37,40,48) e il probe HDRI e' sostituito da un rig
   bianco, altrimenti il piano 3D veniva tinto di caldo.
3. **Nessun depth buffer sul percorso mesh software**: le mesh si disegnano in
   ordine di layer. Le scene usano solo solidi convessi e non sovrapposti;
   altrimenti un mesh occluderebbe se stesso in modo errato.
4. **Illuminazione piatta sulle facce grandi**: la faccia frontale del cubo in
   `01`/`02` resta quasi uniforme; la variazione del probe si vede sulle facce
   inclinate e sui contorni. Non e' un errore di scena, e' la risposta attuale
   del materiale software.
