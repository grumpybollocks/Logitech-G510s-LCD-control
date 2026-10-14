# Logitech G510s – LCD-Statistiken, Tasten und Beleuchtung

Ausführliche technische Dokumentation zur G510s-Anwendung: Protokoll, Fehlerhistorie und Besonderheiten beim Erstellen. Diese Datei stammt aus der Zeit vor der README-Einstiegsseite für beide Tastaturen (`README.md`) und bezeichnet das Projekt daher an manchen Stellen als „dieses Projekt“. Beginne mit **Aktueller Status** und springe anschließend zum passenden Abschnitt.

## Aktueller Status

**v1.0 MARKIERT** (Git-Tag `v1.0`, veröffentlicht). Alle Funktionen im folgenden v1.0-Abschnitt sind abgeschlossen und vom Benutzer als funktionierend bestätigt. Bereits bestätigte Punkte müssen nicht erneut untersucht werden, sofern nicht ein konkretes Problem auftritt.

**v1.1 ERSTELLT, NOCH NICHT MARKIERT** – der Code ist in `main` eingecheckt; neu gebaute Programme laufen auf der echten Hardware. Die Anwendung wurde wiederholt ohne Absturz ausgeführt und die Bildschirmausgabe Pixel für Pixel geprüft. Eine physische Bestätigung durch den Benutzer am LCD der Tastatur steht jedoch noch aus. v1.1 darf erst dann markiert oder als „fertig“ bezeichnet werden.

**Canvas-Umbau für Beleuchtung und G-Tasten ERSTELLT, NOCH NICHT BESTÄTIGT** (2026-09-14, siehe [`G510_CANVAS_PLAN.md`](G510_CANVAS_PLAN.md)): Die Tabs „Backlight“ und „G-Keys“ wurden zum Tab „Backlight + G-Keys“ zusammengeführt. Das frühere einfache Tastenraster wurde durch ein tastaturförmiges Canvas (`src/g510_canvas.py`) ersetzt. Die Oberfläche orientiert sich am dunklen Stil der Schwesteranwendung für die G910, wurde aber unabhängig umgesetzt, da sich die tatsächlichen Funktionen der Tastaturen zu stark unterscheiden. Die Implementierung wurde ohne Bildschirm (headless), mit echten Signalverbindungen, über einen echten Beleuchtungstest an der Hardware und anhand eines Screenshots des vollständigen Fensters geprüft. Der Benutzer hatte die fertige Oberfläche zu diesem Zeitpunkt noch nicht selbst angesehen. Wie bei Custom Screens v1.1 gilt daher: so weit wie möglich gebaut und getestet, aber erst nach tatsächlicher Prüfung durch den Benutzer abgeschlossen. Beim Test wurde außerdem ein bereits vorhandenes, nicht durch den Umbau eingeführtes Hardwareproblem entdeckt: Zwischenwerte des Helligkeitsreglers ließen sich nicht zuverlässig aus `/sys/class/leds/g15::kbd_backlight/brightness` zurücklesen, obwohl `max_brightness=255` einen Wertebereich von 0 bis 255 erwarten lässt. **Aktualisierung 2026-09-15:** Das Problem wurde untersucht und bestätigt – jeder Schreibversuch wurde als 255 zurückgelesen. Der funktionslose Helligkeitsregler wurde deshalb entfernt. Einzelheiten stehen im Abschnitt „Beleuchtung“.

**Feinschliff der ersten Custom-Screens-Phase ERSTELLT**, auf Branch `g510s-dev`, nicht in `main` und noch nicht bestätigt (2026-09-15): Eine nachträgliche Prüfung des PNG-/Bildimports fand zwei echte Robustheitslücken: Der Bildpfad-Puffer wurde nicht sicher terminiert und die Bildhöhe beim Import nicht begrenzt. In Kombination hätte ein zu hohes Bild beim Laden unbemerkt verschwinden können. Anschließend wurde die Auffindbarkeit verbessert: Zwei direkt nacheinander importierte Bilder liegen nicht mehr exakt übereinander, deaktivierte L1-Steuerelemente erklären sich per Tooltip und der dauerhafte Zieh-Hinweis gilt nun auch für Bilder. Die Regressionstests waren erfolgreich; `custom_screens.txt` blieb unverändert. Zusammenführung und Markierung warten weiterhin auf die Prüfung an der echten Tastatur.

**Textfelder, L1-Oberflächenkorrekturen und analoge Uhr ERSTELLT**, auf `g510s-dev`, nicht in `main` und noch nicht bestätigt (2026-09-15): Für L3–L5 können bis zu vier Textfelder pro Bildschirm hinzugefügt und wie andere Elemente verschoben werden. Sie werden als `TEXT content=... x=... y=...` in `custom_screens.txt` gespeichert. Ein Fehler in der L1-Vorschau ließ beim Tippen gelegentlich den Inhalt von L2 aufblitzen: Drei Aufrufer verwendeten dieselbe temporäre Datei `--preview`. Behoben wurde dies mit separaten, gekennzeichneten temporären Dateien und atomarem Schreiben per temporärer Datei plus Umbenennung auf der C-Seite. Außerdem erhielt die Uhr im bislang leeren rechten Bereich ein analoges Zifferblatt; siehe „Analog-Uhr“ im Abschnitt „Mehrbildschirmsystem“. Die Regressionstests liefen erfolgreich, darunter elf Fehlerprüfskripte (auch solche mit Hardwarezugriff, da sie den ursprünglichen Zustand wiederherstellen). Das Produktionsprogramm wurde neu gebaut und der Dienst `g510-lcd-stats.service` sowie die grafische Oberfläche wurden neu gestartet. Die physische Bestätigung durch den Benutzer steht weiterhin aus.

**Repositoryübergreifende Fehlerprüfung, in `main` eingecheckt, aber nicht markiert/veröffentlicht** (2026-09-20; auf einem Rechner ohne angeschlossene G510s durchgeführt): Eine Prüfung von `g510_lcd_stats.c` (alle 1621 Zeilen), `src/g510_app.py` und `src/g510_macro_daemon.py` deckte drei echte Fehler auf. Jeder wurde vor und nach der Korrektur direkt reproduziert:

