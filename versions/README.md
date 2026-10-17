# Versionsstände von `g510_app.py`

Die aktuelle Anwendung liegt als `g510_app.py` im Projektstamm – das ist die Datei, die ausgeführt und bearbeitet werden soll. Dieser Ordner enthält lediglich Momentaufnahmen früherer Entwicklungsstände zum Vergleichen und Nachvollziehen der Entwicklung.

## v0.1 – Nur Beleuchtungssteuerung

`g510_app_v0.1.py` ist der Stand vor der Einführung der G-Tasten-Makros. Die Oberfläche hatte einen Tab mit:

- Auswahlfeld für Farbprofile und Helligkeitsregler sowie **Apply** / **Apply Defaults**
- Schaltflächen **Start Service** / **Restart Service**

Dieser historische Stand ist eingefroren und nicht als Grundlage für die weitere Entwicklung gedacht. Er kann zwar eigenständig laufen, erhält aber keine späteren Fehlerkorrekturen – diese erfolgen ausschließlich in der aktuellen `g510_app.py`.

## Spätere Versionen

Der nächste Entwicklungsschritt (G-Tasten-Tab mit Aufzeichnung, Wiedergabe über `ydotool` und Profilen M1/M2/M3) ist in der technischen Dokumentation [`G510_README.md`](../G510_README.md) beschrieben und wird hier nicht als Snapshot abgelegt, da er in der aktuellen Anwendung weiterentwickelt wird. Die Überprüfung des Profilwechsels über die M-Tasten ist pausiert; siehe den Abschnitt „Aktueller Status“ am Anfang der technischen Dokumentation.
