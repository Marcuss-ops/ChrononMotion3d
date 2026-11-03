# Contratto canonico camera Motion → RenderPlan (gate §8.1)

- **Stato:** contratto implementato e pinnato localmente il 2026-11-03.
- **Ambito:** camera risolta e campioni camera del RenderPlan V3.
- **Autorità:** ChrononMotion3D risolve la regia; Chronon3D proietta e
  renderizza i valori ricevuti. Il backend non ricostruisce target, roll,
  parent o traiettorie.
- **Sede del codice:** `Chronon3d/include/chronon3d/render_plan/camera_contract.hpp`
  per l'ammissione, `Chronon3d/include/chronon3d/render_plan/render_plan_layer_types.hpp`
  per il tipo wire, `RenderPlanLoweringCamera.cpp` per la conversione Motion.

Questo documento chiude il primo punto del gate §8.1 del ticket
`TICKET-CAMERA-DEFINITIVE-CLOSURE`: una sola descrizione di unità, spazi,
rotazioni, precedenza ottica e capability. I gate §8.2–§8.4 e la review/CI
same-SHA restano fuori da questo contratto.

## 1. Spazi e convenzioni

| Concetto | Contratto |
|---|---|
| Spazio Motion | world right-handed; la camera Motion guarda lungo **−Z**. |
| Spazio RenderPlan | world right-handed; la camera RenderPlan guarda lungo **+Z**. |
| Riflessione Z | `S = diag(1, 1, −1)`; posizione `p_plan = (x, y, −z)`. |
| Rotazione camera | `R_plan = S · R_motion · S`; non è una semplice inversione quando pan, tilt e roll sono combinati. |
| Ordine rotazioni wire | `rotation_deg = [tilt_x, pan_y, roll_z]`, gradi; il consumer ricostruisce `qZ · qY · qX`. |
| Parent/Null | Motion compone la gerarchia locale→world e applica il world matrix del parent della camera prima del campionamento. Il RenderPlan contiene solo pose world risolte; non contiene il grafo parent. |
| Look-at e roll | Risolti da Motion nell'orientamento della `CameraPose`. `lowerCameraPose` rifiuta una pose con `hasTarget` o `roll != 0`: non esistono canali target/roll separati nel wire. |
| Layer 2.5D | Motion emette TRS world già in coordinate RenderPlan, con origine canvas centrata. La profondità è camera-space positiva. |
| Unità di lunghezza | Unità scena, identiche sui due lati per posizione, dimensione layer, profondità e focus. Non sono pixel: la scala pixel deriva dalla proiezione (`focal_px / depth`). |
| Unità fisiche | focale e sensore in millimetri; FOV e rotazioni in gradi; `lens_shift` normalizzato al viewport; f-stop adimensionale. |
| Tempo | Frame assoluto (`double`) per i campioni; i secondi sono derivati da `fps_num/fps_den`. I sottocampioni shutter sono istanti assoluti, non offset interi. |
| Profondità 2.5D | `visible ⇔ depth > 1e-4`; `perspective_scale = focal_y_px / depth`; sentinel “nessuna superficie” `kUnsetDofDepth = 1e18`. Il dettaglio del piano 2.5D resta nel contratto `ChrononMotion3D/contracts/chronon.render-plan.2_5d-plane.v1.json`. |

La regola di proprietà è quella di
[`ADR-CAMERA-PIPELINE-OWNERSHIP.md`](ADR-CAMERA-PIPELINE-OWNERSHIP.md): Motion
risolve gerarchia, traiettorie, look-at, roll, easing e valori ottici;
Chronon3D proietta i valori ricevuti. Il RenderPlan è un trasporto risolto, non
una seconda authoring surface.

## 2. Precedenza ottica

1. `type == "orthographic"`: il consumer usa la modalità Zoom; `zoom` è
   l'autorità di scala. `focal_length_mm` deve restare `0` in authoring
   ortografico.
2. `type == "perspective"` e `focal_length_mm > 0`: modalità PhysicalLens.
   Focale e sensore governano la proiezione; `fov_deg` resta trasportato e
   validato, ma non è la sorgente della focale.
3. `type == "perspective"` e `focal_length_mm == 0`: modalità FieldOfView.
   `fov_deg` è l'autorità verticale.
4. `zoom` è il valore di compatibilità/Zoom; nei piani perspective-FOV è
   trasportato per compatibilità ma non sostituisce il FOV. Non viene
   ricalcolato o clampato dal backend.