1. `RecorderThread.run()` in `g510_app.py` öffnete das Tastaturgerät ohne Fehlerbehandlung. War die Tastatur nicht angeschlossen, entkam die Ausnahme aus `QThread.run()` und beendete den gesamten Prozess mit `SIGABRT`. Das wurde sowohl durch direkten Thread-Aufruf als auch über den echten Aufzeichnungsknopf reproduziert. Das Öffnen ist jetzt in `try/except` eingeschlossen; bei einem Fehler wird `finished_recording("", False)` gesendet. Das entspricht dem `ok: bool`-Muster des später in derselben Datei hinzugefügten `ImageResizeWorker`. Der Dialog meldet nun „Keyboard not found -- is it plugged in?“ statt abzustürzen.
2. `main()` in `g510_macro_daemon.py` öffnete das Gerät ebenfalls ungeschützt. Ohne angeschlossene Tastatur trat nachweislich `FileNotFoundError` auf. Zusammen mit `Restart=on-failure` und `RestartSec=3` führte das dazu, dass systemd den Prozess bis zum Anschließen der Tastatur alle drei Sekunden beendete und neu startete. Die neue Funktion `_wait_for_device()` wartet nun ruhig auf das Gerät, statt abzustürzen.
3. `read_cpu_totals()` und `get_ram_kb()` in `g510_lcd_stats.c` waren die einzigen Statistikfunktionen, die den Rückgabewert von `fopen()` nicht auf `NULL` prüften. Alle anderen – etwa `get_cpu_temp_c`, `get_vram_bytes`, `get_swap_percent` und `get_uptime_hours` – taten dies bereits. Ein isolierter Test bestätigte, dass `fscanf`/`fgets` mit einem `NULL FILE*` tatsächlich `SIGSEGV` (Exitcode 139) auslösen. Beide Funktionen liegen zudem im häufig ausgeführten Pfad `draw_stats_screen()` der Standardanzeige, nicht in einem seltenen Sonderfall. Sie geben bei fehlgeschlagenem `fopen()` jetzt wie die übrigen Funktionen leere Werte zurück und beenden sich frühzeitig. Die exakt korrigierten Funktionskörper wurden mit `-Wall -Wextra` separat kompiliert und mit dem echten `/proc/stat` und `/proc/meminfo` getestet; die erwarteten, unveränderten Nicht-Null-Werte kamen zurück.

**Aktualisierung 2026-09-20 (an echter Hardware bestätigt):** Das gesamte Projekt wurde auf dem Rechner mit angeschlossener G510s neu gebaut. `-Wall -Wextra` meldete keine Probleme; `--preview 0` zeigte CPU- und RAM-Werte ungleich null ohne Absturz. Alle 19 Regressionstests liefen erfolgreich. Das Produktionsprogramm wurde installiert, alle drei systemd-Benutzerdienste wurden neu gestartet und als aktiv bestätigt, und die grafische Oberfläche wurde erneut gestartet. Damit sind alle drei Fehlerkorrekturen – der Schutz in `RecorderThread`, `_wait_for_device()` und die beiden `fopen()`-Prüfungen – durchgängig und nicht nur anhand des Quellcodes bestätigt.

**Aktualisierung 2026-09-20 (erledigt):** Der Git-Verlauf des früheren Repositorys enthielt echte persönliche Kennungen; dieselben waren auch im aktuellen Inhalt des `PKGBUILD` vorhanden. Statt den Verlauf vor Ort umzuschreiben (`git-filter-repo`, erzwungener Push und dadurch ungültige bestehende Klone) wurde das Repository unter seinem aktuellen Namen an einen frischen, verlaufsfreien Ort verschoben. Die Maintainer-Zeile im `PKGBUILD` enthält keine E-Mail-Adresse mehr; auch der Autor des einzigen initialen Commits des neuen Repositorys trägt keine persönliche Kennung.

## Neuerungen in v1.1

Der Tab **Custom Screens** in `g510_app.py` ist ein AIDA64-ähnlicher Editor für die Tasten L2–L5. Wähle einen Bildschirm, füge Sensoren mit Darstellungsart (Number oder Bar) und X-/Y-Position hinzu, prüfe das Ergebnis in der Live-Vorschau und entferne Elemente bei Bedarf. Änderungen werden sofort gespeichert. Zu den zusätzlichen Sensoren gegenüber CPU, RAM, VRAM und CPU-Temperatur gehören GPU-Auslastung, GPU-Edge-/Hotspot-/VRAM-Temperaturen, Swap-Auslastung, Speicherbelegung des Stammverzeichnisses und eines optionalen zweiten Laufwerks, Laufzeit, Netzwerk-Up-/Downloadrate sowie sechs nicht näher bezeichnete Mainboard-Temperaturen („MB Temp 1..6“ statt erfundener Namen). Layouts stehen als einfache `SCREEN`-/`ELEMENT`-Zeilen in `custom_screens.txt`; `g510_lcd_stats.c` zeichnet sie mit `draw_custom_screen()`.

**Vorschau:** `g510_lcd_stats` besitzt einen einmaligen Modus `--preview <screen> <outfile>`. Er zeichnet ein Bild mit exakt derselben Zeichenlogik wie das Live-LCD, schreibt das Ergebnis aber als PPM-Datei, statt `/dev/g510-lcd` zu verändern. `g510_app.py` führt den Vorgang im Hintergrund aus und zeigt das Bild an. Die Vorschau entspricht daher Pixel für Pixel der tatsächlichen Anzeige.

Beim Erstellen wurden mehrere echte Fehler behoben, die auch bei zukünftigen Änderungen wichtig sind:

1. Dem Compile-Aufruf von `g510_lcd_stats.c` fehlten FreeType-/TTF-Flags, die beim Erstellen von `libg15render.so` verwendet worden waren. Dadurch war die lokale Sicht auf `g15canvas` kleiner als die Struktur, in die die Bibliothek schrieb: zusätzliche `FT_Library`-/`FT_Face`-Felder sind über `#ifdef TTF_SUPPORT` bedingt. `g15r_initCanvas()` schrieb über das Ende der auf dem Stack angelegten Struktur hinaus. Dieser bereits in v1.0 vorhandene Fehler blieb zunächst unbemerkt, da er in Stack-Padding schrieb. Zusätzliche lokale Variablen in v1.1 verschoben den Schreibzugriff auf den Stack-Canary und verursachten den Absturz „stack smashing detected“. AddressSanitizer bestätigte den Fehler (keine Heap-/Logiklücke; nur undichte FreeType-Initialisierungsallokationen, die harmlos sind); ein direkter Vergleich mit und ohne Stack-Protector bestätigte die Ursache. Korrektur: Vor `#include <libg15render.h>` werden `TTF_SUPPORT` und die FreeType-Header eingebunden; kompiliert wird mit `$(pkg-config --cflags freetype2)` und gelinkt mit `$(pkg-config --libs freetype2)`. Dies ist inzwischen in `install.sh`, `scripts/rebuild.sh` und der Quelldatei enthalten. Bei einem manuellen Compile müssen dieselben Flags verwendet werden, sonst kann es weiterhin zu intermittierenden Abstürzen kommen.
2. Die konvertierte Schrift `fonts/lcd-label-8.fnt` (Eurostile Bold) enthielt ein beschädigtes „S“, das eher wie eine „6“ aussah. Zuvor war das nicht aufgefallen, da CPU/RAM/VRAM/TEMP/MAX kein S enthalten; in v1.1 deckten die Beschriftungen SWAP und DISK den Fehler sofort auf. Die Schrift wurde aus der Originaldatei `/usr/local/share/fonts/e/Eurostile_Bold.otf` mit `g15fontconvert -s 8 -i <otf> -o fonts/lcd-label-8.fnt` erneut erstellt. Das gesamte Alphabet sowie alle in der Sensortabelle verwendeten Beschriftungen wurden gerendert und geprüft; die vorhandene CPU-/RAM-/VRAM-/TEMP-Anzeige wurde pixelgenau unverändert bestätigt.
3. Bei der Fehlerprüfung vom 2026-09-20 wurden die fehlenden `NULL`-Prüfungen in `read_cpu_totals()` und `get_ram_kb()` gefunden. Jeder Aufruf von `fopen()` muss vor der Verwendung des `FILE*` geprüft werden. Neue Statistikfunktionen sollten dem Muster `if (!f) { ...Ausgaben auf 0 setzen...; return; }` der übrigen Funktionen folgen – auch wenn `/proc`- und `/sys`-Dateien bisher immer vorhanden waren.

