# G510s Plan A – von `g510s-dev` zur bestätigten, zusammenführbaren und installierbaren Anwendung

Verfasst am 15.09.2026 auf dem Rechner mit angeschlossener G510s (`g510s-dev`, laut `BRANCHES.md` der maßgebliche Branch für diese Arbeit). Dies ist ein fortlaufender Fahrplan, keine einmalige Aufgabenliste. Künftige Sitzungen sollen die Datei aktualisieren, wenn Phasen abgeschlossen werden oder neue Lücken auftreten – wie bei `G510_CANVAS_PLAN.md` und `G510_DRAW_ON_DISPLAY_PLAN.md`. Die Angaben beruhen auf echtem Git-Verlauf, tatsächlichen Dateiinhalten, `journalctl`-Ausgaben und `lsusb -v` für die angeschlossene Hardware, nicht nur auf Dateinamen oder Commit-Betreffzeilen.

## Aktueller Stand dieses Branches

`g510s-dev` liegt drei Commits vor `main` und wurde noch nicht zusammengeführt:

```text
caf5be0 Document phase-1 polish status in G510_README.md
7807781 Phase 1 polish: Custom Screens discoverability fixes
0d7de14 Harden image-import path found during post-launch audit
```

Die Prüfung mit `git fetch` und `git log main..g510s-dev` ergab keine weitere Abweichung und keine nicht eingecheckten Codeänderungen. Die einzige lokale Änderung an `custom_screens.txt` stammt vom Benutzer, der gerade sein echtes LCD-Layout bearbeitet – das ist erwartungsgemäß. Nichts davon ist veraltet oder gefährdet.

Vier Arbeitspakete sind **implementiert und selbst getestet, aber am echten LCD der G510s noch nicht physisch bestätigt**. Gemäß der wiederholt festgehaltenen Projektregel (siehe `G510_CANVAS_PLAN.md` und `G510_README.md`) gilt nichts als abgeschlossen, bevor der Benutzer das Ergebnis auf der echten Hardware bestätigt.

1. **Custom Screens v1.1** – Sensordashboard für L2–L5 mit mehr als 20 Sensoren, frei platzierbaren Elementen, skalierbaren Balken und PNG-Bildimport. Die Implementierung wurde in dieser Sitzung vollständig nachvollzogen: Die Vorschau ruft das echte kompilierte Programm `g510_lcd_stats --preview` auf. Dieses schreibt eine `.meta`-Begleitdatei mit den exakten Pixelgrenzen jedes Elements, basierend auf echten Schriftmetriken. Vorschau und Treffererkennung für Ziehen/Skalieren sind somit nachweislich pixelgleich mit der LCD-Anzeige, keine Python-Nachbildung oder geschätzte Anordnung. Das verbleibende Risiko betrifft Eigenheiten der echten Hardware (Kontrast, Aktualisierungszeit, Gefühl der Tasten), nicht die Genauigkeit der Vorschau.
2. **Canvas-Umbau** – Beleuchtung und G-Tasten befinden sich in einem Tab. Die tastaturförmige, mit `QPainter` gezeichnete Oberfläche `g510_canvas.py` ersetzt das frühere Tastenraster. Eine MR-Anzeige wurde ergänzt; sie zeigt den LED-Zustand, ist aber nicht anklickbar, da der Daemon keinen globalen „Aufnahme umschalten“-Modus besitzt.
3. **PNG-/Bildimport** für Custom Screens über `png-to-lcd.py` mit Floyd-Steinberg-Dithering und Größenanpassung ohne Vergrößerung.
4. **Feinschliff der Phase 1** (`7807781`) – Tooltips erklären die deaktivierten L1-Steuerelemente; zwei direkt nacheinander importierte Bilder werden versetzt platziert, statt genau übereinander zu liegen; eine doppelte Konstante wurde entfernt.

`0d7de14` behebt bereits einen echten Fehler beim Bildimport und wartet nicht mehr auf die Umsetzung: Im Konfigurationsparser von `g510_lcd_stats.c` wird jeder mit `strncpy()` befüllte Puffer ausdrücklich terminiert, da ein nicht terminierter Wert später an `fopen()` übergeben würde. Vor dem Akzeptieren einer `IMAGE`-Zeile werden jetzt außerdem sowohl Breite als auch Höhe geprüft. Die Änderung entspricht der gleichzeitigen Korrektur in `png-to-lcd.py`, das zuvor nur die Breite begrenzte. Dieser Fix muss nicht wie die visuellen Funktionen erneut physisch bestätigt werden; er ist hier aufgeführt, weil er im selben noch nicht zusammengeführten Bereich gelandet ist.

