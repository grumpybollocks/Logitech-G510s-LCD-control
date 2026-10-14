# Phase „Auf dem Display zeichnen“ – umgesetzt am 14.09.2026

**Aktualisierung: implementiert.** In der ursprünglichen Fassung dieses Dokuments sollte mit der Arbeit gewartet werden, bis der Benutzer anwesend ist. Zum Zeitpunkt der Umsetzung hatte der Benutzer jedoch ausdrücklich darum gebeten, die Arbeit abzuschließen. Alle übrigen Custom-Screens-Punkte waren fertiggestellt und für mehrere Funktionen derselben Sitzung war bereits volle Selbstständigkeit („you have autonomy im away AFK“) gewährt worden. Die Umsetzung folgte genau dem in diesem Dokument beschriebenen Ablauf: planen, mit `--preview` prüfen und anschließend verifizieren. Die frühere Warteanweisung wurde damit für diesen letzten Punkt aufgehoben, nicht ignoriert.

Der Inhalt unterhalb dieses Hinweises besteht aus der ursprünglichen Planung (unverändert im Sinn der Dokumentation) und dem Bericht zur Umsetzung und Prüfung.

## Was umgesetzt wurde

Der Umfang entspricht weitgehend der empfohlenen einfachen Lösung: eine Schaltfläche **Import Image...** im Custom-Screens-Bereich. Bilder können wie Sensorelemente verschoben, in dieser Version aber **nicht skaliert** werden. Ihre Abmessungen werden beim Import von `png-to-lcd.py` anhand von `max_width` bestimmt. Derzeit gilt standardmäßig eine maximale Breite von 60 Pixeln; die Höhe ergibt sich aus dem Seitenverhältnis des Originalbilds.

**Speicherformat (`custom_screens.txt`):** Wie vorgesehen wurde eine neue Zeilenart `IMAGE` ergänzt, getrennt von `ELEMENT`:

```text
IMAGE path=custom_screen_images/logo.bin width=40 height=25 x=6 y=6
```

`path` ist relativ zu `PROJECT_DIR`. Damit werden keine absoluten Pfade im Projekt gespeichert.

**C-Seite (`g510_lcd_stats.c`):** `custom_screen_t` erhielt neben dem vorhandenen `element_t elements[MAX_ELEMENTS]` ein separates Array `image_t images[MAX_IMAGES]` mit `MAX_IMAGES = 2` (auf dem 160 × 43 Pixel großen Bildschirm passen Bilder nur selten sinnvoll). `load_custom_screens()` hat einen eigenen Parser-Zweig für Bildzeilen; `draw_image_element()` liest die rohen `.bin`-Bytes ein und ruft `g15r_drawXBM()` auf. Derselbe Bibliotheksaufruf war früher in der Sitzung bereits separat geprüft worden; jetzt ist er zum ersten Mal in den echten Rendering-Pfad eingebunden. Außerdem prüft die Erkennung eines noch nicht eingerichteten Bildschirms (`cs->count == 0`) jetzt auch `image_count`, damit ein Bildschirm, der ausschließlich ein Bild enthält, nicht fälschlich den Platzhalter „not set up yet“ zeigt.

**Python-Seite (`g510_app.py`):** `load_custom_screens` und `save_custom_screens` lesen und schreiben nun `IMAGE`-Zeilen. Jedes Element erhält den Typ `"kind": "sensor"` oder `"kind": "image"`, sodass die übrige Anwendung sauber zwischen beiden Arten unterscheiden kann. `on_import_image()` übernimmt den kompletten Ablauf: Dateiauswahl, kollisionssicherer Ausgabedateiname, Aufruf von `png-to-lcd.py` als Unterprozess, Einlesen der ausgegebenen Breite/Höhe und Hinzufügen des Elements. Dabei werden `save_custom_screens` und `refresh_preview` wiederverwendet. `_element_rect()` im Canvas verwendet für Bilder deren bekannte Breite und Höhe; der `.meta`-Mechanismus für unbekannte Schriftmetriken bei Sensoren ist für Bilder nicht nötig. Die Größenänderungsgriffe schließen Bilder bereits aus, da sie ausschließlich `style == "bar"` prüfen.

## Durchgeführte Überprüfungen

Es wurde ein echter Ende-zu-Ende-Test durchgeführt, kein Simulationslauf: Ein echtes PNG durchlief die echte Funktion `on_import_image()`. Lediglich `QFileDialog.getOpenFileName` wurde so simuliert, dass sie den Testdateipfad zurückgab; der übrige Ablauf einschließlich echtem Aufruf von `png-to-lcd.py` und Datei-I/O lief unverändert.