**PNG-/Bildplatzierung ist implementiert** (2026-09-14, siehe [`G510_DRAW_ON_DISPLAY_PLAN.md`](G510_DRAW_ON_DISPLAY_PLAN.md)): Die Schaltfläche **Import Image...** im Custom-Screens-Tab wandelt ein PNG (oder ein anderes von Pillow lesbares Bild) um und platziert es auf dem aktuellen Bildschirm. Es lässt sich wie ein Sensorelement verschieben. Die Implementierung verwendet nun tatsächlich `src/png-to-lcd.py` und `g15r_drawXBM()`.

## v1.0

Alle folgenden v1.0-Funktionen sind abgeschlossen und vom Benutzer als funktionierend bestätigt:

- **Hauptoberfläche ist `g510_app.py`** (PyQt5, ein Fenster mit `QTabWidget`).
  - **Tab „Backlight + G-Keys“** (seit 2026-09-14 zusammengeführt; zuvor getrennte Tabs, siehe [`G510_CANVAS_PLAN.md`](G510_CANVAS_PLAN.md)): links ein tastaturförmiges Canvas (`g510_canvas.py`), dessen ganze Fläche die tatsächlich aktive Beleuchtungsfarbe zeigt. G-Tasten erscheinen in einer eigenen Akzentfarbe und öffnen beim Anklicken den Makrodialog; M1/M2/M3/MR stehen über den G-Tasten. Rechts befinden sich Farbauswahl, **Apply** (sofort anwenden und als Startfarbe speichern), **Set as Default** (speichern, ohne die aktuelle Farbe zu ändern) und kompakte Dienststeuerungen (Start/Stop/Restart). Der Helligkeitsregler wurde am 15.09.2026 entfernt; siehe „Beleuchtung“. **Apply Defaults** wurde auf Wunsch entfernt, da es dasselbe wie **Set as Default** tat und zudem einen echten Fehler enthielt: Es las eine hart codierte Konstante statt des tatsächlich gespeicherten Standardwerts. Bei ähnlichen Fehlern mit „Einstellungen merken“ sollte daher auf fest codierte Rückfallwerte statt auf das gespeicherte Profil geachtet werden. Die Oberfläche liest alle 500 ms die aktive Profil-Datei des Daemons aus, damit auch physische M-Tastendrücke die hervorgehobene Taste im Canvas aktualisieren. Für jede G-Taste kann entweder eine Tastenkombination aufgezeichnet werden (QThread und `python-evdev`; das Gerät wird während der Aufnahme exklusiv beansprucht) oder ein Shell-Befehl eingetragen werden. Beides wurde vom Benutzer bestätigt und über `g510_macro_daemon.py` wiedergegeben. Der Daemon läuft als separater systemd-Benutzerdienst und beobachtet `/dev/g510-keys`. Der physische Profilwechsel per M1/M2/M3 funktioniert nachweislich; die Keycodes waren korrekt, die Logik des Daemons ebenfalls. Die Hardware-LEDs M1/M2/M3/MR funktionieren ebenfalls. Behoben wurde dies durch den Wechsel von einer deklarativen udev-Regel mit `MODE=`/`GROUP=` (die nie griff; als Ursache wurde nur festgestellt, dass diese LEDs an eine Sitzung gebunden sind und sich anders verhalten) zum bereits für `kbd_backlight` bewährten Muster `ACTION=="add"` plus `RUN+="chgrp/chmod"`. Der MR-LED-Zustand wird im Canvas angezeigt; ein Klick auf MR bewirkt derzeit nichts (siehe [`G510_CANVAS_PLAN.md`](G510_CANVAS_PLAN.md)).
- Die alte yad-/Bash-Oberfläche zur Beleuchtungssteuerung (`g510-backlight-control.sh` und `g510-backlight-apply.sh`) gehört nicht mehr zu diesem Branch. Sie liegt als eigenständige Referenz im Branch `legacy-yad-backlight-script`. In `main` ist `g510_app.py` die einzige Oberfläche.

Die folgenden Abschnitte erläutern, warum bestimmte nicht offensichtliche technische Entscheidungen getroffen wurden – nützlich, wenn etwas diagnostiziert oder geändert werden muss.

## Projektüberblick

Die Anwendung verwandelt das eingebaute LCD der Logitech G510s in eine Live-Anzeige für CPU, RAM, VRAM und Temperatur, verbindet die fünf Tasten unter dem Display (L1–L5) mit der Anwendung und steuert die RGB-Beleuchtung. Zur Laufzeit sind keine Root-Rechte nötig. Der Start erfolgt automatisch bei der Anmeldung; Neustarts, erneutes Anstecken und Kernel-Updates übersteht die Anwendung.

## Wichtige Dateien

| Datei | Aufgabe |
| --- | --- |
| `g510_app.py` | Hauptoberfläche (PyQt5). Beleuchtung und G-Tasten in einem Canvas-Tab; Custom Screens in einem eigenen, noch in Arbeit befindlichen Tab. |
| `g510_canvas.py` | Tastaturförmiges Canvas für „Backlight + G-Keys“ mit `QPainter` und `Cell`-Geometrie pro Taste; siehe [`G510_CANVAS_PLAN.md`](G510_CANVAS_PLAN.md). |
| `g510_lcd_stats.c` | Zeichnet die LCD-Bildschirme; Hauptprogramm. |
| `g510_lcd_buttons.c` | Erkennt die Tasten L1–L5. |
| `png-to-lcd.py` | Konvertiert PNG in ein rohes 1-bpp-Bitmap; wird über **Import Image...** aufgerufen. |
| `g510-lcd-stats.service` | systemd-Benutzerdienst für das Statistikprogramm. |
| `g510-lcd-buttons.service` | systemd-Benutzerdienst für die Tasten. |
| `99-g510-lcd.rules` | udev-Regeln, siehe „Dauerhafte Einrichtung“. |
| `fonts/lcd-label-8.fnt` | Konvertierte Schriftart für LCD-Beschriftungen, die tatsächlich verwendet wird; siehe „Schriften“. |
| `fonts/source-ttf/FONT.otf` | Quelldatei für `lcd-label-8.fnt`, eingecheckt im Repository. Die eingebetteten Metadaten verweisen auf eine Adobe-Typekit-EULA statt auf eine freie Lizenz. Das Risiko ist bekannt und bewusst akzeptiert. `install.sh` und `scripts/rebuild.sh` konvertieren die Schrift inzwischen automatisch. |
| `fonts/source-ttf/FALLBACK.ttf` | Liberation Sans Bold unter der SIL Open Font License; wird automatisch verwendet, wenn `FONT.otf` nicht geladen werden kann oder die Prüfung auf beschädigte Glyphen nicht besteht. Siehe `font_sanity.h`. |
| `set-backlight-color.sh` | Wendet beim Systemstart die gespeicherte Beleuchtungsfarbe an. Die GUI schreibt die Datei bei **Apply** automatisch neu; manuelle Änderungen bleiben daher nicht dauerhaft bestehen. |
| `rebuild.sh` | Kompiliert beide Programme neu und startet die Dienste. |
| `view-logs.sh` | Zeigt die Live-Protokolle beider Dienste. |
| `start.sh` | Start mit einem Klick über das Desktop-Symbol. |