## Phase 0 – Bestätigung an echter Hardware (Voraussetzung für alles Weitere)

Der tatsächliche Engpass sind fehlende Hardwaretests, nicht fehlender Code. Alle vier oben aufgeführten Punkte müssen auf der physischen G510s geprüft werden:

- **Custom Screens v1.1:** L2–L5 mit echten Tastendrücken durchschalten, nicht nur die GUI-Vorschau ansehen. Die Entprellung von 400 ms in `g510_lcd_buttons.c` wurde eingeführt, nachdem ein einzelner realer Tastendruck zwei bis vier Ereignisse ausgelöst hatte. Daher sollte das Verhalten der physischen Taste nicht ungeprüft als zuverlässig gelten. Prüfen, ob jeder Sensor korrekt abgelesen wird, ob Verschieben/Skalieren in der GUI der LCD-Anzeige entspricht und ob der PNG-Import beim echten LCD-Kontrast gut aussieht. Die Umwandlung zu 1-bpp-XBM kann auf dem Bildschirm anders wirken als auf einem echten monochromen LCD.
- **Canvas-Umbau:** Prüfen, ob der zusammengeführte Tab korrekt angezeigt wird, G-Tasten-Klicks weiterhin den Makrodialog öffnen, M-Tasten-Klicks weiterhin das Profil wechseln und die Gehäusefarbe der tatsächlichen Beleuchtung entspricht.
- **Feinschliff der Phase 1:** Prüfen, ob die neuen Tooltips angezeigt werden und ob zwei nacheinander importierte Bilder nicht mehr exakt übereinander liegen.

Gefundene Probleme müssen mit der echten Hardware behoben werden. Dabei gilt das im Repository etablierte Muster der Eigenprüfung. `G510_DRAW_ON_DISPLAY_PLAN.md` beschreibt es: bauen, selbst testen und einen echten Fehler vor der Freigabe entdecken und beheben. Bei diesem Prozess wurde beispielsweise festgestellt, dass ein Demobildschirm mit einem veralteten `/tmp`-Programm statt mit dem echten Produktionsprogramm erzeugt worden war. Vor Abschluss dieser Phase darf nichts nach `main` übernommen und Phase 2 nicht begonnen werden.

## Phase 1 – Bekannte Eigenheit des Helligkeitsreglers untersuchen

Der Zwischenwerte des Helligkeitsreglers lassen sich aus sysfs (`/sys/class/leds/g15::kbd_backlight/`) nicht zuverlässig zurücklesen. Zu klären wäre, ob es sich um ein Timingproblem beim Lesen, einen sysfs-Cache oder eine tatsächliche Begrenzung/Rundung von Zwischenwerten in der Kernel-LED-Klasse handelt. Diese Frage wurde hier nicht weiter untersucht, da die gezielte Untersuchung selbst die Aufgabe dieser Phase sein sollte.

**Aktualisierung nach späterer Prüfung:** Wie in `G510_README.md` dokumentiert, wurde das Problem am 15.09.2026 untersucht. Schreibversuche mit mehreren Werten wurden immer als 255 zurückgelesen. Der Helligkeitsregler wurde daher entfernt; ein weiterer offener Untersuchungsbedarf besteht in dieser Form nicht.

## Phase 2 – G510s-spezifische Paketierungslücken schließen – ERLEDIGT am 15.09.2026

Die Punkte stammen aus `READY_FOR_ANYONE.md`, das in diesem Durchlauf in diesen Branch übernommen wurde. Alle drei zuvor klar abgegrenzten Lücken wurden nicht nur im Code geändert, sondern tatsächlich überprüft:

1. ~~Der Schritt für Desktop-Verknüpfungen in `install.sh` änderte mit `chmod +x` nur Dateien, die zuvor von Hand angelegt werden mussten.~~ Behoben: Das Skript erzeugt nun alle fünf Starter anhand des ermittelten `$DIR`, nach dem bereits funktionierenden Muster aus `install-g910.sh`.
2. ~~`scripts/start.sh` ruft `zenity` auf, aber `install.sh` prüfte oder installierte es nicht.~~ Behoben: `zenity` wurde in die Abhängigkeitsliste aufgenommen.
3. ~~Desktop-Starter wurden nur unter `~/Desktop`, nicht aber unter `~/.local/share/applications` gespeichert.~~ Behoben: Sie werden nun an beiden Orten angelegt.