Bestätigt wurden:

- die exakte Dateigröße der erzeugten `.bin`-Datei;
- ein unveränderter Rundlauf durch Speichern und anschließendes Laden;
- die Bildobergrenze pro Bildschirm: Bei Erreichen des Limits warnt die Anwendung und lehnt den Import ab, statt das Array unbemerkt zu überfüllen;
- Treffererkennung und Ziehen eines echten Bildelements;
- dass Bilder erwartungsgemäß keinen Ziehpunkt zum Skalieren erhalten.

Zusätzlich wurde das tatsächliche GUI-Fenster mit einem echten importierten Bild gerendert und optisch geprüft. Ein Testlogo erschien scharf an der richtigen Stelle. Dabei wurde ein echter Fehler entdeckt und behoben: Das Produktionsprogramm war noch nicht mit der Bilddarstellung neu gebaut worden. Der erste Versuch zeigte daher den Platzhalter „not set up yet“ statt des Bildes. Nach dem Neubau wurde erneut gerendert und die korrekte Darstellung bestätigt. Die Regressionstests liefen während der gesamten Arbeit erfolgreich. Die echte `custom_screens.txt` des Benutzers war nie gefährdet: Getestet wurde mit dem leeren Bildschirm L4 und/oder der ursprüngliche Dateiinhalt wurde unmittelbar bytegenau wiederhergestellt.

---

## Ursprünglicher Planungsteil

### Worum es geht und woher die Idee stammt

Zu Beginn des Projekts, bevor das Sensordashboard Custom Screens v1.1 existierte, wurde die Idee untersucht, beliebige Inhalte auf dem LCD zu zeichnen – Text und PNG-Bilder frei auf eigenen Bildschirmen platzieren. Die Prioritäten änderten sich dabei zweimal:

1. Zunächst stimmte der Benutzer einem einfachen Ansatz zu, änderte dann aber ausdrücklich die Richtung: *„hold up. roll back. i dont want a script on steroids“*. Eine auf yad/Bash basierende Umsetzung sollte nicht zu einem überladenen Skript anwachsen; stattdessen wurde eine richtige PyQt5-Anwendung entwickelt, aus der die heutige Architektur entstand.
2. Später wurde die Priorität erneut geändert: *„png function isnt that important as much as id like to do my own stats“*. Phase 2 wurde damit das Sensordashboard (Custom Screens v1.1), das inzwischen umgesetzt ist. PNG-/Bildplatzierung wurde zurückgestellt, aber nicht aufgegeben.

Hinweise darauf, dass die Funktion wieder aufgenommen werden sollte, gab es bereits: `.gitignore` enthielt schon `custom_screen_images/*.bin`, obwohl das Verzeichnis noch nicht existierte. Außerdem waren `src/png-to-lcd.py` (PNG zu 1-Bit-XBM, mit Dithering von Pillow) und `g15r_drawXBM()` aus `libg15render` bereits vorhanden, aber ungenutzt.

**Korrektur, am 14.09.2026 verifiziert:** Die frühere Fassung dieses Plans wiederholte eine veraltete Aussage aus einer README, nach der `g15r_drawXBM()` schon auf dem echten LCD mit einem Rechtecktest geprüft worden sei. Eine Überprüfung statt bloßer Übernahme der Behauptung zeigte: Die aktuelle C-Datei rief `g15r_drawXBM` nirgends auf, und `git log --all` enthielt keine Commits zu XBM, Rechtecken oder einem PNG-Test. Diese Bestätigung war in diesem Repository nicht vorhanden; sie könnte aus einem nicht eingecheckten Wegwerftest stammen oder veraltete Dokumentation sein.

Beide Komponenten wurden deshalb neu und direkt getestet:

- `png-to-lcd.py` wandelte ein echtes PNG mit 40 × 20 Pixeln (Rechteck und Text „TEST“) um. Die Ausgabe war exakt 100 Byte groß, wie für das gepackte 1-bpp-Format mit `((40+7)/8) * 20` erwartet.
- Ein kleines eigenständiges C-Programm lud genau diese `.bin`-Datei, rief `g15r_drawXBM(canvas, data, 40, 20, 10, 10)` auf und wurde mit derselben PPM-Vorschau wie der Custom-Screens-Tab gerendert. Die Ausgabe zeigte das Rechteck und den Text korrekt an der Position (10,10). Das wurde am gerenderten Bild überprüft, nicht allein aus der Dateigröße abgeleitet; echte Hardware wurde nicht verwendet.