Desktop-Symbole in `~/Desktop` (mit Namen „G510 LCD - …“):

| Symbol | Aufgabe |
| --- | --- |
| App | Startet `g510_app.py` (Hauptoberfläche). |
| Rebuild | Nach Änderungen an `.c`-Dateien ausführen. |
| Start | Startet beide Dienste neu, egal ob sie gestoppt, abgestürzt oder nicht mehr ansprechbar sind. |
| View Logs | Öffnet die Live-Protokolle. |
| Project Folder | Öffnet diesen Ordner im Dateimanager. |

## Änderungen am Code

1. Bearbeite `g510_lcd_stats.c` oder `g510_lcd_buttons.c`.
2. Doppelklicke auf dem Desktop auf **G510 LCD - Rebuild** oder führe `./rebuild.sh` aus. Beide Programme werden neu kompiliert und die Dienste neu gestartet.
3. Prüfe nach Änderungen an der Pixelzeichnung, ob die Ausgabe fehlerfrei ist. Lies zuvor „Pixelformat“, bevor du einen Darstellungsfehler für einen Tippfehler hältst.

## Pixelformat – die wichtigste technische Einzelheit

Das LCD hat 160 × 43 Pixel und einen Bit pro Pixel. Der Canvas-Puffer von libg15render speichert die Pixel zeilenweise und mit dem höchstwertigen Bit zuerst (`pixel_offset = y*160+x`). Das physische LCD erwartet dagegen ein vertikales „Page“-Format: acht Pixel pro Byte, ein Byte pro Spalte, wobei das niederwertigste Bit das oberste Pixel enthält. Dieses Format ist vom SSD1306 bekannt. `dump_to_lcd_format()` in `g510_lcd_stats.c` ist eine bytegenaue Portierung von libg15s `dumpPixmapIntoLCDFormat()` und führt die Umwandlung durch. Werden statt sauberem Text diagonale Streifen oder unleserliche Pixel angezeigt, fehlt wahrscheinlich diese Umwandlung. Den Canvas-Puffer keinesfalls direkt mit `memcpy` an das Gerät senden – das ergibt Bildrauschen.

Das endgültige HID-Paket umfasst 992 Byte: Byte 0 ist `0x03` (Report-ID, durch Lesen des libg15-Quellcodes entdeckt und sonst nirgends dokumentiert), Bytes 1–31 sind Padding/Nullen und Bytes 32–991 enthalten die 960 umgewandelten Bilddaten im Page-Format.

## Warum weder `g15daemon` noch libg15s Gerätecode verwendet wird

`g15daemon`, das übliche Werkzeug für diese Tastaturen, beansprucht über libusb die gesamte USB-Schnittstelle der Zusatztasten und trennt sie dadurch vom Kernel-Treiber `hid_lg_g15`. Anschließend interpretiert es Tastendrücke mit einer eigenen Tabelle, die für die G510s seit Jahren falsch ist. Bei Tests führte das zu massiven Fehlern bei Medien- und Lautstärketasten, darunter zufällig ausgelöste Tastenanschläge.

Dieses Programm schreibt stattdessen direkt nach `/dev/g510-lcd`, einem hidraw-Geräteknoten. hidraw ermöglicht das Senden und Empfangen von HID-Berichten über ein Gerät, das weiterhin dem Kernel-Treiber gehört; der Treiber muss also nicht getrennt werden. Die normale Tastaturverarbeitung durch `hid_lg_g15` bleibt unbeeinträchtigt. Das wurde wiederholt geprüft: Das LCD lief dauerhaft, während Medientasten verwendet wurden, ohne dass sich die Funktionen gegenseitig störten.

## Korrekturen für Medientasten

Die folgenden Probleme betreffen nicht den LCD-Code, erklären aber, warum Änderungen an der Eingabebehandlung vorsichtig erfolgen sollten:

1. Die Play/Pause-Taste ist physisch defekt (Korrosion/abgenutzter Kontakt). Eine rohe HID-Aufzeichnung zeigte keinerlei Signal von dieser Taste, selbst nach Kontaktreiniger; benachbarte Tasten derselben Schnittstelle funktionierten. Eine udev-hwdb-Zuordnung verwendet deshalb die Stop-Taste für Play/Pause: `/etc/udev/hwdb.d/91-g510-stop-to-playpause.hwdb` ordnet deren Scan-Code `0x000c00b7` (HID-Consumer-Seite „Stop“) `KEY_PLAYPAUSE` statt `KEY_STOPCD` zu. Damit geht die Hardware-Stop-Funktion verloren, Play/Pause funktioniert dafür. Zum Wiederherstellen von Stop die hwdb-Datei löschen und `sudo systemd-hwdb update` ausführen.
2. Braves native Mediensteuerung und die Browser-Erweiterung „Plasma Integration“ (ID `cimiefiiaegbelhefglklhhakcgmhkai`) meldeten denselben Tab beide als MPRIS-Player an. KDEs Mediensteuerung konnte dadurch nicht mehr zuverlässig entscheiden, welcher Player bedient werden soll. Behoben wurde dies, indem die Mediensteuerung in der Erweiterung deaktiviert wurde. Falls nötig, sollte `active_bit` in Braves Preferences-JSON auf `false` stehen.
3. Der Kernel-Treiber `hid_lg_g15` meldet die Tastendrücke korrekt und darf nicht auf die Sperrliste gesetzt werden. Ein früherer Versuch, unzuverlässige Medientasten mit `hid-generic` zu reparieren, war unnötig, nachdem die Ursachen 1 und 2 gefunden worden waren. Außerdem ist `hid_lg_g15` für die Gaming-Keys-Schnittstelle erforderlich, auf der die LCD-Tasten basieren. Bei Problemen nach Systemänderungen `lsmod | grep hid_lg_g15` und `grep -rl lg_g15 /etc/modprobe.d/` prüfen; die zweite Abfrage sollte leer sein.
4. **Fehlerbericht: „nur die Medienvor- und -zurück-Tasten funktionieren nicht“** (Play/Pause, Lautstärke und Stummschaltung waren in Ordnung). Rohe evdev-Ereignisse auf der Consumer-Control-Schnittstelle bestätigten, dass `KEY_PLAYPAUSE` und `KEY_PREVIOUSSONG` korrekt ausgelöst wurden. Auch `kglobalaccel`-Tastenkürzel für `nextmedia`/`previousmedia` waren korrekt gebunden (`qdbus --literal org.kde.kglobalaccel /component/mediacontrol org.kde.kglobalaccel.Component.allShortcutInfos`), und `playerctl next` übersprang beim direkten Aufruf den Titel. Die Ursache war eine veraltete MPRIS-Anmeldung von Braves `plasma-browser-integration`: Der D-Bus-Name antwortete bei direkter `Properties.Get`-Abfrage zeitweise mit `ServiceUnknown`, obwohl `playerctl -l` den Player weiterhin aufführte. Dadurch war nur das Routing für Vor/Zurück gestört; Play/Pause und Lautstärke nutzten andere Wege. **Lösung: Browser-Tab oder -Seite neu laden**, damit eine frische MPRIS-Sitzung registriert wird. Der Benutzer bestätigte die Lösung sofort. Der Fehler lag nicht im Code dieses Projekts.

## Mehrbildschirmsystem

