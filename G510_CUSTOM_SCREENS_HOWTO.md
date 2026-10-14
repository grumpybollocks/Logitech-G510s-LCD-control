# Eigene Bildschirme – Kurzanleitung

Im Tab **Custom Screens** kannst du eigene Übersichten für die Tasten L2–L5 erstellen. Hier findest du die wichtigsten Schritte.

## Grundlagen

1. **Bildschirm auswählen** – Klicke oben auf L1, L2, L3, L4 oder L5.
   - **L1 ist eine Vorschau und nicht bearbeitbar.** Dort wird die eingebaute Uhr angezeigt. Mit der physischen L1-Taste an der Tastatur wechselst du wie gewohnt zwischen Uhr und CPU-/RAM-/VRAM-/Temperaturanzeige. Der Tab zeigt jeweils den aktuellen Inhalt, damit du nicht zur Tastatur greifen musst. Hinzufügen und Importieren sind hier deaktiviert; der Mauszeiger erklärt per Tooltip den Grund.
   - **L2–L5 kannst du frei gestalten.** Ein leerer Bildschirm bleibt einfach leer – du musst vorher nichts löschen.

2. **Sensor hinzufügen** – Wähle im ersten Auswahlfeld einen Sensor (zum Beispiel CPU-Auslastung, RAM oder GPU-Temperatur). Wähle **Number** für eine reine Zahlenanzeige oder **Bar** für einen Balken mit Zahlenwert. Für Balken kannst du außerdem eine waagerechte oder senkrechte Ausrichtung einstellen. Klicke anschließend auf **Add**. Senkrechte Balken zeigen nur den Balken; für eine Überschrift kannst du ein eigenes Textelement daneben platzieren.

3. **Text hinzufügen** – Klicke auf **Add Text...**, gib den gewünschten festen Text ein (zum Beispiel eine Beschriftung oder einen Namen) und füge ihn wie jedes andere Element zur Vorschau hinzu. Pro Bildschirm sind bis zu vier Textfelder möglich.

4. **Element verschieben** – Ziehe es mit der Maus an die gewünschte Stelle in der Vorschau. Dort wird es auch auf dem Tastaturdisplay erscheinen.

5. **Bild hinzufügen** – Klicke auf **Import Image...** und wähle ein Bild auf deinem Rechner aus. Es wird automatisch verkleinert und für das kleine Schwarz-Weiß-Display umgewandelt. Anschließend kannst du es wie jedes andere Element verschieben.

6. **Balken oder Bild skalieren** – Bewege den Mauszeiger nahe an das rechte Ende eines Balkens oder an die rechte untere Ecke eines Bildes. Ein kleines blaues Quadrat erscheint. Ziehe es, um die Größe anzupassen.

7. **Zahlen oder Text skalieren** – Diese Elemente bestehen aus Text; die kleinen eingebauten Display-Schriften lassen sich nicht stufenlos vergrößern. Deshalb gibt es dafür keinen Ziehpunkt. Wähle vor dem Hinzufügen im Auswahlfeld **Text Size** eine Größe (Small/Medium/Large/Huge). Bei bereits vorhandenen Elementen kannst du über die Schaltfläche neben dem jeweiligen Eintrag rechts durch die Größen wechseln.

8. **Element entfernen** – Klicke rechts in der Liste neben dem Element auf **✕**.

Das war's. Eine **Speichern**-Schaltfläche gibt es nicht: Jede Änderung (Verschieben, Skalieren, Hinzufügen oder Entfernen) wird sofort gespeichert.

## Wissenswertes

- **Das Display ist klein:** 160 × 43 Pixel, genau wie das echte LCD. Bei zu vielen Elementen können sich Zeilen überlappen oder am unteren Rand abgeschnitten werden. Die Vorschau entspricht der echten Anzeige. Wenn etwas in der Anwendung falsch aussieht, sieht es auch auf der Tastatur falsch aus – lass genügend Abstand.
- **Für jeden Elementtyp gilt ein eigenes Limit pro Bildschirm:** 16 Sensoren, 2 Bilder und 4 Textfelder. Das sind keine gemeinsamen Gesamtlimits: Ein Bildschirm kann also gleichzeitig 16 Sensoren, 2 Bilder und 4 Textfelder enthalten. Die Anwendung meldet, wenn eines der Limits erreicht ist.
- **Nicht jeder Sensor kann als Balken angezeigt werden.** Balken gibt es nur für Werte mit einem sinnvollen Bereich von 0 bis 100 Prozent oder einem geeigneten Temperaturbereich. Bei ungeeigneten Sensoren informiert dich die Anwendung und zeigt den Wert stattdessen als Zahl an.
- **Senkrechte Balken** eignen sich für kompakte Anzeigen mehrerer CPU-Kerne oder der GPU. Bewege den Mauszeiger nahe an das untere Ende und ziehe den Griff nach oben oder unten, um die Höhe anzupassen.
- **Der blaue Ziehpunkt erscheint nur bei Bedarf.** Er wird nur sichtbar, wenn sich der Mauszeiger nahe an einem Balken oder an einem skalierbaren Bild befindet. Früher waren alle Ziehpunkte gleichzeitig sichtbar, was unübersichtlich war.
- **Die Sensorauswahl erklärt die Werte.** Die Beschreibung unter dem Auswahlfeld wird beim Wechsel der Auswahl aktualisiert. So musst du nicht raten, was zum Beispiel „GPU Hotspot Temp“ oder „Max Temp Seen“ bedeutet.

## Auf der echten Tastatur anzeigen

Wenn deine eigene Anzeige in der Anwendung passt, drücke die zugehörige L-Taste an der Tastatur (L2, L3, L4 oder L5). Falls die Tastatur eine Änderung nicht sofort anzeigt, muss möglicherweise der LCD-Dienst neu gestartet werden. Wenn du nicht weißt, wie das geht, frage nach.

## Übersicht im C2DtoG15-Stil

Klicke auf **Apply C2DtoG15 layout**, um den ausgewählten Bildschirm L2–L5 durch eine kompakte Übersicht zu ersetzen. Sie enthält acht Balken für die Auslastung einzelner logischer CPU-Kerne, die GPU-Temperatur am Rand, GPU-Auslastung, RAM-Auslastung sowie aktuelle Netzwerk-Up- und Downloadraten. Vor dem Ersetzen fragt die Anwendung nach einer Bestätigung. Anschließend kannst du die erzeugten Elemente wie gewohnt verschieben, skalieren und Sensoren hinzufügen oder entfernen.

Die Höhe der CPU-Balken zeigt die **Auslastung**, nicht die Temperatur. Linux stellt die Temperatur einzelner CPU-Kerne nicht auf jeder Hardware bereit.