Damit waren beide Komponenten unabhängig und zusammen isoliert geprüft. Die Integration in `g510_lcd_stats.c` und die GUI war zunächst noch eine eigene Implementierungsaufgabe und baute nun auf bestätigten Grundlagen statt auf einer unbelegten Dokumentationsbehauptung auf.

**Weitere Recherche am selben Tag:** Es wurde geprüft, ob es bestehende Bibliotheksfunktionen gibt, statt die erste Lösung ungeprüft zu übernehmen:

1. `libg15render` unterstützt auch das eigene Bildformat WBMP (`g15r_loadWbmpToBuf`/`g15r_loadWbmpSplash`), ein standardisiertes 1-Bit-Format. `png-to-lcd.py` umgeht dies und extrahiert stattdessen mit einem regulären Ausdruck Hexdaten aus einer von Pillow erstellten XBM-C-Quelldatei. Das wirkte zunächst wie unnötige Eigenentwicklung und wurde genauer untersucht.
2. Es wurde bestätigt, dass `img.convert("1")` von Pillow standardmäßig Floyd-Steinberg-Dithering verwendet – ein geeignetes Standardverfahren. Damit war ein Detail des ursprünglichen Plans geprüft statt nur angenommen.

Ein Wechsel zur direkten Nutzung von Pillow-`img.tobytes()` wurde ausprobiert, bevor er übernommen werden sollte. Der Test ergab, dass dieser vermeintlich sauberere Ansatz **falsch** ist: `g15r_drawXBM()` erwartet die traditionelle XBM-Bitreihenfolge; Pillow packt die Bits im Modus „1“ entgegengesetzt. Beide Varianten wurden mit demselben Testprogramm gerendert. Die XBM-Regex-Ausgabe war korrekt, die direkte `tobytes()`-Ausgabe sichtbar beschädigt: Die acht Pixel jedes Bytes waren gespiegelt und „TEST“ wurde zu unleserlichem Muster.

**Ergebnis:** Der Plan wurde nicht auf die vermeintlich einfachere Variante umgestellt. Die vorhandene XBM-Export-mit-Regex-Methode in `png-to-lcd.py` wirkt zwar unschön, ist für diese Bibliotheksfunktion aber korrekt. Ein Wechsel zu WBMP würde zusätzlich dessen Dateikopf für `g15r_loadWbmpToBuf` korrekt aufbauen müssen. Da `g15r_drawXBM()` bereits funktioniert und Breite/Höhe als separate Parameter erhält, bringt ein selbstbeschreibendes Format keinen Vorteil, solange die Abmessungen ohnehin in `custom_screens.txt` stehen. WBMP bleibt eine mögliche Alternative, falls später portable Einzelbilddateien benötigt werden; derzeit gibt es keinen Grund zum Wechsel.

Die Phase „auf dem Display zeichnen“ bedeutet somit: Bilder auf einem Bildschirm L2–L5 platzieren zu können, nicht nur Sensorbalken und Zahlen.

### Empfohlener Umfang – entsprechend dem geäußerten Wunsch

Der Benutzer hatte bei der ersten Diskussion gesagt: *„i don't want drag and drop if i could just click 'import png' button, whatever makes you code the least... i want something LIGHT, less is more kinda thinking.“* Dieser Wunsch nach einer einfachen Lösung sollte weiterhin gelten:

- Eine Schaltfläche **Import Image** im vorhandenen Custom-Screens-Tab, neben dem Formular zum Hinzufügen von Sensoren – kein neuer Tab und kein neuer Anwendungsmodus. Bilder und Sensorelemente können auf demselben Bildschirm kombiniert werden (zum Beispiel kleines Logo und CPU-Balken), da das bestehende System bereits mehrere Elemente je Bildschirm verwaltet.
- Kein manueller Zuschneide- oder Anpassungsschritt. `png-to-lcd.py` skaliert das Bild bereits automatisch und wandelt es um. Wenn ein bestimmtes Bild nach dem Dithering schlecht aussieht, kann das später gezielt verbessert werden; dafür ist jetzt kein Bildeditor erforderlich.
- Positionierung über dieselben X-/Y-Werte wie bei Sensorelementen. Ein Bild ist einfach ein weiterer Eintrag derselben Liste mit `x` und `y` statt `style`.

