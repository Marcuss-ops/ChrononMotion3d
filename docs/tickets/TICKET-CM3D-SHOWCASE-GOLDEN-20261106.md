# TICKET-CM3D-SHOWCASE-GOLDEN — 27 firme showcase stale dopo l'estensione `particles`

- **Stato:** CLOSED (2026-11-06)
- **Data:** 2026-11-06
- **Ambito:** ChrononMotion3D, `tests/showcase_golden.json`,
  `chrononmotion_showcase_all`, `chrononmotion_showcase_renderplan`
- **Owner:** ChrononMotion3D test/golden

## Problema

Dopo la build verde di T01, la suite completa si fermava a **65/67**:

- `chrononmotion_showcase_all` falliva 27 casi su 63 con
  `golden mismatch (actual …, expected …)`;
- `chrononmotion_showcase_renderplan` falliva `renderplan_roundtrip` con
  `1/10 assertions failed`, sempre per la firma.

I casi falliti non avevano assertion di comportamento in errore: solo la
firma FNV-1a-64 non corrispondeva al manifest. Senza diagnosi, il gate non
distingue una regressione reale da un manifest non rigenerato, e la suite non
può essere dichiarata verde.

## Diagnosi

La firma non è un hash dei soli campioni: `capture()` in
`tests/motion_showcase_internal.hpp` accoda

```cpp
snapshot.text(scene.toJson(0));
```

prima degli stati campionati. Quindi qualunque modifica al JSON della scena
cambia la firma anche se la valutazione è identica.

Il commit `2144cac` (“author particle emitters through the plan pipeline”) ha
aggiunto in `src/chrononmotion/motion/SceneExport.cpp`, per **ogni** layer:

```cpp
writer.key("particles");
if (layer.particleEmitter) writeParticles(writer, *layer.particleEmitter);
else writer.null();
```

e in `SceneExportContent.cpp` ha aggiunto `ContentKind::Particles` e
`writeParticles`. Il manifest `tests/showcase_golden.json` non è stato
rigenerato nello stesso commit: l'ultimo aggiornamento del file è il commit
`8943988`.

Il campo `"particles": null` è additivo e viene emesso anche per i layer senza
emettitore, quindi cambiano tutte le scene serializzate. I 27 casi falliti sono
esattamente i casi che serializzano una scena: 26 passano da `capture()`, il
ventisettesimo chiama direttamente `scene.toJson(0)` (riga 714). I 36 che non
serializzano una scena restano identici. La prova è nell'insieme degli ID:

- 27 ID cambiati tra vecchio e nuovo dump;
- 27 ID falliti nella suite;
- i due insiemi coincidono (`diff` vuoto);
- i 36 ID rimanenti hanno la stessa firma.

## Criteri di accettazione

- [x] Il dump del golden è deterministico: due esecuzioni consecutive di
      `--dump-golden` producono byte identici.
- [x] L'insieme delle firme cambiate coincide esattamente con l'insieme dei
      casi falliti.
- [x] Il numero di cambi è spiegato da una modifica additiva dello schema
      (`particles`), non da un cambio di valutazione.
- [x] Nessuna assertion di comportamento viene indebolita o disabilitata.
- [x] Il manifest conserva tutte le 63 case eseguibili con adapter.
- [x] Showcase e suite completa tornano verdi.

## Soluzione applicata

1. Generato il manifest con il binario già costruito:

   ```
   ./build/release-fast/chrononmotion-showcase --dump-golden \
     > tests/showcase_golden.json
   ```

2. Verificato che il dump ripetuto fosse identico e che il diff tra vecchio e
   nuovo manifest contenesse **27 inserzioni e 27 rimozioni**, tutte e sole le
   27 firme fallite.
3. Sostituito `tests/showcase_golden.json` con il dump verificato.

Nessun file sorgente o di test è stato modificato per far passare i golden:
la modifica è limitata al manifest, che ora include il campo `particles`
introdotto dal commit `2144cac`.

## Evidenza

Prima:

```
chrononmotion_showcase_all .......... Failed (27/63 golden mismatch)
chrononmotion_showcase_renderplan ... Failed (renderplan_roundtrip, 1/10)
97% tests passed, 2 tests failed out of 67
```

Dopo:

```
1/3 Test #53: chrononmotion_showcase_all .......... Passed
2/3 Test #54: chrononmotion_showcase_catalog ...... Passed
3/3 Test #55: chrononmotion_showcase_renderplan ... Passed
100% tests passed, 0 tests failed out of 3

100% tests passed, 0 tests failed out of 67
Total Test time (real) = 30.44 sec
```

Verifiche registrate:

- `--dump-golden` eseguito due volte: `cmp` identico.
- `changed_ids=27 failing_ids=27`; `diff` degli insiemi vuoto.
- `git diff --stat tests/showcase_golden.json`:
  `1 file changed, 27 insertions(+), 27 deletions(-)`.
- 63 entry nel manifest; `chrononmotion_showcase_all` passa 63/63 con adapter.

## Nota per il futuro

`"particles"` è ora parte intenzionale dello schema serializzato. Ogni futura
estensione di `SceneExport` che cambia il JSON deve rigenerare
`tests/showcase_golden.json` nello stesso commit, dopo aver verificato che il
dump sia deterministico e che l'insieme dei cambi sia spiegabile.