Die Datei `$XDG_RUNTIME_DIR/g510lcd_screen` enthält eine Zahl:

- `0` – Statistikbildschirm (CPU, Temperatur, VRAM, RAM in dieser Reihenfolge)
- `1` – Uhr (digitale Uhrzeit und Datum links, analoges Zifferblatt rechts; siehe „Analog-Uhr“)
- `2` bis `5` – Testbildschirme „L2“ bis „L5“ (siehe „Tasten“)

`g510_lcd_stats.c` liest die Datei in jedem Schleifendurchlauf (etwa alle 1–2 Sekunden) erneut ein und zeichnet den angegebenen Bildschirm. Nur `g510_lcd_buttons.c` schreibt in diese Datei. Für einen neuen Bildschirm eine `draw_XXX_screen()`-Funktion hinzufügen, in der `main()`-Schleife eine Verzweigung für die zugehörige Nummer einfügen und in `g510_lcd_buttons.c` eine Taste so konfigurieren, dass sie diese Nummer schreibt.

### Analog-Uhr (L1, rechte Seite)

Auf Wunsch sollte der freie Bereich rechts neben der digitalen Uhrzeit und dem Datum mit einem analogen Zifferblatt gefüllt werden. Die Umsetzung erfolgte in mehreren Schritten:

1. Zunächst entstand ein einfacher quadratischer Rahmen (`g15r_pixelBox`) ohne Ziffern, nur mit Zeigern, passend zur kantigen 1-Bit-Darstellung. Die Position wurde gemessen statt geraten: Das echte Uhrbild wurde mit `--preview` gerendert und nach dem äußersten Pixel des digitalen Texts abgesucht; dieser lag bei x=92.
2. Auf Wunsch nach abgerundeten Ecken, römischen Ziffern bei 12, 3, 6 und 9 sowie kleinen Strichen dazwischen wurde `g15r_drawRoundBox` verwendet. Das Zifferblatt wurde von 36 × 36 auf 40 × 40 Pixel (`(107,1)-(147,41)`) vergrößert. Hinzu kamen `XII`, `III`, `VI` und `IX` sowie acht kleine Stundenmarkierungen.
3. Die eingebaute Bitmap-Schrift `G15_TEXT_SMALL` besitzt keine API zum Abfragen der Textbreite. Deshalb wurden die Pixelbreiten aller Ziffern durch Rendern und Auslesen der leuchtenden Pixel bestimmt: `XII` = 11 px, `III` = 11 px, `VI` = 7 px, `IX` = 7 px; die Höhe beträgt jeweils 5 px. Die Position jeder Ziffer wird anhand ihrer eigenen gemessenen Abmessungen auf einem Kreis mit 12 px Radius zentriert.
4. Auf den Wunsch „nur ein bisschen kleiner“ wurden die Zeichen `I`, `V` und `X` statt mit der Bitmap-Schrift direkt mit `g15r_drawLine` gezeichnet (`draw_roman_glyph`/`draw_roman_numeral` im Quellcode). Bei 4 px Höhe sind sie tatsächlich kleiner und auf einem 1-Bit-Display schärfer: `XII` wurde von 11 auf 7 px, `III` von 11 auf 5 px und `VI`/`IX` von 7 auf 5 px Breite verkleinert. Gerade Linien bleiben in dieser Größe klarer als skalierte Bitmap- oder geglättete Zeichen. Die Zentrierung berechnet die tatsächliche Breite aus den gezeichneten Glyphen statt aus einem separat eingetragenen festen Wert.

Die Stundenstriche liegen im Abstand von 15–18 px zum Mittelpunkt, außerhalb des Ziffernbereichs und innerhalb des abgerundeten Rahmens. Die Zeigerlängen (Minutenzeiger 9 px, Stundenzeiger 6 px) bleiben innerhalb des Ziffernradius, sodass sie die Ziffern auch bei :15, :45, 3:00 und 9:00 nicht überdecken. Einen Sekundenzeiger gibt es bewusst nicht – bei dieser Größe wäre er unnötig.

Die Winkelkonvention entspricht dem Rest der Datei: 0 zeigt auf 12 Uhr, positive Winkel laufen im Uhrzeigersinn. Es gilt `dx = round(sin(angle)*len)` und `dy = -round(cos(angle)*len)` (die y-Achse des LCDs zeigt nach unten).

Die Funktion wurde auf `g510s-dev` gebaut und selbst überprüft: `-Wall -Wextra` ohne Warnungen, erfolgreiche Regressionstests, Vorschau über `--preview` und vergrößerte Sichtprüfung. Wie alle Änderungen auf diesem Branch wird sie erst nach physischer Bestätigung an der Tastatur in `main` übernommen oder markiert.

## Tasten L1–L5

Es werden die normalen Linux-Keycodes verwendet; anders als bei den Medientasten war keine HID-Neuzuordnung erforderlich. Jede Taste wurde einzeln geprüft:

- L1 = 696 (`KEY_KBD_LCD_MENU1`) – wechselt zwischen Statistikbildschirm und Uhr. Befindet sich gerade ein Testbildschirm L2–L5 auf dem LCD, führt L1 zur Statistik zurück.
- L2 = 697 (`KEY_KBD_LCD_MENU2`) – zeigt als Test/Platzhalter „L2“ an.
- L3 = 698, L4 = 699 und L5 = 700 – gleiches Verhalten.

Die Schalter prellen: Ein physischer Druck kann zwei bis vier rohe Ereignisse auslösen. `g510_lcd_buttons.c` enthält deshalb eine Entprellung von 400 ms pro Taste; sie darf nicht entfernt werden. L2–L5 bestätigen derzeit nur den Tastendruck. Für eine andere Funktion muss der `else`-Zweig der Hauptschleife in `g510_lcd_buttons.c` angepasst werden (derzeit ruft er nur `write_screen()` und `log_button()` auf).

## Schriftarten

Die Bibliothek enthält `default-00` bis `default-39.fnt`, aber alle verwenden dieselbe Schriftfamilie in verschiedenen Größen; es sind keine unterschiedlichen Schriften. Andere Schriftarten lassen sich mit `g15fontconvert -i font.ttf -o out.fnt -s N -g 1` konvertieren (enthalten im AUR-Paket `libg15render`). `-s` ist **keine** Pixelhöhe, sondern eher eine Punktgröße. Die tatsächlich erzeugte Höhe lässt sich so prüfen:

```sh
python3 -c "import struct; print(struct.unpack('<H', open('FILE.fnt','rb').read()[4:6])[0])"
```

Passe `-s` an, bis die gewünschte Höhe erreicht ist. Bei einem Zeilenabstand von 10 px sollte die Schrift etwa 8–9 px hoch sein.

Dünne oder normale Schriftstärken werden in dieser geringen Auflösung unleserlich, weil nicht genügend Pixel für feine Linien vorhanden sind. Verwende deshalb nur Bold oder Black. Aktuell werden Beschriftungen mit der beigefügten `fonts/source-ttf/FONT.otf` gezeichnet, die stilistisch der originalen G510s-LCD-Schrift ähnelt. Zahlen verwenden die eingebaute `G15_TEXT_SMALL`-Schrift: Mehrere konvertierte Schriften sahen dafür schlechter aus; die eingebaute Schrift wurde für genau diese Auflösung erstellt.

