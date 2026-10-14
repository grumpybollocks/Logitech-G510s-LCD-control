# Übersicht der Branches

Dieses Repository enthält eine Anwendung für eine Tastatur: die Logitech G510s (LCD, Hintergrundbeleuchtung und G-Tasten). Diese Datei soll aktuell bleiben. Ändert sich der Zweck eines Branches, muss der zugehörige Eintrag im selben Commit angepasst werden.

Unter [`READY_FOR_ANYONE.md`](READY_FOR_ANYONE.md) steht, was die Anwendung für die Installation durch andere Personen bereits zuverlässig erfüllt und welche bekannten Lücken noch bestehen.

## Früher ein Repository für zwei Tastaturen

Bis zum 17.09.2026 enthielt dieses Repository auch eine separate Anwendung für die Logitech G910 Orion Spectrum (RGB-Beleuchtung einzelner Tasten und Makros), die zeitweise parallel zur G510s-Anwendung entwickelt wurde. Deren Quellcode, Dokumentation, Installationsskript, Paketierung und Dienstdatei wurden am 17.09.2026 aus `main` entfernt. Der vollständige Abgleich aller gemeinsamen Dateien und passenden Tags bestätigte, dass das eigenständige öffentliche Repository [grumpybollocks/g910-control](https://github.com/grumpybollocks/g910-control) bereits den gesamten damaligen Stand und zusätzliche Verbesserungen enthält. `main` ist nun ausschließlich der G510s gewidmet.

Auch die alten Branches `g910` und `g910-canvas`, der kurzlebige leere Branch `g510s` sowie alle `g910-*`-Tags (`g910-v1.0` bis `v1.4`, `g910-gui-v1` und `g910-skeleton-v1-buttongrid`) wurden nach derselben Überprüfung am 17.09.2026 gelöscht. Zuvor wurde eine vollständige `--mirror`-Sicherung des Repositorys erstellt. Der entfernte Stand ist daher nicht unwiederbringlich verloren, auch wenn er nicht mehr auf GitHub liegt.

## Aktive Branches

- **`main`** – die G510s-Anwendung mit LCD-Statistiken, Beleuchtungssteuerung und G-Tasten-Makros. Mit `v1.0` markiert.
- **`g510s-dev`** – aktive Entwicklung auf dem Rechner mit angeschlossener G510s. Die Funktionen lassen sich nur dort an der echten Tastatur vollständig testen. Dies ist der maßgebliche Branch für die weitere Entwicklung. Änderungen werden erst nach Abschluss und Bestätigung an der echten Tastatur in `main` übernommen und markiert. Das ist eine feste Regel, keine Verzögerung.

## Historische / eingefrorene Branches

Diese Branches bleiben als Referenz erhalten und werden nicht gelöscht:

- **`legacy-yad-backlight-script`** – das ursprüngliche eigenständige yad-/Bash-Skript zur Beleuchtungssteuerung der G510s. Es wurde durch den Beleuchtungs-Tab der Anwendung ersetzt.

## Tags

- **`v1.0`** – G510s v1.0 (LCD-Statistiken, Beleuchtung und G-Tasten-Makros); markierter und bestätigter Ausgangsstand in `main`.
- **`pre-canvas-checkpoint`** – Sicherung unmittelbar vor dem Umbau der Beleuchtungs- und G-Tasten-Oberfläche zum Canvas; enthält bereits die Arbeit an Custom Screens v1.1 (AIDA64-ähnliches Sensordashboard für L2–L5).
- **`g510-canvas-v1` / `g510-canvas-v2` / `g510-canvas-v3`** – Sicherungspunkte während des Canvas-Umbaus: Anzeige der mit Makros belegten Tasten und vollständige Eigenprüfung (v1), Mauszeiger-Feedback und kontextbezogene Hinweise (v2) sowie die echte Fehlerkorrektur für M1/M2/M3-Klicks, deren Auswahl nach etwa 500 ms zurücksprang (v3).
