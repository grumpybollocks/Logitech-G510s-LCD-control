# Paketierung von `g510-lcd`

Ein echtes Arch-/pacman-PKGBUILD für die G510s-Anwendung. Daneben bleibt die Installation über `install.sh` aus einem geklonten Repository weiterhin gültig. Dieses Paket folgt dem Muster „Dateien an feste `/usr/...`-Pfade installieren, keine benutzerspezifische Pfadsuche“ (siehe `PORTABILITY.md`) und wurde in derselben Nacht wie `packaging/g910-control/` für die G910-Seite erstellt, um die Ansätze direkt vergleichen zu können.

## Status

Das Paket wurde auf dem echten Rechner erstellt und installiert (`sudo pacman -U`, vom Benutzer selbst ausgeführt, nachdem die Berechtigungsrichtlinien dieser Sitzung eine automatische Installation verhindert hatten). Die beiden ursprünglich offenen Punkte sind mit echten Korrekturen und nicht mit Umgehungen behoben. **Noch nicht im AUR eingereicht** – dies ist weiterhin bewusst zurückgestellt (siehe „Vor einer echten AUR-Einreichung“). **Die G510s-Anwendung als Ganzes ist weiterhin in Arbeit**: Custom Screens ist ausdrücklich noch WIP. Eine solide Paketierung bedeutet nicht, dass die Anwendung fertig ist, und ändert nichts an der Projektregel, dass erst nach vollständiger Prüfung an der echten Tastatur ein Release markiert oder als fertig bezeichnet wird.

## Abhängigkeiten

```sh
depends=('python' 'python-pyqt5' 'python-pillow' 'python-evdev'
         'ydotool' 'freetype2' 'libg15render')
```

Die Abhängigkeiten wurden mit dem Quellcode abgeglichen und nicht ungeprüft aus `install.sh` übernommen. `yad` steht zwar in der `pacman`-Zeile des Installationsskripts, wird laut einer Suche in allen aktuellen `.py`- und `.c`-Dateien aber nirgends verwendet. Es ist ein Überbleibsel des eingestellten yad-basierten Beleuchtungsskripts (siehe den Eintrag `legacy-yad-backlight-script` in `BRANCHES.md`) und wurde hier daher korrekt weggelassen. `python-pillow` wird hingegen tatsächlich benötigt: `png-to-lcd.py` verwendet Floyd-Steinberg-Dithering für den Bildimport in Custom Screens.

## Verzeichnisstruktur – bewusst nicht flach wie bei `g910-control`

Das PKGBUILD von `packaging/g910-control` installiert seine `.py`-Dateien direkt unter `/usr/lib/g910-control/`, ohne Unterordner `src/`. Dieses Paket behält dagegen den Unterordner `src/` bei:

- `/usr/bin/g510-lcd` – schlanker Starter, der das eigentliche Skript mit `exec` ausführt
- `/usr/lib/g510-lcd/src/*.py` und kompilierte `*.c`-Programme – gleiche Struktur wie ein geklontes Repository, nicht abgeflacht
- `/usr/lib/systemd/user/g510-lcd-stats.service`, `g510-lcd-buttons.service` und `g510-macro-daemon.service`
- `/usr/lib/udev/rules.d/99-g510-lcd.rules` – bytegenau wie die eingecheckte Quelldatei installiert (siehe die inzwischen behobene Lücke weiter unten)
- `/usr/lib/g510-lcd/restore-backlight.sh` – von udev aufgerufener Starter zum Wiederherstellen der Beleuchtung (siehe unten)
- `/usr/share/applications/g510-lcd.desktop`
- `/usr/share/licenses/g510-lcd/LICENSE`

Das ist keine reine Stilentscheidung: `g510_app.py` ermittelt `PROJECT_DIR` mit `Path(__file__).resolve().parent.parent`. Mehrere Pfadkonstanten (`STATS_BINARY`, `DEFAULTS_SCRIPT` und der Aufruf von `png-to-lcd.py`) werden außerdem aus `PROJECT_DIR / "src" / ...` beziehungsweise `PROJECT_DIR / "scripts" / ...` gebildet. Würde die Struktur wie bei `g910-control` abgeflacht, würden alle diese Pfade stillschweigend falsch aufgelöst – `PROJECT_DIR` läge dann zum Beispiel unter `/usr/lib` statt `/usr/lib/g510-lcd`. Da diese Arbeit keine entsprechende Änderung am Quellcode vorgenommen hat, bleibt die bekannte Verzeichnisstruktur unter einem festen Stammverzeichnis erhalten. So funktioniert die bereits getestete Pfadlogik unverändert, statt einen neuen, ungeprüften Codepfad einzuführen.

## Behoben: Wiederherstellen der Beleuchtung über die udev-Regel