`f_stop == 0` significa “non authored”. Se il DoF è abilitato, il consumer usa
il default canonico del `LensModel` (`f/2.8`), dichiarato qui e non inventato
dal backend. `focal_length_mm == 0` significa “usa il percorso FOV”.

## 3. Matrice capability camera/DoF

La matrice è applicata da `camera_contract_problem()` sul producer Motion
(`lowerCameraPose`, `emissionCameraProblem`, lowering dei campioni) e sul
decoder/compilatore Chronon3D. Un payload accettato da un lato è accettato
dall'altro; un payload rifiutato produce un errore tipizzato prima del render.

| Campo | Valori ammessi | Consumatore | Rifiuto |
|---|---|---|---|
| `type` | `perspective`, `orthographic` | adapter ottico | altro valore |
| `position` | 3 float finiti, unità scena | view/proiezione | non finito |
| `rotation_deg` | 3 float finiti, `[tilt_x, pan_y, roll_z]` | `qZ*qY*qX` | non finito |
| `fov_deg` | `[10, 120]` gradi | FieldOfView | fuori range, NaN, infinito |
| `near`, `far` | finiti, `0 < near < far` | clipping/proiezione | intervallo invalido |
| `zoom` | finito e `> 0` | modalità Zoom/ortografica | non positivo o non finito |
| `focal_length_mm` | `0` oppure `[12, 200]` mm | PhysicalLens se `> 0` | fuori range, NaN, infinito |
| `sensor_width_mm`, `sensor_height_mm` | `[1, 100]` mm | gate fisico/CoC | fuori range, non finito |
| `lens_shift` | 2 float finiti, `abs(x) <= 1`, `abs(y) <= 1` | principal point | fuori range, non finito |
| `f_stop` | `0` oppure `[0.7, 32]` | apertura fisica/DoF | fuori range, non finito |
| `iris_blades` | **solo `0`** | bokeh circolare | qualunque valore nonzero: nessun kernel poligonale |
| `dof_enabled` | bool | dispatch DoF | nessuna combinazione non supportata |
| `dof_focus_distance` | finito, `> 0`, `<= 10 000 000` unità scena | thin-lens S1 lungo l'asse ottico | zero, negativo, non finito, oltre il limite |
| `dof_max_blur` | `[0, 256]` | cap raggio blur | fuori range, non finito |
| `dof_highlight_gain` | `[0, 8]` | highlight weighting circolare, software e Vulkan | fuori range, non finito |

Regole di combinazione:

- `iris_blades != 0` è sempre rifiutato: non esiste un kernel a lamelle. Non è
  un clamp e non è un fallback a bokeh circolare.
- `dof_highlight_gain` nell'intervallo è accettato **e consumato** dai kernel
  circolari; `0` lo disabilita. Non viene scartato né normalizzato.
- `dof_focus_distance` è la distanza camera-space lungo l'asse ottico, non una
  coordinata world-Z. Non viene ricalcolata dal backend.
- La focale positiva ha precedenza sul FOV; il FOV resta validato per evitare
  payload ambigui. Non c'è clamp silenzioso.
- I default `f_stop=0` e `focal_length_mm=0` sono modalità dichiarate, non
  fallback non dichiarati.

## 4. Pinnaggio e verifica

- Test C3D: `tests/renderer/camera/test_camera_motion.cpp`, casi
  `Camera capability contract rejects unsupported optics at both compiler
  boundaries` e `Camera contract frontier matrix accepts and rejects identical
  payloads`. Il secondo percorre i due confini del compilatore, il piano base e
  `camera_samples`.
- Test Motion: `tests/motion_authoring_renderplan.cpp`, blocco “camera pose
  lowering” con FOV, focale, sensore, f-stop, focus, lens shift, max blur e
  highlight gain ai limiti e oltre.
- Parità producer/consumer: `tests/check_renderplan_camera_parity.cmake`
  richiede `camera_contract_problem` nel lowering Motion, nell'emissione e nel
  decoder C3D, oltre ai campi camera-level una sola volta.
- Comandi mirati usati per il pin locale:
  `cmake --build build/chronon/linux-video-release --target chronon3d_camera_tests -j2`
  e `cmake --build build/release-fast --target chrononmotion_authoring -j2`.
  I risultati del run sono registrati nel ticket camera, non in questo
  documento.

Questo contratto è locale al pair di lavoro. Non certifica CI same-SHA, review
umana, shutter §8.2, occlusione/DoF §8.3 o acceptance §8.4.
