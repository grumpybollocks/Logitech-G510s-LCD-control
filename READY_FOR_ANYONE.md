# Für alle installierbar

Dies ist kein persönliches Skript, das zufällig auf einem einzelnen Rechner funktioniert. Es ist eine fertige Anwendung, die andere Personen für ihre eigene G510s klonen und ausführen können, ohne an die ursprüngliche Entwicklungsumgebung gebunden zu sein. Diese Datei hält fest, warum das so ist und welche Punkte noch offen sind, bis die Anwendung dieses Ziel vollständig erfüllt.

## Kurzfassung

Der ausgelieferte Quellcode und die Konfiguration enthalten weder einen fest eingetragenen Benutzernamen oder Rechnernamen noch einen fest angenommenen Klonpfad. `install.sh` ermittelt seinen tatsächlichen Speicherort zur Laufzeit (`DIR="$(cd "$(dirname "$0")" && pwd)"`) und ersetzt damit entweder den Platzhalter `__PROJECT_DIR__` in den eingecheckten systemd-Dienstdateien und der udev-Regel oder übergibt den Pfad mit `-DPROJECT_DIR` an den C-Compiler. Das LCD-Programm enthält ein `#error` zur Übersetzungszeit, falls dieses Flag fehlt – es gibt keinen stillen Rückfall auf einen falschen Pfad. Das Installationsskript erzeugt außerdem alle Desktop-Starter neu anhand des ermittelten Pfades, statt vorauszusetzen, dass sie bereits existieren. Klonen, beliebigen Speicherort wählen, Installationsskript ausführen – genau das ist der Anspruch.

## Was das Installationsskript tatsächlich erledigt

`install.sh`:

1. Ermittelt unabhängig vom Klonpfad seinen tatsächlichen Speicherort (`$DIR`).
2. Prüft zuerst jede Abhängigkeit und meldet deren Status (`[ok]`/`[missing]`), bevor das System verändert wird. Es gibt kein still im Hintergrund laufendes `pacman -S`.
3. Installiert nur tatsächlich fehlende Abhängigkeiten.
4. Fragt nach oder beendet sich mit einer verständlichen Meldung, statt bei Dingen zu raten, die sich nicht zuverlässig automatisch bestimmen lassen. Die konkreten Fälle stehen in den Kommentaren des Skripts.
5. Ersetzt `__PROJECT_DIR__` beziehungsweise übergibt `-DPROJECT_DIR` an jede eingecheckte Dienstdatei, udev-Regel und C-Quelldatei, die den Installationsort kennen muss.
6. Erzeugt Desktop-Starter anhand des tatsächlichen Pfads und schreibt sie sowohl nach `~/Desktop` (als Desktop-Symbol) als auch nach `~/.local/share/applications` (dem standardisierten XDG-Speicherort für Anwendungsmenüs). Manche Desktop-Umgebungen – insbesondere GNOME – zeigen Desktop-Symbole standardmäßig gar nicht an; nur `~/Desktop` zu verwenden würde die Anwendung dort trotz korrektem Pfad unauffindbar machen.

## Status

**In diesem Durchlauf erledigt und überprüft**, auf dem Rechner mit der echten G510s: `install.sh` erzeugt alle fünf Desktop-Starter frisch anhand des ermittelten `$DIR`. Getestet wurde ausschließlich die Logik zur Erzeugung der Starter – nicht das vollständige Installationsskript, das unabhängige Änderungen an `sudo`, `pacman`, udev und systemd auslösen würde. Die neu erzeugten Dateien wurden mit den funktionierenden Originalen verglichen. Der einzige Unterschied waren Anführungszeichen im `Exec=`-Pfad, eine echte Robustheitsverbesserung. Außerdem wurde bestätigt, dass die Anwendung mit dem quotierten Befehl startet und dass `dolphin`, `konsole` und `gio` auf diesem System vorhanden sind. `zenity` wurde zu den Abhängigkeiten hinzugefügt. Die Starter werden jetzt zusätzlich unter `~/.local/share/applications` abgelegt.

## An einem echten Vorbild geprüft, nicht geraten

`pacman -Ql solaar` wurde geprüft: Solaar ist ein reales, aktiv gepflegtes, offiziell für Arch paketiertes Logitech-Geräteverwaltungsprogramm, das auf diesem Rechner installiert ist. Es gehört zum selben Problemfeld (Steuerung von Logitech-HID++-Geräten, grafische PyQt-Oberfläche und eine benötigte udev-Regel). Die tatsächlich installierte Verzeichnisstruktur – `/usr/bin/solaar`, `/usr/lib/udev/rules.d/`, `/usr/share/applications/solaar.desktop` und Python-Quellcode unter `/usr/lib/python3.*/site-packages/` – bestätigt das übliche professionelle Muster: Ein Systempaket installiert Dateien beim Erstellen an feste Speicherorte, sodass zur Laufzeit kein benutzerspezifischer Installationspfad ermittelt werden muss.

Die in diesem Repository verwendete Vorlage `__PROJECT_DIR__` ist für eine Installation per „Repository klonen und `install.sh` ausführen“ passend. Ein richtiges AUR-Paket wäre der nächste Schritt von „alle können das Repository klonen und installieren“ zu „alle können es einfach mit `yay -S` installieren“: ein PKGBUILD pro Anwendung, Dienstdateien unter `/usr/lib/systemd/user/`, die udev-Regel unter `/usr/lib/udev/rules.d/` und Abhängigkeitsauflösung über pacmans `depends=` statt eines eigenen Installationsskripts. Ein echtes `packaging/g510-lcd/`-PKGBUILD existiert bereits in `g510s-dev`; siehe die Tabelle in der Haupt-README. Auf `main` wurde dieser Schritt hier noch nicht begonnen.