`99-g510-lcd.rules` erfüllt vier Aufgaben: Sie erstellt die symbolischen Verknüpfungen `/dev/g510-lcd` und `/dev/g510-keys` (beide sind zwingend erforderlich, da `g510_lcd_stats.c` und `g510_lcd_buttons.c` genau diese Pfade mit `fopen()` öffnen und andernfalls fehlschlagen), korrigiert die Berechtigungen des LED-sysfs-Eintrags und stellt bei jedem Hotplug die zuletzt gewählte Beleuchtungsfarbe wieder her.

Die letzte Aufgabe rief früher ein Skript mit eingesetztem `__PROJECT_DIR__` auf. Bei einem Paket hätte das nicht funktioniert: udev läuft als Root und kann kein benutzerspezifisches `$HOME` ermitteln. Die Funktion wurde korrekt repariert und nicht entfernt. `scripts/restore-backlight.sh` ist ein installationsartunabhängiger Starter: Beim Hotplug fragt er `loginctl` ab, welcher Benutzer eine aktive `seat0`-Sitzung hat (die Hardware ist ein Desktop-Rechner mit einem Sitzplatz; eine allgemeinere Mehrbenutzerbehandlung gehört nicht zum Umfang), ermittelt dessen `$HOME` mit `getent passwd` und führt dessen `~/.local/share/g510-lcd/set-backlight-color.sh` aus.

Das Skript benötigt kein `PROJECT_DIR`. Sowohl `install.sh` als auch das PKGBUILD installieren deshalb dieselbe Datei am selben festen Pfad (`/usr/lib/g510-lcd/restore-backlight.sh`). Auch die udev-Regel muss weder ersetzt noch bereinigt werden und wird bytegenau wie die Quelldatei installiert.

Die Prüfung erfolgte auf dem echten Rechner und nicht nur durch Lesen: Das Skript wurde direkt mit vollständiger Ablaufverfolgung (`bash -x`) ausgeführt. Es ermittelte den angemeldeten Benutzer und dessen gespeichertes Farbschema korrekt, führte das Skript aus und bestätigte anschließend den passenden Zustand des LED-sysfs-Eintrags. Das entspricht dem Mechanismus eines echten Hotplug-Ereignisses, wurde aber manuell ausgelöst, statt auf ein physisches erneutes Anstecken zu warten.

Ein kleiner, aber wichtiger Hinweis: Wenn beide Installationsarten gleichzeitig vorhanden sind (wie derzeit auf diesem Rechner), betrachtet `pacman` `/usr/lib/g510-lcd/` nach Installation dieses PKGBUILD als paketverwaltet. Das `sudo cp` von `install.sh` in denselben Pfad verändert dann Dateien außerhalb der pacman-Verwaltung. In der Praxis ist das unproblematisch, weil beide Verfahren dieselben Dateien kopieren. Trotzdem sollte `pacman -Qkk g510-lcd` auf einem Rechner, auf dem auch `install.sh` ausgeführt wurde, nicht als alleinige Wahrheitsquelle behandelt werden.

## Behoben: Beschriftungsschriftart

Es gilt dieselbe Einschränkung wie bei `install.sh`: Eurostile Bold ist kommerziell lizenziert und darf nicht im Paket weitergegeben werden. `g510_lcd_stats` benötigt weiterhin eine konvertierte Kopie an einem passenden Ort. Die Anwendung prüft jetzt zuerst `~/.local/share/g510-lcd/fonts/lcd-label-8.fnt` – unabhängig von der Installationsart ein benutzerschreibbarer Pfad – und greift erst danach auf den alten Pfad relativ zu `PROJECT_DIR` zurück. Bei einer Paketinstallation benötigt die manuelle Konvertierung somit kein `sudo`; sie ist genauso unkompliziert wie bei einem geklonten Repository.

Die Überprüfung erfolgte schrittweise: Nach dem Neubau des Programms wurden alle drei isolierten Fälle geprüft – Schriftart nur am alten Pfad, nur am neuen Pfad oder an keinem der beiden Orte. Alle verhielten sich wie vorgesehen; auch im Fehlerfall erschien weiterhin dieselbe verständliche Meldung. Zusätzlich wurde die auf dem echten Rechner installierte Paketversion geprüft, die vor dieser Korrektur erstellt worden war: Sie schlug erwartungsgemäß mit der dokumentierten Meldung fehl, da ihr kompiliertes Programm nur den alten `PROJECT_DIR`-Pfad kannte. Die konvertierte Schrift wurde anschließend ohne `sudo` nach `~/.local/share/g510-lcd/fonts/` kopiert. Damit ist sie bereit, sobald das mit der Korrektur neu erstellte Paket installiert wird – das war zum Zeitpunkt dieser Notiz noch nicht geschehen.

Das korrigierte Paket wurde wie der übrige Inhalt dieser Datei neu erstellt und inhaltlich überprüft. **Die Installation dieses konkreten Pakets auf dem echten Rechner, um genau diese Korrektur zu übernehmen, steht aber noch aus.**