Geprüft wurde nur die Erzeugungslogik der Starter, nicht der vollständige Installer, der unabhängige Änderungen an `sudo`, `pacman`, udev oder systemd auslösen würde. Die neu erzeugten Dateien wurden mit den funktionierenden Dateien auf diesem Rechner verglichen. Der einzige Unterschied war die Quotierung des `Exec=`-Pfads, eine echte Robustheitsverbesserung und keine Regression. Der Start der Anwendung über diesen Befehl wurde bestätigt. Der G510s-Abschnitt von `READY_FOR_ANYONE.md` wurde im selben Branch an die Formulierung „in diesem Durchlauf erledigt und überprüft“ des G910-Abschnitts angepasst.

**Außerhalb des ursprünglichen Phasenumfangs in derselben Nacht gefunden und behoben:** Eine neue Suche über jedes eingecheckte Repository-Element nach Benutzername, Rechnername und Home-Pfad fand noch einen übersehenen Treffer: `DISK_FRIGIDER_PATH` in `g510_lcd_stats.c` war eine Compile-Time-Konstante mit fest codiertem Benutzernamen. Der Pfad wird nun wie bei `button_log_path()` in `g510_lcd_buttons.c` zur Laufzeit aus `$USER` gebildet. Erfolgreich geprüft wurden der Compile, die `--preview`-Darstellung, die vollständige Regressionstestsuite, Neubau des Produktionsprogramms und Neustart des Dienstes. Eine erneute Suche in allen eingecheckten Dateien fand keine Benutzer-/Rechnernamen oder Home-Pfade mehr.

## Phase 3 – Mit `main` zusammenführen

Nach Abschluss und Bestätigung der Phasen 0–2 wird `g510s-dev` in `main` übernommen. In `BRANCHES.md` wird die frühere Zusammenführung der G910-Änderungen als konfliktfrei beschrieben, da die Entwicklung beider Tastaturen jeweils die Dateien der anderen nicht berührte. Das sollte beim tatsächlichen Zusammenführen trotzdem erneut geprüft werden; ein früherer konfliktfreier Merge ist kein Beweis für den nächsten.

`main` liegt aktuell 52 Commits vor dem Stand, den dieser Branch bisher gesehen hat. Laut `git fetch` besteht der Unterschied größtenteils aus der Zusammenführung von G910-Arbeit und betrifft keine G510s-Dateien. Dies wurde in dieser Sitzung geprüft und nicht nur angenommen. Vor einer Zusammenführung ist deshalb ein echter Merge-/Rebase-Review sinnvoll; es ist keine bloße Formalität.

## Phase 4 / „Option B“ – AUR-Paketierung (Zusatzumfang, noch nicht begonnen)

Die Bezeichnungen „Plan B“ und „Option B“ beziehen sich auf dieselbe Phase; der Benutzer bestätigte das am 15.09.2026 direkt.

`READY_FOR_ANYONE.md` hat die Paketierung bereits anhand eines echten installierten Beispiels untersucht: `solaar`, ein Logitech-HID++-Programm mit PyQt-Oberfläche und benötigter udev-Regel. Ein richtiges Paket verwendet ein PKGBUILD, legt Dienste unter `/usr/lib/systemd/user/` und udev-Regeln unter `/usr/lib/udev/rules.d/` ab und überlässt die Abhängigkeitsauflösung `depends=`, statt sie wie heute in `install.sh` selbst zu implementieren. Damit wechselt die Installation von „Repository klonen und `install.sh` ausführen“ zu „`yay -S`“.

**Hindernis durch Identität im Verlauf – durch Entscheidung des Benutzers geklärt, nicht technisch bereinigt:** Für eine echte AUR-Veröffentlichung braucht es ein öffentliches Quell-Repository. Der bisherige Git-Verlauf enthält jedoch in fast jedem Commit die echte E-Mail-Adresse des Benutzers in `Co-Authored-By:`-Zeilen. Eine Bereinigung wäre nur durch Umschreiben eines Verlaufs möglich, der auf mehreren aktiven Branches und Rechnern geteilt wird. Der Benutzer entschied am 15.09.2026 ausdrücklich und nach direkter Rückfrage, dieses Problem nicht im aktuellen Repository zu lösen. Die Entwicklung soll hier normal durch die Phasen 0–3 weitergehen. Wenn die Anwendung wirklich fertig ist, erstellt der Benutzer selbst ein frisches Repository für die öffentliche Veröffentlichung und das AUR. Die Paketierung (PKGBUILD usw.) kann hier entwickelt und getestet werden; die Veröffentlichung erfolgt später aus dem neuen Repository. Dasselbe Hindernis und dieselbe Entscheidung gelten für die entsprechende Phase auf der G910-Seite.

