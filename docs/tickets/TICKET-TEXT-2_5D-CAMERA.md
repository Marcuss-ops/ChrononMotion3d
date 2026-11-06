# TICKET-TEXT-2_5D-CAMERA — titoli luminosi su piano 2.5D con camera

## Stato

OPEN — C3D-owned. Questa scheda riguarda esclusivamente Chronon3D: shaping,
font stack, atlas MTSDF, raster, materiale gel, compositing, shadow, DoF,
lane software/Vulkan, stress test, golden e profiling. ChrononMotion3D non
esegue alcun lavoro sul testo: il testo è solo un layer 2.5D con `ContentRef`
Text e `Transform` comune; non esiste una feature testo CM3D. Tutte le fasi e
i gate sotto sono C3D-owned. Riferimento visivo fornito il 2026-11-05:
[titolo “calories of ingredients”](https://drive.google.com/file/d/1Tr3N3MPSoFrC6-x_-hLLTBwQv6KAfGrD/view).

## Obiettivo

Realizzare titoli editoriali su un piano di testo nello spazio 2.5D, leggibili
sopra footage e integrati con camera, profondità, occlusione e fuoco. Il caso
golden riprende il riferimento e la specifica ottica fornita: caratteri cyan/
blu gonfi e traslucidi come tubi di gel, cresta bombata al centro dei tratti,
riflessi speculari stretti, pancia inferiore scura, lieve rifrazione del footage
e alone controllabile. La camera muove il piano nel mondo; il rilievo ottico è
calcolato sul run e non deve sembrare un'estrusione piatta con bordi taglienti.

Il criterio “come After Effects” è limitato a questo flusso 2.5D verificabile;
non dichiara parità con ogni funzione tipografica o 3D di After Effects.

## Contratto di prodotto

- Il testo è un layer piano XY con posizione XYZ, anchor, scala XYZ e rotazione
  XYZ; eredita transform e opacità del parent.
- Camera prospettica e ortografica, animazione camera e campioni shutter
  proiettano il piano con la stessa convenzione coordinate del resto della
  scena. FOV, near/far, focus distance e aperture hanno unità documentate.
- Il piano partecipa a profondità e occlusione 2.5D. DOF e bokeh usano la sua
  profondità; input e uscita mantengono alpha premoltiplicato corretto.
- Fill, tracking, spaziatura, stroke e glow si applicano all’intero run di
  testo. Il materiale `GelText` deriva una heightmap dal mask/SDF del run, ne
  calcola i normali per pixel e li illumina con diffuse, speculare, rim e
  ombra interna. Un pass di shade combina colore, Fresnel/rifrazione limitata
  del footage sottostante e alpha premoltiplicato; il glow resta un contributo
  separato con raggio, intensità e colore espliciti. Nessun blur o bordo deve
  essere tagliato al rettangolo del singolo glifo.
- I campi non supportati dal consumer sono respinti dal producer con un
  diagnostic preciso, prima di emettere un RenderPlan parzialmente valido.

## Fasi e accettazione

1. **Contratto e trasformazioni** — RenderPlan tipizzato e fingerprintato per
   transform 3D del testo, parent, camera animata e focus. Round-trip Motion →
   RenderPlan → Chronon3D preserva i valori e rifiuta input fuori range.
2. **Proiezione e compositing** — fixture con testo davanti/dietro a due piani,
   camera prospettica in movimento e alpha parziale; entrambi gli ordini di
   authoring producono la stessa occlusione attesa.
3. **Stile gel del riferimento** — fixture 1920×1080 con filmato e headline
   cyan: cresta centrale continua, highlight stretto orientato alla luce,
   bordo di contatto blu scuro, displacement/refrazione sottile, glow sul run.
   Golden raw versionata; il testo resta leggibile, non ha clipping per glifo
   e non cambia forma ai bordi dei tile.
4. **Fuoco e profondità** — fixture near/focus/far con camera e focus animati;
   il testo nel piano focale è più nitido dei piani fuori fuoco, alpha e bokeh
   restano stabili e l’iris non supportato fallisce esplicitamente.
5. **Lane e ripetibilità** — stessi piani, parametri e golden sulla lane
   software e Vulkan strict-native; metriche RGB/alpha MAE e p99 registrate,
   zero fallback non dichiarati, due run byte-identiche dove la lane lo
   garantisce. Test Motion e C3D verdi sullo stesso source revision.

## Stress suite richiesta

1. **Topologia** — `%&@$# æøå gggQQQ 88888888 ||||||||` a più corpi. Occhielli
   e aste sottili collassano senza spike, normali invertiti, NaN o Inf;
   gradient height C1 entro la tolleranza numerica fissata dalla fixture.
2. **Tracking dinamico** — `calories or ingredients`, tracking sinusoidale da
   +50 px a −30 px a 60 e 120 FPS. I glifi formano un'unica silhouette liscia
   quando si toccano; highlight e hash sono deterministici senza isteresi.
3. **Sub-pixel serif** — font arrotondato e Didot/Bodoni, zoom 0.05×–10× e
   rotazione 15°. Niente sparkle speculare; roughness geometrica sale con la
   varianza screen-space dei normali (Toksvig o misura equivalente).
4. **Luce radente** — luce puntuale orbita 360° vicino al piano. La cresta
   dorsale resta continua e il terminatore usa diffuse wrap, senza metà
   glifo nera.
5. **Trasmissione ottica** — footage/scacchiera ad alto contrasto dietro il
   testo. Verificare assorbimento Beer–Lambert per canale, rifrazione limitata,
   alpha corretto e assenza di un semplice mix opaco `background/foreground`.
6. **Dispersione e banda** — 3840×2160 a 120 FPS, bloom piramidale a cinque
   stadi e dispersione RGB mascherata a testo/highlight. Profilare ciascun
   pass e la pipeline completa; obiettivo iniziale 1.5 ms per post-process su
   GPU desktop. Il tempo totale deve restare sotto 8.33 ms, senza degradare il
   footage non interessato. Se l'hardware non raggiunge l'obiettivo, registrare
   la misura e ridurre pass/risoluzione con una soglia concordata, senza
   dichiarare PASS per sola assenza di crash.

## Gap già individuati

- Motion respinge alcune proprietà di text animator che richiedono semantiche
  3D non rappresentabili nel text run (vedi
  `src/chrononmotion/motion/RenderPlanEmissionValidateText.cpp`).
- I ticket DoF/occlusione C3D documentano test software e probe Vulkan locali,
  ma dichiarano ancora aperta la fixture integrata software/Vulkan strict-native.
- La shadow canonica sul run testo (union mask, spread, blur, source-over
  lineare) è ancora aperta in `Chronon3d/docs/tickets/TICKET-TEXT-QUALITY-V2.md`.

## Strategia d’implementazione proposta

Riutilizzare shaping, atlas MTSDF, `GlowPipeline`, compositing RGBA32F,
displacement e proiezione 2.5D già presenti. Aggiungere un pass GPU condiviso
che costruisce una silhouette unita del run e una height profile morbida con
massimo al centro del tratto; da differenze finite ricava normali e fa
illuminazione speculare/rim. Campiona il frame sottostante con offset guidato
dal normale per simulare una rifrazione lieve, mantenendo il limite ottico in
pixel e l'alpha premoltiplicato. La lane software è l’oracolo numerico per
height/normal/compositing; il risultato visivo usa un golden raw e review.
Questo evita dipendenze da CC Glass o da Layer Styles di AE.

## Avanzamento 2026-11-05 (aggiornamento)

È stato aggiunto un oracle C++ condivisibile con la futura lane Vulkan in
`Chronon3d/include/chronon3d/text/gel_surface.hpp`: profilo parabolico a pendenza
normalizzata, smooth-max stabile (il campo atlas è positivo all'interno),
Half-Lambert, roughness geometrica e trasmissione Beer–Lambert. Le cinque
fixture matematiche locali passano **720/720 assertion** con
`chronon3d_renderer_core_tests --test-case="gel surface *"`.
La suite completa `chronon3d_renderer_core_tests` poi passa in 31.76 s con
queste fixture incluse. È stato poi aggiunto `TextMaterial::gel()` e collegato
al percorso MTSDF Vulkan: il compute shader ricostruisce normali dal campo di
distanza, applica Half-Lambert, doppia specularità e assorbimento cromatico.
La registrazione usa il bit 3 già libero di `effect_flags`, senza cambiare
l'ABI std430 da 128 byte; gli altri materiali mantengono il percorso esistente.
La build `chronon3d_renderer_core_tests` termina e CTest passa (214 casi;
30.60 s); l'oracle aggiornato passa (720 asserzioni). La canary Vulkan del
raster MTSDF attiva il bit gel su un glifo e passa (105 asserzioni), verificando
corpo assorbente e riflesso speculare nel dispatch GPU. Il test del
preset gel e della distinzione hash passa (4 asserzioni). Questi test non sono
ancora un golden della resa gel: mancano silhouette unita tra glifi,
rifrazione del frame, sei stress test visivi e profiling 4K/120.

### Render visivo Vulkan — 2026-11-05

Il primo piano di prova aveva uno strato colore opaco davanti al testo e
produceva un frame vuoto. Corretto il piano del fondale, un render tramite
`--video-sink raw` ora mostra `calories of ingredients` con `gel_surface: true`
sulla lane Vulkan. Evidenza e piano riproducibile:
[`gel-text-vulkan-smoke-20261105`](../../../Chronon3d/docs/evidence/gel-text-vulkan-smoke-20261105/README.md).
La resa mostra riflessi lucidi e volume, ma anche highlight sfaccettati; è uno
smoke visivo, non un golden né una chiusura di qualità. È stata aggiunta una
prima stima Toksvig screen-space: il gradiente SDF sotto lunghezza unitaria
allarga il lobo speculare. La nuova canary GPU passa (105 asserzioni) e il nuovo
render raw Vulkan è valido; gli artefatti a faccette restano visibili e quindi
la condizione anti-sparkle non è ancora dimostrata. I clip MP4 di revisione
(2 secondi ciascuno, frame fermi) sono nella cartella Drive richiesta:
[prima resa](https://drive.google.com/file/d/1n2CLkSplYv5sdKHKhxyJOj2Kky9faqrJ/view?usp=drivesdk),
[stima Toksvig](https://drive.google.com/file/d/17WmyM20TtXiOqZbnlO3Lgl9knyny0Q6a/view?usp=drivesdk).
Il ticket resta PARTIAL.

### Profilo morbido e controllo luce — 2026-11-05

La sezione circolare con pendenza infinita al bordo è stata sostituita con un
profilo parabolico a pendenza finita al bordo e zero al centro; oracle C++ e
compute Vulkan usano la stessa funzione e la derivata analitica. Il materiale accetta ora
`gel_light.azimuth_radians` e `gel_light.elevation` per-run, serializzati
attraverso RenderPlan V2/V3, inclusi nell'hash e trasportati nei due percorsi
Vulkan senza modificare il contratto std430 da 128 byte. Motion3D li emette e
ne convalida i range. Test aggiornati: gel surface 5 casi/723 asserzioni,
canary Vulkan 105 asserzioni, decoder/fingerprint 2 casi/17 asserzioni e
`chrononmotion_authoring` 1/1. Il frame fisso a luce radente è stato verificato
e caricato come [preview MP4](https://drive.google.com/file/d/1xxz2wz9ZK1rjHpZlQZvvPYiEBbNEM17x/view?usp=drivesdk).
Mostra che il parametro raggiunge lo shader, ma conserva highlight sfaccettati;
non è un'orbita animata a 360° né una chiusura dei sei stress test.

È stata poi corretta una discrepanza C++/Vulkan: lo shader stima ora a
differenze finite il gradiente dell'SDF e usa la derivata analitica del profilo
smoothstep per ricostruire la normale. I test oracle coprono la pendenza attesa
al centro del profilo e la normale piatta sull'asse mediano; la canary Vulkan e
il render raw sono stati rieseguiti.

Lo scenario di kerning dinamico è stato esercitato con un piano Vulkan di 61
frame: tracking +50 px → −30 px → +50 px. Il tracking si anima, ma gli highlight
si spezzano e i glifi si intersecano come superfici indipendenti. La prova
documenta il fail del requisito di fusione, non il suo superamento:
[MP4 kerning stress](https://drive.google.com/file/d/1a7J5a8S0r0nBXawQqOgETF1nWzZFp_-7/view?usp=drivesdk).

Il controllo del generatore conferma che Chronon ha già un campo vero a banda
larga: `MtsdfProfile` canonico usa 16 px di range e 24 px di padding; il canale
alpha memorizza la distanza firmata vera. `msdfgen` non è ancora necessario
per provare la bombatura e non è stato introdotto come dipendenza.

### Verifica cross-repo — 2026-11-05

Il target `chrononmotion_authoring` è stato ricompilato e il test CTest
`chrononmotion_authoring` passa (1/1, 0,05 s) sul checkout corrente, compresi
i casi di emissione e validazione dei parametri luce gel aggiunti in
`motion_authoring_renderplan.cpp`. Per la build locale è stato impostato
`CHRONON3D_DEPENDENCY_INCLUDE_DIR` su
`Chronon3d/vcpkg_installed/linux-video-test/x64-linux/include`, poiché il
percorso cached `linux-fast-dev` non contiene `magic_enum`; il sorgente
ChrononMotion3D non è stato modificato per aggirare la dipendenza. Questa è
verifica dell’authoring/emissione, non della proiezione 2.5D runtime, camera,
depth/occlusione o resa gel integrata. Il ticket resta OPEN.

### Render post-allineamento del profilo — 2026-11-05

Chronon3D ha rifatto il render Vulkan 1280×720 a un frame sul profilo GPU
allineato all'oracolo C++. Il processo termina correttamente e il contenuto
testuale è presente, ma la review del PNG mostra una superficie ciano ancora
piatta, senza volume né cresta speculare leggibile. Questo corregge la
precedente nota che descriveva volume leggibile: lo smoke iniziale e il nuovo
render non costituiscono una golden e il gate visivo resta FAIL. Fusione del
run, camera/depth runtime, stress suite e 4K/120 sono ancora da certificare.

## Fuori perimetro

- Mesh volumetrica con rifrazione ray-traced, materiali C4D e text-on-path.
  Il requisito è l’equivalente ottico 2.5D del gel, ottenuto con heightmap,
  normali, specularità e rifrazione screen-space, non la simulazione fisica
  completa.
- Ambient Occlusion: non è una capability corrente; richiede un ticket e un
  algoritmo dedicati.
- Parità universale con After Effects su ogni font, piattaforma e codec.

## Gate di chiusura

Non archiviare il ticket sulla sola compilazione o su screenshot software.
Servono la golden raw, il manifest dei parametri, tutti e sei gli scenari
stress, metriche per entrambe le lane, profiling 4K/120, test Motion/C3D sullo
stesso checkout e review visiva del risultato.