Bitmap-Schriften im Format `.otb` oder `.pcf` (zum Beispiel Terminus) funktionieren mit `g15fontconvert` nicht. Das Programm erzeugt dabei unbemerkt eine leere oder beschädigte `.fnt`-Datei: `font_height` bleibt unabhängig von `-s` gleich und die Datei ist nahezu leer. Nur skalierbare TTF-/OTF-Konturschriften werden korrekt konvertiert.

**Lizenzhinweis (bekannte und bewusst akzeptierte Entscheidung):** Die eingebetteten Metadaten in `FONT.otf` verweisen auf eine Adobe-Typekit-EULA (`typekit.com/eulas/...`) und nicht auf eine freie Lizenz. Wahrscheinlich war die Datei nicht zur freien Weitergabe bestimmt; sie ist hier dennoch enthalten. Als technische Rückfallebene – nicht als Lizenzkorrektur – liegt zusätzlich `fonts/source-ttf/FALLBACK.ttf` bei (Liberation Sans Bold, tatsächlich unter der SIL Open Font License; Lizenztext in `fonts/source-ttf/FALLBACK.LICENSE.txt`). `g510_lcd_stats` wechselt automatisch zu dieser Schrift, falls `FONT.otf` nicht geladen werden kann oder eine Glyphenprüfung fehlschlägt. Siehe `font_sanity.h` und den Abschnitt über den beschädigten S-Glyphen oben.

`install.sh` und `scripts/rebuild.sh` konvertieren beide Schriften mit `g15fontconvert` automatisch. Das Programm gehört zum AUR-Paket `libg15render`; bei einer Neuinstallation ist kein manueller Schritt erforderlich.

## Beleuchtung (RGB-Tastaturbeleuchtung)

Die Beleuchtung ist ein echtes LED-Gerät des Linux-Kernels und keine eigene hidraw-/USB-Schnittstelle: `/sys/class/leds/g15::kbd_backlight/`. `brightness` nimmt laut Meldung Werte von 0 bis 255 entgegen; `multi_intensity` enthält RGB-Werte als durch Leerzeichen getrennte Zahlen von 0 bis 255. KDes Option „Keyboard Colour: Follow accent colour“ schreibt in dieselbe Datei. Deaktiviere sie in den Systemeinstellungen unter **Brightness & Color**, wenn manuelle Änderungen erhalten bleiben sollen.

Die udev-Regel wendet `set-backlight-color.sh` beim Systemstart oder erneuten Anschließen automatisch an. Die Farbe wird im Tab **Backlight + G-Keys** von `g510_app.py` mit einem Auswahlfeld, **Apply** und **Set as Default** geändert; Root-Rechte sind dafür nicht erforderlich. Bei **Apply** schreibt `apply_backlight()` in `g510_app.py` `set-backlight-color.sh` mit der neuen Auswahl neu. Damit wird diese Farbe automatisch zur dauerhaften Startfarbe, nicht nur zur einmaligen Live-Einstellung. Die Gehäusefarbe im Canvas wird unmittelbar angepasst.

**Helligkeitsregler am 15.09.2026 entfernt; das Problem ist nachweislich ein echter Fehler:** Die Notiz vom 14.09.2026 in [`G510_CANVAS_PLAN.md`](G510_CANVAS_PLAN.md) vermutete noch unzuverlässiges Zurücklesen. Direkte Tests bestätigten, dass Schreibversuche mit 50, 128, 200, 255 und 100 jedes Mal sofort 255 zurücklieferten. Dies ist eine Einschränkung des Kernels/Treibers auf dieser Hardware, kein Timingproblem: `max_brightness=255` suggeriert einen funktionierenden Bereich von 0 bis 255, den es praktisch nicht gibt. Der Regler bewirkte nichts und wurde entfernt. Die Anwendung verwendet nun immer `MAX_BRIGHTNESS = 255`, den einzigen tatsächlich wirksamen Wert. Schreibvorgänge für die Farbe (`multi_intensity`) funktionieren weiterhin zuverlässig und wurden nicht verändert.

Für Farben wird ein Auswahlfeld mit Voreinstellungen (`COLOR_RGB`) statt eines Live-Farbwählers verwendet. Ein Qt-Farbwähler wäre hier technisch möglich (anders als der Absturz mit `CLR+SCL` im alten yad-Skript; siehe Branch `legacy-yad-backlight-script`), wurde aber bisher nicht umgesetzt. Weitere Voreinstellungen können im `COLOR_RGB`-Dictionary am Anfang von `g510_app.py` ergänzt werden.

## Dauerhafte Einrichtung

`99-g510-lcd.rules` (installiert unter `/etc/udev/rules.d/`) erledigt drei Dinge:

1. Erstellt `/dev/g510-lcd` als stabile symbolische Verknüpfung zur jeweils vergebenen hidraw-Gerätenummer der LCD-USB-Schnittstelle. Diese Nummer kann sich bei Neustarts und erneutem Anstecken ändern; ein konkretes `hidrawN` darf daher nicht fest codiert werden.
2. Erstellt auf dieselbe Weise `/dev/g510-keys` für das Eingabegerät der Gaming-Keys.
3. Ändert beim Erscheinen der Beleuchtungs-LED Gruppen- und Dateiberechtigungen auf die Gruppe `input` (damit zur Laufzeit kein `sudo` nötig ist) und führt `set-backlight-color.sh` aus.

Die beiden systemd-Benutzerdienste sind aktiviert und starten bei jeder Anmeldung. `hid_lg_g15`, der benötigte Kernel-Treiber, gehört zum Hauptkernel und ist in jedem Manjaro-Kernelpaket enthalten. Das Projekt hängt daher nicht von einer bestimmten Kernelversion ab.

**Schaltfläche „Save All Settings Now“:** Auf Wunsch wurde eine sichtbare Speicherfunktion ergänzt, weil der Benutzer nicht darauf vertraute, dass Einstellungen Neustarts überstehen. Jede Änderung – etwa das Verschieben/Skalieren eines Custom-Screens-Elements, **Apply**, **Set as Default** oder eine Makroaufnahme – wurde bereits direkt in `DATA_DIR` gespeichert. Es fehlte kein Speicherpfad, sondern eine sichtbare Bestätigung. Die Schaltfläche in der oberen Werkzeugleiste (in beiden Tabs sichtbar) speichert die aktuelle Custom-Screens-Konfiguration und die Standard-Beleuchtungsfarbe erneut. Danach liest sie beide Dateien wieder ein und vergleicht den Inhalt mit den erwarteten Werten. So wird ein echter Schreib-/Lese-Rundlauf geprüft, statt lediglich anzunehmen, dass der Schreibaufruf erfolgreich war. Das Ergebnis – einschließlich eines Fehlers – erscheint in der Statusleiste.

## Medieninformationen und Audio-Visualizer (L2–L5)

Drei Sensorschlüssel (`MEDIA_TITLE`, `MEDIA_ARTIST`, `MEDIA_ELAPSED`) und die Elementart `visualizer` lassen sich im Custom-Screens-Editor wie Sensoren, Text oder Bilder hinzufügen.

### Medieninformationen

`update_media_info()` in `src/g510_lcd_stats.c` ruft einmal pro Abfragezyklus `playerctl` auf (Paket aus Arch `extra`, in den Abhängigkeiten von `install.sh`) und verwendet MPRIS. Beim Testen wurden folgende leicht zu übersehenden Details festgestellt:

- Ein Titel kann ein echtes Zeichen `|` enthalten; ein getesteter YouTube-Titel lautete `"Müneccim | YouTube Music"`. Deshalb verwendet die gemeinsame `--format`-Abfrage als Feldtrenner das ASCII Unit Separator-Zeichen (`\x1f`) statt `|`.
- Die Felder `{{ position }}` und `{{ mpris:length }}` im Formatstring von `playerctl` verwenden **Mikrosekunden**. Der separate Unterbefehl `playerctl position` gibt dagegen **Sekunden** zurück. Beide Werte wurden direkt getestet und dürfen nicht gleichgesetzt werden.
- Die Auswahl des Players ist wichtig, und je nach Feld unterschiedlich. Ohne `-p` wählt `playerctl` den ersten Player in seiner Prioritätsreihenfolge. Auf einem KDE-System mit Brave und YouTube Music lieferte Braves MPRIS-Datenquelle nur den allgemeinen Tabtitel („YouTube Music“) und keinen Interpreten. `plasma-browser-integration`, KDes Medienbrücke für den Browser, lieferte dagegen echten Titel, Interpret und Album.
- Für `position` und `mpris:length` galt in einem Live-Test das Gegenteil: Bei automatisch weiterlaufenden Wiedergabelisten zählte die Position von `plasma-browser-integration` über zehn Minuten hinaus und die Gesamtlänge nahm in jedem Abfragezyklus weiter zu. Braves MPRIS-Datenquelle basiert direkt auf `currentTime`/`duration` des echten `<video>`-Elements und blieb innerhalb der realen Titellänge.
- Die Lösung verwendet eine einzige Abfrage `playerctl -a metadata` (ein Unterprozess statt zwei), die alle laufenden Player zeilenweise mit `{{ playerName }}` ausgibt. `update_media_info()` bezieht Titel und Interpret – sofern verfügbar – aus `plasma-browser-integration`, Position und Länge dagegen aus einer anderen, nicht auf der Browser-Brücke basierenden Zeile. Falls es nur einen Player gibt, verwendet es diese Quelle für beide Werte. Spotify, VLC und andere Fälle ohne getrennte Browser-Bridge bleiben unverändert; sie werden weiterhin durch den echten Ende-zu-Ende-Test `bugcheck_media_sensors.py` abgedeckt.

### Audio-Visualizer

Der Visualizer zeigt einen Live-Audio-Equalizer und ist auf ein Element pro Bildschirm begrenzt (`MAX_VISUALIZERS` im C-Quellcode), weil es pro Prozess genau einen gemeinsamen Audio-Aufnahmestrom gibt.

- **Von Playern unabhängig:** Er erfasst über `parec` den Monitor des Standardausgabegeräts von PipeWire/PulseAudio – also den tatsächlich ausgegebenen Systemton, nicht den Stream einer einzelnen Anwendung. Damit funktioniert er gleichermaßen mit Brave, Spotify, VLC und anderen Programmen, ohne anwendungsspezifischen Code.
- **Fehler in der Aufnahme erkannt und behoben:** Die erste Version leitete die Ausgabe von `parec` über `popen()` aus einer Pipe. Ein anonymer Pipe-Kanal und eine benannte FIFO lieferten nachweislich dauerhaft nur Null-Bytes, obwohl `read()`/`fread()` Erfolg und die korrekte Länge meldeten. Derselbe Befehl lieferte bei Ausgabe in eine normale Datei zuverlässig echte Audiodaten. Ursache: `parec`/PipeWire-pulse verwendet einen deutlich größeren Flush-Puffer, wenn die Ausgabe als Pipe erkannt wird. Das lässt sich nicht auf der Leseseite beheben. Die aktuelle Version startet `parec` mit echtem `fork()`/`execlp()` und verfolgt die PID des Kindprozesses. Damit wird kein anfälliges `pkill`-Muster verwendet – ein dauerhaftes Projektprinzip, nachdem es damit zuvor Probleme gab. Die Ausgabe geht in eine Datei unter `$XDG_RUNTIME_DIR`, die alle 30 Sekunden (`VIZ_RESTART_SEC`) erneuert wird, um den Speicherverbrauch zu begrenzen. Ein `SIGTERM`-/`SIGINT`-Handler beendet den Kindprozess beim Stoppen des Daemons; ohne ihn blieben nach jedem Dienstneustart verwaiste `parec`-Prozesse zurück. Dieser Fehler wurde beim Testen gefunden und behoben.
- Anzahl der Balken und Segmente werden dynamisch aus der tatsächlichen Elementbreite und -höhe sowie einer festen kleinen Pixelgröße pro Balken/Segment berechnet. Eine Vergrößerung zeigt dadurch mehr Frequenzdetails statt nur größere Balken.
- Eine segmentierte Nulllinie mit denselben Abständen wie die Balken erstreckt sich unabhängig von Position und Größe des Elements über alle 160 Displaypixel. So ist erkennbar, dass der Visualizer vorhanden, aber gerade ohne Ausschlag ist.
- Verwendet wird eine kleine selbst geschriebene Ein-Bin-DFT (`viz_dft_bin_magnitude()`, ähnlich Goertzel), die nur die etwa 20 benötigten Frequenzbereiche auswertet. FFTW ist auf dem System verfügbar, wäre für diese kleine Funktion aber eine unverhältnismäßig große Abhängigkeit.
- `VIZ_SCALE` (rohe DFT-Amplitude zu Balkenhöhe) ist ein kalibrierter fester Wert. Lautstärke und Mastering unterscheiden sich zwischen Musiktiteln so stark, dass kein einzelner Wert alle Titel perfekt abdeckt. Wirkt ein Titel zu flach oder sind die Balken ständig am Maximum, ist das die bekannte Abwägung. Eine automatische Verstärkungsregelung könnte helfen, wäre aber eine größere Änderung.
- Die Vorschau über `--preview`, die der GUI-Editor einmalig startet, liest dieselbe Live-Aufnahmedatei wie der dauerhafte Daemon. Im Custom-Screens-Tab ist dadurch ein echter Live-Schnappschuss des aktuellen Tons sichtbar; ein separater Vorschaupfad war nicht nötig.
- Die Balken sind 4 px breit (`bar_w`), zuvor 2 px. Diese bewusste Verdoppelung folgte auf den Live-Test, bei dem die Balken zu dünn und spärlich wirkten. Zuerst wird die über die volle Breite reichende segmentierte Nulllinie gezeichnet, damit jede Spalte auch bei inaktiven Balken eine Nullmarkierung hat.
- Die Abstimmung erfolgte mit echter laufender Musik, nicht mit künstlichen Testtönen: Die Zahl der DFT-Bins (`VIZ_NUM_BARS`) wurde erhöht und die Glättung so geändert, dass der neue Messwert stärker gewichtet wird (`0.85` neu / `0.15` alt, vorher `0.7` / `0.3`). Dadurch reagieren die Balken sichtbar im Takt, statt langsam nachzuziehen. Der Wert ist ein empirischer Kompromiss, keine allgemeingültige Konstante; für andere Musik gilt dieselbe Abwägung wie bei `VIZ_SCALE`.
- **Verzögerung erkannt und behoben:** Auf Wunsch nach schnellerer Reaktion wurde die Größe der Aufnahmedatei alle 50 ms gemessen. Sie wuchs nur in Paketen von etwa 65 KB (rund 0,7 Sekunden Ton) alle 0,6–0,7 Sekunden. Jeder Balken nutzte dadurch bis zu 0,7 Sekunden alte Audiodaten; die Zeichenschleife selbst war nicht die Hauptursache. Der Test mit `parec --latency-msec=50` an einer temporären Datei zeigte, dass die Schreibvorgänge nun in kleinen Paketen fast bei jedem Abfragezyklus eintreffen. Danach wurde auch die nur für den Visualizer geltende Neuzeichnungsrate auf 50 ms verdoppelt (zuvor 100 ms; etwa 20 statt 10 Bilder pro Sekunde in der Hauptschleife von `g510_lcd_stats.c`).
- Größen der Live-Elemente liegen in den benutzereigenen Daten `~/.local/share/g510-lcd/custom_screens.txt` und werden nicht eingecheckt. Die mitgelieferten Beispielbildschirme verwenden als Ausgangspunkt ein breiteres Element (116 × 15 statt 84 × 13); Verschieben und Skalieren funktionieren unverändert.