## Was in diesem Durchlauf tatsächlich überprüft wurde

1. `makepkg` wurde mit einem echten Tarball ausgeführt: zunächst mit einem für schnelle Iterationen manuell aus dem Arbeitsverzeichnis erstellten Tarball, während unten ein echter Fehler behoben wurde, und anschließend mit einem echten `git archive` des HEADs von `g510s-dev`. Letzteres entspricht dem in der README von `g910-control` beschriebenen Verfahren; da keine der Anwendungen ein echtes öffentliches Tag hat, lässt sich so trotzdem aus einer realen Quelle bauen. Beide Builds liefen ohne Fehler oder Warnungen.
2. Das erstellte `.pkg.tar.zst` wurde entpackt und alle Dateien mit eingesetzten Pfaden manuell geprüft: der Starter `/usr/bin/g510-lcd`, die `ExecStart=`-Zeilen aller drei systemd-Dienste und die udev-Regel. Letztere ist jetzt bytegenau mit der Quelle identisch; das wurde mit `diff` bestätigt.
3. `strings` bestätigte, dass das kompilierte `g510_lcd_stats` den Pfad `/usr/lib/g510-lcd` enthält.
4. Alle Dateien im erstellten Paket (nicht nur der Quellcode) wurden auf persönliche Kennungen geprüft. Einzige Treffer waren die Felder `builddir` und `startdir` in `.BUILDINFO`, die den Buildspeicherort festhalten. Dieser Build lief in einem temporären Verzeichnis unter dem Home-Verzeichnis des Benutzers. Das ist kein Fehler im PKGBUILD: Ein echter AUR-Build oder ein Build unter `/tmp` würde diese Angaben nicht enthalten. Der Umstand ist der Vollständigkeit halber dokumentiert.
5. **Die echte Installation wurde durchgeführt:** Der Benutzer selbst führte `sudo pacman -U` aus. Danach zeigte `systemctl --user show g510-lcd-stats.service -p FragmentPath` weiterhin auf `~/.config/systemd/user/g510-lcd-stats.service` (die Version aus dem geklonten Repository). Auch die `MainPID` war vor und nach der Installation unverändert. Die Paketdateien – Dienstdateien, udev-Regel und weitere Bestandteile – wurden also wie vorgesehen installiert, ohne die laufende Umgebung zu stören. Das Paket enthält kein `.install`-Hook (`pacman -Qi g510-lcd` zeigt `Install Script: No`), daher hat `pacman -U` die systemd-Aktivierung nicht verändert. Die Paketdienste wurden durch die Installation weder gestartet noch aktiviert. Sie für einen vollständigen Funktionstest zu starten, hätte vorübergehend das `~/.config`-Override entfernen müssen und wurde deshalb in diesem Durchlauf nicht getan.

## Was noch wirklich aussteht

- **Die Korrekturen für Schriftart und Wiederherstellung der Beleuchtung** sind in einem neu gebauten, isoliert getesteten Paket enthalten. Das aktuell auf dem echten Rechner installierte Paket ist jedoch älter und enthält beide Korrekturen nicht. Es muss mit `sudo pacman -U` erneut installiert werden, damit die reale Installation aktualisiert wird. Dieser Schritt wurde in diesem Durchlauf nicht vorgenommen, da er dieselbe direkte Benutzeraktion erfordert hatte wie die erste Installation.
- **Die Paketdienste vollständig starten und durchtesten** – LCD, Tasten und Makros müssen tatsächlich aus `/usr/lib/g510-lcd/` funktionieren, nicht nur auf den Aufruf von `--preview` reagieren. Dieser Test wurde ausgelassen, damit die laufenden Entwicklungsdienste nicht versehentlich gestört werden.
- **`namcap`** ist auf diesem Rechner nicht installiert. Der Prüfschritt wurde daher ausgelassen und nicht einfach als fehlerfrei angenommen.
- **`sha256sums=SKIP`** ist vorläufig wegen der privaten Quelle und des fehlenden öffentlichen Tags gesetzt; derselbe Umstand ist in `G510_PLAN_A.md`, Phase 4, dokumentiert. Vor jeder AUR-Einreichung muss ein echter Prüfsummenwert eingetragen werden.

## Vor einer echten AUR-Einreichung

Diese Schritte wurden noch nicht ausgeführt und benötigen die Zustimmung des Benutzers:

- Das Paket mit den Korrekturen für Schriftart und Beleuchtungswiederherstellung erneut installieren und den vollständigen Diensttest durchführen.
- Das Quellcode-Repository öffentlich machen und eine echte `sha256sum` berechnen.
- `.SRCINFO` mit `makepkg --printsrcinfo > .SRCINFO` erstellen.
- Zu einem eigenen Git-Remote unter `aur.archlinux.org` pushen.
