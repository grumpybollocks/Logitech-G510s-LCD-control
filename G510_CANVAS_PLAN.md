# Canvas-Umbau der G510s – Beleuchtung und G-Tasten in einer Ansicht

Am 14.09.2026 auf dem Rechner mit der echten G510s entwickelt. Der Umbau orientiert sich am Canvas-Umbau des Schwesterprojekts für die G910 (dessen `G910_CANVAS_PLAN.md` und Branch `g910-canvas` befinden sich inzwischen unter [grumpybollocks/g910-control](https://github.com/grumpybollocks/g910-control), nicht in diesem Repository). Anlass war der ausdrückliche Wunsch des Benutzers, die dortige Lösung an die Möglichkeiten dieser Tastatur anzupassen. Es handelt sich nicht um gemeinsam verwendeten Code: Die Tastaturen können zu Unterschiedliches, um die Funktionen sinnvoll durch eine gemeinsame Abstraktion zu erzwingen. Die Implementierung wurde erstellt und selbst überprüft, vom Benutzer aber noch nicht visuell bestätigt (er schlief, als die Arbeit abgeschlossen wurde). Wie im übrigen Projekt gilt: Erst nach dieser Bestätigung ist die Arbeit vollständig abgeschlossen.

## Änderungen

- Die Tabs **Backlight** und **G-Keys** wurden zum Tab **Backlight + G-Keys** zusammengeführt.
- Das Raster aus gewöhnlichen `QPushButton`s wurde durch `G510Canvas` in `src/g510_canvas.py` ersetzt: ein tastaturförmiges `QWidget` mit `QPainter`-Darstellung und rechteckiger Treffpunkterkennung (`Cell(key_name, label, row, col, width, height, kind)`). Das Grundmuster entspricht `g910_canvas.py` im G910-Projekt.
- Der Tab **Custom Screens** wurde in **Custom Screens (WIP)** umbenannt, sonst aber ausdrücklich nicht verändert. Der Benutzer hatte gebeten, diesen Tab nicht anzufassen, solange er Änderungen am LCD nicht selbst bestätigen konnte.
- Das dunkle `STYLESHEET` der G910-Anwendung wurde unverändert übernommen und im Kommentar kenntlich gemacht, damit die Schwesteranwendungen einheitlich aussehen.
- Dieselbe Art Fehler wie ein veralteter, fest codierter `resize()`-Wert im G910-Projekt wurde vermieden: `MainWindow` ruft jetzt `self.adjustSize()` statt eine feste Fenstergröße zu verwenden. So passt das Fenster immer zum tatsächlichen Platzbedarf des aktiven Tabs.
- Eine MR-Anzeige wurde ergänzt (Zeile M1/M2/M3/MR links oberhalb der G-Tasten). Die physische Taste und LED (`g15::macro_record`) gab es bereits und `g510_macro_daemon.py` verwendete sie schon; die grafische Oberfläche zeigte sie bisher nicht.

## Aus dem G910-Projekt übernommen und eigens neu erstellt

**Unverändert und mit Quellenangabe übernommen** wurden Positionsdaten für Haupttastatur, Navigationstasten und Ziffernblock: echte Spaltenpositionen, Abstände zwischen Gruppen sowie Breiten für übliche ANSI-Tastenkappen (Backspace ungefähr 2u, Tab 1,5u, Caps 1,75u, Enter 2,25u, Shift 2,25u/2,75u, Ctrl/Alt/Win 1,25u). Diese Daten hatte das G910-Projekt von Solaars `ui/perkey/layouts/_keyboard_base.py` übernommen, das wiederum auf OpenRGBs `KeyboardLayoutManager.cpp` (GPL-2.0-or-later) basiert. Sie sind echte Referenzdaten für ein übliches ANSI-Layout, das auch die G510s verwendet. Das wurde anhand der Produktfotos des Benutzers bestätigt: vollständiges ANSI-Layout mit Ziffernblock. Die Wiederverwendung einer überprüften Tatsache ist hier sinnvoll; es wurde keine unbestätigte Annahme kopiert.

**Für die G510s neu erstellt** wurden die Positionen von G-Tasten, M-Tasten und LCD sowie die gesamte Logik der Beleuchtungssteuerung. Das Layout der G510s unterscheidet sich physisch deutlich: 18 G-Tasten in drei Spalten mit je sechs Zeilen, gruppiert in drei Blöcke zu zwei Zeilen; die G910 hat neun Tasten in einer Aufteilung von fünf plus vier. Eine Logo-Taste besitzt die G510s nicht.

## Unterschiede zwischen G910 und G510s

| | G910 | G510s |
|---|---|---|
| Beleuchtung | HID++-RGB je Taste (`keyledsctl`, individuelle Farbe pro Taste) | Eine einzige sysfs-LED für die ganze Tastatur (`/sys/class/leds/g15::kbd_backlight`); bestätigt: einzelne Tasten lassen sich auf dieser Hardware nicht adressieren |
| G-Tasten | 9 (G1–G9), fünf links und vier oben | 18 (G1–G18), drei Spalten × sechs Zeilen, in drei Zweiergruppen |
| Logo-Taste | Vorhanden und einzeln beleuchtbar | Kein entsprechendes Element |
| Canvas-Bedienung | Jede Taste anklicken oder auswählen und deren Farbe ändern | Nur G-Tasten (Makrodialog) und M1/M2/M3 (Profilwechsel) sind interaktiv. Alles andere ist Dekoration, weil die Hardware dort keine Bedienung anbietet. |

Das Canvas der G510s färbt daher die **gesamte Tastatur** (Hauptbereich, Navigation, Ziffernblock und M-Tasten-Zeile) in der **tatsächlich aktiven Beleuchtungsfarbe** (`read_current_rgb()`, vorhandener und geprüfter Code). Die Farbe wird aktualisiert, sobald **Apply** die Beleuchtung ändert. Eine Vorschau einzelner Tasten wäre irreführend, da die Hardware das nicht unterstützt. G-Tasten erhalten eine eigene, feste Akzentfarbe, damit klar ist, welcher Bereich anklickbar ist. Das LCD-Platzhalterelement bekommt einen dritten, gedämpften Farbton, um es als Bildschirm statt als Licht oder Taste zu kennzeichnen. Diese Dreiteilung entspricht auch den Produktfotos der Tastatur.

## MR-Taste: sichtbar, aber noch ohne Bedienfunktion

Das Canvas zeichnet MR und zeigt den echten LED-Zustand an (`/sys/class/leds/g15::macro_record/brightness`), der alle 500 ms parallel zur Synchronisierung von M1/M2/M3 abgefragt wird. Der sysfs-Pfad wurde an der echten Hardware als vorhanden und lesbar bestätigt. Ein Klick auf MR bewirkt derzeit nichts: In `g510_macro_daemon.py` und der übrigen Anwendung gibt es kein bestehendes Konzept für einen globalen Aufnahmemodus, an das sich ein solcher Klick anschließen ließe. Die Aufnahme erfolgt über den vorhandenen `MacroRecordDialog` pro G-Taste. Den Indikator sichtbar zu machen, ohne eine neue Funktion zu erfinden, wurde als passender Umfang angesehen. Für eine interaktive MR-Taste müsste das Verhalten zunächst entworfen werden.

## Während des Tests entdeckte Hardware-Eigenheit

Bei der Prüfung, ob `on_apply()` weiterhin die echte LED erreicht (ja: RGB wurde vor und nach der Änderung korrekt bestätigt), erwies sich die praktische Wirkung des Helligkeitsreglers als fraglich. Schreibvorgänge für Zwischenwerte (zum Beispiel 153 für 60 %, 30 oder sogar 0) in `/sys/class/leds/g15::kbd_backlight/brightness` ließen sich nicht zuverlässig zurücklesen. Ein Test meldete 255/100 %, ein anderer einen alten Wert 30, obwohl `max_brightness` 255 angab. Mögliche Ursachen waren eine Begrenzung durch den Treiber auf wenige Stufen, Verzögerungen bei der USB-Rückmeldung oder ein anderer, noch nicht untersuchter Effekt.

Das Problem bestand schon vor dem Canvas-Umbau und betraf dieselbe, unveränderte Funktion `apply_backlight()`. Es lag außerhalb des angefragten Umfangs und verhinderte nicht die korrekte Farbanzeige im Canvas. Daher wurde es zunächst für eine eigene Untersuchung vorgemerkt und nicht voreilig geändert. **Aktualisierung aus `G510_README.md`:** Die spätere gezielte Prüfung bestätigte, dass alle getesteten Helligkeitswerte sofort als 255 zurückgelesen wurden. Der funktionslose Regler wurde daraufhin entfernt.

## Durchgeführte Überprüfungen

Alle folgenden Prüfungen wurden vor der Übergabe durchgeführt:

- `python3 -m py_compile` für beide geänderten/neuen Dateien: ohne Fehler.
- Headless-Smoke-Test (`QT_QPA_PLATFORM=offscreen`): `MainWindow` mit beiden Tabs erstellt sich ohne Ausnahme.
- Signalverbindungen direkt mit dem laufenden Code geprüft, nicht nur aus dem Quelltext abgeleitet: Ein simulierter G-Tasten-Klick öffnete `MacroRecordDialog` (dessen `exec_()` war für den headless Test simuliert; der Aufruf wurde mit einer Mock-Prüfung bestätigt). Ein simulierter M-Tasten-Klick aktualisierte sowohl `current_profile` als auch die im Canvas hervorgehobene M-Taste. Die MR-Abfrage las den echten sysfs-Eintrag und spiegelte dessen Zustand wider.
- **Test an echter Hardware, nicht simuliert:** `on_apply()` wurde über den tatsächlichen GUI-Codepfad mit einer Testfarbe (Cyan und beliebiger Helligkeit) ausgeführt. Der Wert von `multi_intensity` der physischen LED änderte sich nachweislich entsprechend; auch die Tastaturfarbe im Canvas wurde aktualisiert. Anschließend wurde die gespeicherte Standardfarbe der Tastatur (Grün, volle Helligkeit) wiederhergestellt und geprüft.
- Ein Screenshot des tatsächlich laufenden vollständigen `MainWindow` (kein separates Testgerüst) wurde geprüft. Layout, Farben und Proportionen waren korrekt: blaue G-Tasten, gedämpftes LCD, Haupttastatur in der realen Live-Farbe Grün und M1 als aktives Profil hervorgehoben.
- Das gesamte Projekt wurde nach verbliebenen Verweisen auf die entfernten Klassen `BacklightTab`/`GKeysTab` durchsucht. Außer erklärenden Kommentaren gab es keine.
- Die echte GUI-Anwendung wurde mit dem neuen Code erneut gestartet. Das Protokoll blieb leer (keine Ausnahmen) und der Prozess lief weiter.

**Noch nicht geprüft:** ein echter Mausklick durch das sichtbare GUI-Fenster (statt einer simulierten Signalauslösung). Der Pfad `mousePressEvent` → `_cell_at` → Signal ist übliches Qt-Verhalten; die Signalseite wurde geprüft, aber ein Mensch hat noch nicht physisch geklickt. Ebenso stehen eine echte Makroaufnahme über das neue Canvas und die persönliche Sichtprüfung durch den Benutzer noch aus. Wie immer gilt: Erst nach dessen Bestätigung ist die Funktion „fertig“.

## Zusatz: Anzeige belegter Makrotasten

Bei der späteren Eigenprüfung wurde eine kleine Ergänzung vorgenommen, die über die G910 hinausgeht (dort gibt es kein Konzept für eine Makrobelegung, das angezeigt werden könnte): G-Tasten mit einem Makro im **aktuell aktiven Profil** erhalten einen goldenen statt des gewöhnlichen Akzentrands. So ist sofort sichtbar, welche Tasten programmiert sind. Aktualisiert wird die Markierung beim Profilwechsel (`select_profile`) und nach dem Schließen eines Makrodialogs (`open_key_dialog`), da **Save** oder **Clear** die Belegung geändert haben können. Beide Pfade rufen `refresh_assigned_keys()` auf. Die Funktion liest mit dem bestehenden `load_macros()` die Makros des aktiven Profils und übergibt die Tasten an `canvas.set_assigned_keys()`. Es gibt weder neuen Zustand noch neue Dateien; der vorhandene Ladepfad wird wiederverwendet.

## Umfassende Eigenprüfung (2026-09-14)

Auf ausdrücklichen Wunsch wurde die Anwendung ohne menschliche Interaktion vollständig geprüft. Sechs getrennte headless Testskripte wurden mit echtem Code und, wo passend, echter Hardware ausgeführt (keine Hardware-Mocks). Jedes Skript stellte alle veränderten Zustände wieder her:

1. **Geometrie:** Alle 127 Canvas-Zellen wurden paarweise auf überlappende Pixelrechtecke geprüft – keine Überlappung. Alle 18 G-Tastennamen waren vorhanden und eindeutig; die vier Namen der M-Tasten/MR waren korrekt.
2. **Klicks simuliert:** Alle 18 G-Tasten wurden genau in der Mitte ihres Rechtecks über `_cell_at` und die Signalauslösung angeklickt; jeweils wurde der korrekte Tastenname ausgegeben. Die vier M-Tasten/MR-Zellen wurden über ein echtes `QMouseEvent` und den tatsächlichen `mousePressEvent`-Handler geprüft, nicht über eine Abkürzung. M1/M2/M3 lösten korrekt aus; MR tat erwartungsgemäß nichts. Ein Klick auf den leeren Hintergrund löste ebenfalls nichts aus.
3. **Integration:** Für alle drei Profile wurden je drei Beispiel-G-Tasten über die tatsächliche Signalverdrahtung von `KeyboardTab` angeklickt (neun Kombinationen). Jedes Mal erhielt `MacroRecordDialog` das richtige Profil-/G-Tasten-Paar. Eine überwachte `__init__`-Methode prüfte die echten Argumente statt nur einen Mock aufzurufen; damit wurde insbesondere ein möglicher Fehler durch eine veraltete Schleifenvariable ausgeschlossen. Danach wurden alle neun `COLOR_RGB`-Voreinstellungen über `on_apply()` angewendet. Jede wurde sowohl mit `multi_intensity` der physischen LED als auch mit der Tastaturfarbe im Canvas abgeglichen. **Set as Default** ließ die aktuelle LED unverändert; dieser Fehlerfall war bereits früher aufgetreten und wurde wegen des gemeinsam genutzten Codepfads erneut geprüft.
4. **Dienststeuerung und MR-LED:** `restart`, `stop` und `start` liefen wirklich über `systemctl --user`; nach jedem Befehl wurde der echte Dienststatus geprüft, nicht nur das Ausbleiben einer Ausnahme. Die Datei `g15::macro_record` unter sysfs wurde mit 1 und anschließend 0 beschrieben; das Abfrageergebnis entsprach jeweils dem echten Wert.
5. **Fenstergröße und Custom-Screens-Regression:** Beim Wechsel zum schmaleren Custom-Screens-Tab wurde geprüft, dass sich das Fenster nicht verkleinert. Das wurde bewusst als kein Fehler bewertet: eine stabile Fenstergröße ist womöglich besser als ein springendes Fenster. `render_preview()` wurde erneut für alle sechs LCD-Bildschirme (0–5) aufgerufen. Außerdem wurde `save_macro`/`load_macros` mit der echten `macros.json` getestet; der ursprüngliche Inhalt wurde zuvor gesichert und danach wiederhergestellt.
6. **Anzeige belegter Tasten:** Gegen den tatsächlichen Inhalt von `macros.json` geprüft, nicht gegen Testdaten: korrekter Ausgangszustand für M1, korrekte Aktualisierung beim Wechsel zu M2/M3 sowie der reale Fall, dass ein neu gespeichertes Makro beim Schließen des Dialogs für eine *andere* Taste erkannt wird. Damit wurde geprüft, dass nicht nur die gerade bearbeitete Taste aktualisiert wird.

**Ergebnis: kein echter Fehler gefunden.** Alle Tests bestanden im ersten vollständigen Lauf. Anschließend wurden alle sechs Skripte nach der Ergänzung der Makroanzeigen erneut zusammen ausgeführt, um Regressionen auszuschließen; alle bestanden unverändert.

**Die Hardware blieb im ursprünglichen Zustand:** Die LED war wieder Grün (`(0 255 0)`) bei voller Helligkeit, die `macro_record`-LED stand auf 0, `macros.json` war bytegenau identisch mit dem Inhalt vor den Tests und alle drei Dienste waren aktiv.

## Änderungen zurücknehmen

Für diesen Umbau gibt es zwei Tags auf unterschiedlichen Ständen:

- **`pre-canvas-checkpoint`** (Commit `dae91d8`) – der Stand unmittelbar vor dem Canvas-Umbau, aber bereits mit dem Custom-Screens-Tab aus v1.1. Verwenden, wenn nur das Canvas zurückgenommen werden soll.
- **`v1.0`** (Commit `4521162`) – der letzte Stand, den der Benutzer persönlich bestätigt hatte, noch vor v1.1 und vor dem Canvas-Umbau. Nur verwenden, wenn beide Änderungen verworfen werden sollen.

Nur den Canvas-Umbau rückgängig machen und die Git-Historie erhalten (bevorzugt):

```sh
git revert d6a1d64
```

`main` hart auf den Stand vor dem Canvas-Umbau zurücksetzen (schreibt die Historie um; nur verwenden, wenn der Commit nicht bereits Grundlage weiterer Arbeit ist, und vorher immer mit `git log` prüfen):

```sh
git reset --hard pre-canvas-checkpoint
```

Nach dem Zurücksetzen muss die GUI neu gestartet werden. Der laufende Prozess übernimmt die Änderung nicht automatisch. Die dokumentierte Vorgehensweise lautet: `pkill -f src/g510_app.py` und anschließend vom Projektstamm `python3 src/g510_app.py` ausführen.