### Textüberlauf und abgeschnittene Texte

Die eingebauten Bitmap-Schriften von G15 (`G15_TEXT_SMALL/MED/LARGE/HUGE`) besitzen keine API zum Abfragen der Textbreite. Lange Sensorwerte wie Songtitel und Interpreten oder freie Textelemente konnten deshalb über den Rand des 160-Pixel-Displays hinausgezeichnet werden. Zwei Hilfsfunktionen in `src/g510_lcd_stats.c` beheben das:

- `measure_builtin_text_width()` zeichnet den Text auf ein unsichtbares temporäres Canvas und sucht das am weitesten rechts liegende leuchtende Pixel. Das ist dieselbe Messmethode, die an anderen Stellen im Projekt verwendet wird, weil die Bitmap-Schrift keine andere Abfragemöglichkeit bietet.
- `truncate_builtin_text()` kürzt den Text zeichenweise und hängt einen Punkt an, bis die gemessene Breite in den verfügbaren Platz passt. Die Funktion ist sowohl für Zahlenwerte bei Sensoren mit Zahl- und Balkendarstellung als auch für frei eingegebene Textelemente angebunden.

Stresstests umfassten alle Schriftgrößen, schwierige Unicode-Zeichenfolgen und verfügbare Breiten von negativen Werten bis zur vollen Displaybreite. Die Kürzung überschritt den vorgesehenen Pixelbereich nie. Eine exakt bis zum Rand reichende Zeile wirkt auf dem echten LCD dennoch abgeschnitten. Deshalb zieht `TEXT_EDGE_MARGIN` (3 px) von jedem verfügbaren Breitenbudget ab und lässt einen sichtbaren Abstand zum Rand.

### Treffgenauigkeit der Auswahlflächen (`value_x2`)

Die `.meta`-Begleitdatei teilt der Python-Oberfläche mit, wo sich die anklick- und ziehbaren Begrenzungen der Elemente befinden. Früher wurde die Breite eines Sensorwerts pauschal mit `+30px` geschätzt; bei Balken endete die Auswahlfläche am Balken selbst und berücksichtigte den anschließenden Zahlenwert nicht. Breite Werte – insbesondere Songtitel, aber auch beliebige andere Sensoren – lagen dadurch teilweise außerhalb der Auswahlfläche. Nun schreibt die C-Seite den tatsächlich gemessenen Wert `value_x2` in die `.meta`-Datei. `_element_rect()` in `src/g510_app.py` verwendet diesen echten Messwert statt einer Schätzung.

### Entfernen-Schaltfläche war nicht mehr erreichbar

Eine Gruppierungszeile „Other elements:“ und längere Sensorbezeichnungen wie `"Media: Song Title – <value>"` konnten über die feste Breite des Seitenbereichs von 230 px hinausragen und die Schaltfläche **✕** abschneiden. Sie existierte noch im Widget-Baum, war aber nicht mehr erreichbar. Die Korrektur verwendet `QFontMetrics(...).elidedText(...)`: Die verfügbare Beschriftungsbreite wird aus Panelbreite, Entfernen-Schaltfläche, möglicher Größenschaltfläche und festen Rändern berechnet. Zu lange Texte werden mit `...` verkürzt; der vollständige Text bleibt als Tooltip erhalten. `CUSTOM_SCREENS_PANEL_WIDTH` ist nun eine benannte Konstante statt einer fest codierten Zahl.

### Titel und Interpret automatisch zentrieren

Auf Wunsch werden Songtitel und Interpreten unabhängig von ihrer wechselnden Länge immer zentriert. `MEDIA_TITLE` und `MEDIA_ARTIST` sind in `SENSORS[]` mit `auto_center` markiert. `draw_element()` kürzt sie anhand der verfügbaren Breite in der Mitte des gesamten Displays – nicht nur anhand des Platzes rechts von der gespeicherten x-Position – und berechnet bei jedem Zeichnen `x = (G15_LCD_WIDTH - value_w) / 2` neu. Die y-Position bzw. Zeile bleibt dort, wohin das Element verschoben wurde; nur x wird automatisch zentriert. Stresstests von einem Zeichen bis zu einem maximal gekürzten Text mit etwa 97 Zeichen zeigten, dass der Text nie über den Rand hinaus zentriert wird.

## Tatsächlich aufgetretene Besonderheit

Ein Testskript öffnete einmal `fopen("/dev/hidrawN", "wb")`, während das echte Gerät bei einer Neuanmeldung kurzzeitig nicht vorhanden war. Der Aufruf erzeugte stillschweigend eine **reguläre Datei** namens `hidrawN` unter `/dev`, statt einen Fehler zu melden. Diese Datei verhinderte dauerhaft, dass der Kernel an diesem Pfad das echte Zeichengerät anlegte. Symptome: Berechtigungen sehen korrekt aus, aber Schreibvorgänge schlagen fehl oder udev-Regeln scheinen zu greifen und anschließend wieder aufgehoben zu werden. Mit `stat /dev/hidrawN` prüfen, ob dort eine reguläre Datei statt eines Zeichengeräts liegt; falls ja, die Datei mit `sudo rm` entfernen und die Tastatur erneut anschließen.

## udev-Regel: wichtige Besonderheit

Werte aus `ATTRS{}` verschiedener Gerätevorfahren dürfen nicht in derselben Regelzeile kombiniert werden. Zum Beispiel liegt `ATTRS{idVendor}` am USB-Gerät, `ATTRS{bInterfaceNumber}` aber an der USB-Schnittstelle. Sobald udev einen Vorfahren für einen `ATTRS{}`-Ausdruck ausgewählt hat, muss derselbe Vorfahr für die übrigen `ATTRS{}`-Ausdrücke passen. Die Regel schlägt andernfalls still fehl. Wo möglich, `ENV{}`-Eigenschaften verwenden: `ENV{ID_VENDOR_ID}`, `ENV{ID_MODEL_ID}` und `ENV{ID_USB_INTERFACE_NUM}` sind bereits am zu prüfenden Gerät aufgelöst. Die hidraw-Regel verwendet dieses zuverlässige Muster.