Das ist ausdrücklich die **einfache** Variante. Eine freie Pixel-Malfläche, auf der einzelne Pixel in einem Live-Raster mit 160 × 43 Pixeln an- und ausgeschaltet werden, wäre eine viel größere Funktion mit mehr Code, Oberfläche und Fehlerquellen. Sie entspricht nicht dem zuletzt geäußerten Wunsch und wird nur erwähnt, damit sie nicht stillschweigend ausgeschlossen wird.

### Technischer Plan

**Speicherformat:** Das vorhandene Textformat von `custom_screens.txt` wird um eine neue Elementart ergänzt:

```text
SCREEN L3
ELEMENT sensor=CPU_PCT style=bar x=6 y=3
IMAGE path=custom_screen_images/logo.bin width=40 height=20 x=60 y=5
```

`IMAGE` bleibt eine eigene Zeilenart und wird nicht als `ELEMENT sensor=...` mit einem Sonderwert getarnt. So kann der C-Parser die Elementarten unterscheiden, ohne das Sensorfeld zweckzuentfremden.

**Konvertierungsablauf:** **Import Image** öffnet einen `QFileDialog` für PNG (oder ein anderes von Pillow lesbares Format), ruft `png-to-lcd.py <input> <output.bin> <max_width> <max_height>` als Unterprozess auf und übernimmt dessen ausgegebene Breite und Höhe. Bei einer Prüfung nach dem Start am 14.09.2026 wurde die ursprüngliche Begrenzung nur nach Breite korrigiert: Ein schmales, hohes Bild hätte höher als das 43-Pixel-Display sein können und wäre nach einer späteren C-seitigen Prüfung stillschweigend verworfen worden. Jetzt werden Breite und Höhe begrenzt und Bilder nicht vergrößert. Die `.bin`-Datei wird unter `custom_screen_images/` abgelegt (bereits korrekt in `.gitignore`, da die konvertierten Bitmaps generierte Dateien und keine Quelldateien sind); anschließend wird eine `IMAGE`-Zeile für den ausgewählten Bildschirm an `custom_screens.txt` angehängt und die bestehende Vorschau aktualisiert.

**C-seitige Darstellung (`g510_lcd_stats.c`):** `draw_custom_screen()` durchläuft bereits die Elemente eines Bildschirms. Für `IMAGE` wird ein zusätzlicher Zweig benötigt, der die `.bin`-Datei mit `fopen()` öffnet, die Rohdaten liest und `g15r_drawXBM(canvas, data, width, height, x, y)` aufruft. Der Parser für `custom_screens.txt` muss `IMAGE`-Zeilen erkennen und Breite, Höhe und Pfad pro Bild speichern, entweder neben dem bestehenden `element_t`-Array oder in einem kleinen eigenen Array.

**Vorschau zuerst:** Jede Änderung soll über den vorhandenen Modus `--preview <screen> <outfile>` geprüft werden, bevor `/dev/g510-lcd` berührt wird. Der Vorschaupfad wurde genau dafür eingerichtet, Displayänderungen ohne dauernde Sichtprüfung an der Hardware zu testen. Die abschließende Bestätigung, ob das Ergebnis auf der echten Tastatur gut aussieht, muss weiterhin gemeinsam am Gerät erfolgen.

### Offene Fragen für die Umsetzung

1. **Wie viele Bilder pro Bildschirm?** Ein Bild wäre einfacher und entspräche dem Mindestumfang. Das Format erlaubt mehrere ohne zusätzliche Entwurfsarbeit; die Frage ist daher, ob die GUI das Hinzufügen eines zweiten Bilds ermöglichen soll.
2. **Was passiert, wenn ein Bildschirm gelöscht oder verändert wird?** Vermutlich ist nichts Besonderes nötig: Eine `.bin`-Datei ohne Verweis bleibt liegen, ähnlich wie beim Entfernen eines Sensorelements. Der Punkt ist aufgeführt, damit dies eine bewusste Entscheidung und kein übersehener Fall ist.
3. **Besteht Interesse an der freien Pixel-Malfläche**, eventuell in einer späteren Phase nach dem Bildimport? Sie wird derzeit nicht empfohlen, die Entscheidung liegt aber beim Benutzer.

## Nächster Schritt

Bis zur Freigabe durch den Benutzer passiert nichts. Danach wird der Umfang bestätigt oder angepasst und als normaler Entwicklungs- und Prüfzyklus umgesetzt: implementieren, ausgiebig über `--preview` testen und gemeinsam am echten LCD bestätigen, bevor die Funktion als abgeschlossen gilt.