**Technisches Hindernis vor dem Schreiben eines PKGBUILD entdeckt (15.09.2026):** Das Pfadmodell dieser Anwendung setzt voraus, dass sich alles unter einem einzigen `PROJECT_DIR` befindet. Dieser Pfad wird beim Kompilieren mit `-DPROJECT_DIR` in das C-Programm eingebettet. Schriftart, `custom_screens.txt`, `macros.json` und `custom_screen_images/` befinden sich ebenfalls im selben Verzeichnis wie der Programmcode. Ein echtes Arch-Paket kann das nicht unverändert übernehmen: Programmdateien gehören nach `/usr/lib/g510-lcd/` (Root-Eigentum, schreibgeschützt und bei jedem Upgrade ersetzt). `custom_screens.txt`, `macros.json` und importierte Bilder sind dagegen persönliche Daten und müssen Paket-Upgrades überstehen. Sie müssten beispielsweise nach `~/.config/g510-lcd/` oder `~/.local/share/g510-lcd/` verschoben werden.

Das erfordert echte Codeänderungen: neue Pfadauflösung in `g510_lcd_stats.c` und `g510_app.py` sowie eine Migration beim ersten Start für Daten am bisherigen Speicherort. Genau dieser Pfadcode führte am selben Abend bereits zu zwei echten Problemen: ein `PROJECT_DIR`-Compile-Time-Fallback, der noch einen Benutzernamen enthielt, und der hart codierte Benutzerpfad `DISK_FRIGIDER_PATH`. Eine schnelle PKGBUILD-Umhüllung wäre daher riskant; außerdem sollte die Änderung nicht erfolgen, wenn der Benutzer schläft und sie nicht an der echten Tastatur prüfen kann. Die Migration soll als eigene sorgfältige Teilphase mit Bauen, Überprüfen und Bestätigen behandelt werden, sobald Phase 0 abgeschlossen und Phase 4 tatsächlich begonnen wurde.

**Ein echtes Referenzpaket existiert inzwischen in `main`** (15.09.2026, `177ebee`): Die G910-Seite erstellte und installierte ein echtes PKGBUILD (`packaging/g910-control/`). Es wurde anhand der installierten Solaar-Struktur verglichen und mit `makepkg`/`pacman -U` gebaut und installiert. Dabei wurden zwei weitere echte Fehler gefunden und behoben: eine Versionsabweichung und eine im erstellten Paketartefakt verbliebene persönliche Kennung in einem Kommentar (nicht im Quellbaum). Für diese Phase gilt ebenfalls vorläufig `sha256sums=SKIP`, solange das Repository privat ist und ein öffentliches Tag fehlt. Vor der oben beschriebenen `PROJECT_DIR`-Migration ist die G910-Umsetzung lesenswert. Da die G510s kompilierte C-Programme besitzt, die bei der G910 fehlen, lässt sich die Verzeichnisstruktur nicht direkt kopieren. Das Muster – `packaging/<paketname>/`, Vergleich mit einem echten installierten Referenzpaket und tatsächliches Bauen/Installieren statt bloßer Schreibarbeit – ist jedoch der passende Qualitätsmaßstab.

Phase 4 bleibt ein Zusatzwunsch und blockiert die Phasen 0–3 nicht.

## Warum diese Reihenfolge?

Phase 0 ist eine Voraussetzung für alles Weitere: Werden nicht bestätigte Funktionen zusammengeführt oder weiter ausgebaut, können sich unentdeckte Fehler der echten Hardware fortpflanzen. Das entspricht der etablierten Vorgehensweise des Projekts und ist keine eigens für diesen Plan erfundene Regel. Phase 1 und Phase 2 sind voneinander unabhängig und könnten in getrennten Sitzungen parallel bearbeitet werden. Beide sollten jedoch vor Phase 3 abgeschlossen sein, damit `main` weder die ungeklärte Eigenheit noch die Paketierungslücke übernimmt. Phase 4 ist ausdrücklich ein Zusatzumfang und darf keine der notwendigen Arbeiten blockieren.
