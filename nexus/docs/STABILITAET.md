# NEXUS HOME – Stabilität als Homebrew auf sysNAND

Ausgangslage: Die Konsole hat **kein emuMMC**. NEXUS wird deshalb bis Phase 7 als normale Homebrew-App (NRO) auf sysNAND (sysMMC) entwickelt und getestet. Das HOME-Menü bleibt dabei unangetastet.

## 1. Was die NEXUS-Homebrew niemals tut

| Verboten | Warum |
|---|---|
| In den internen Speicher (NAND) schreiben | Nur so bleibt sysNAND unverändert. |
| Systemeinstellungen ändern (`set:sys` schreibend) | Kein Risiko für Konsolen-Konfiguration. |
| Sysmodule installieren, `atmosphere/` verändern | Fehler könnten den Boot betreffen. |
| Titel installieren, löschen oder verschieben | „Löschen“ in NEXUS entfernt nur NEXUS-Zuordnungen. |
| TICO, TICO-Konfiguration, ROMs oder Spielstände anfassen | Vorgabe aus dem Master-Prompt. |
| uLaunch-Daten (`sdmc:/ulaunch/`) schreiben | Nur lesen; Rückfall auf vanilla uLaunch bleibt möglich. |

**Einziger Schreibort:** `sdmc:/nexus/`. Löscht man diesen Ordner, ist NEXUS vollständig entfernt.

## 2. Laufmodi auf der Switch

- **Applet-Modus** (Start aus dem Album): Hier steht deutlich weniger Arbeitsspeicher zur Verfügung. NEXUS erkennt diesen Modus über `appletGetAppletType()` und verkleinert dann automatisch den Texturspeicher: Heroes in 960×540, höchstens zwei gleichzeitig, keine Leerlauf-Animationen.
- **Title-Takeover** (Spiel mit gedrückter R-Taste starten): Voller Arbeitsspeicher. Das ist der empfohlene Testmodus, er greift nicht ins System ein.
- Das genaue Speicherbudget wird in Phase 3 auf deiner Konsole gemessen (`svcGetInfo`). Bis dahin gelten die konservativen Werte oben.

## 3. Schutzmechanismen (im Prototyp bereits umgesetzt, 1:1 für C++)

1. **Ein Schema für alle Einstellungen:** `settings-schema.json` enthält alle 107 Einstellungen mit Typ, erlaubten Werten, Grenzen und Standardwert. Jede geladene Konfiguration wird dagegen geprüft. Ungültige oder fehlende Werte werden durch den Standardwert ersetzt, Zahlen auf ihre Grenzen begrenzt.
   *Test:* 300 zufällig beschädigte Konfigurationen, danach 0 ungültige Werte.
2. **Selbstheilende Bibliothek:**
   - Verweise auf nicht mehr vorhandene Titel werden entfernt, Duplikate gelöscht.
   - Jeder Titel ist danach genau einmal erreichbar, entweder auf der Startseite oder in genau einem Ordner.
   - Der Bereich „Einstellungen“ kann nicht ausgeblendet werden, damit man sich nie aussperrt.
3. **Atomares Speichern:** Zuerst wird `nexus.json.tmp` geschrieben, die alte Version wandert nach `nexus.json.bak`, dann wird umbenannt. Ist die Datei beim Laden beschädigt, wird das Backup verwendet, danach die Standardwerte.
   *Test:* Hauptdatei und Backup absichtlich zerstört → Start ohne Fehler, Bibliothek neu aufgebaut.
4. **Start-Wächter:** Beim Start wird ein Zähler erhöht und erst nach erfolgreichem Start zurückgesetzt. Nach zwei Fehlstarts startet NEXUS im **abgesicherten Modus**: Die Darstellung wird auf Standard gesetzt, Bibliothek und Bilder bleiben erhalten. Auf der Switch löst zusätzlich **L beim Start gedrückt halten** den abgesicherten Modus aus.
5. **Zweistufige Fehlerbehandlung beim Zeichnen:**
   - Beim 1. Fehler wechselt NEXUS auf die Anordnung „Raster“ mit reduzierter Bewegung.
   - Beim 2. Fehler erscheint ein Fehlerbildschirm mit der Option „Darstellung zurücksetzen“.
   - In keinem Fall gibt es einen Absturz ins System.
6. **Bilder:** Fehlende oder defekte Dateien werden durch einen Platzhalter ersetzt. Importierte Bilder werden beim Speichern auf eine Maximalgröße verkleinert: Cover 600 px, Hero 1280 px, Logo 900 px breit.
7. **Längenlimits** für alle Texte (Ordnernamen 60 Zeichen, Suchbegriffe 120 Zeichen) und eine Obergrenze für Bilddateien.
8. **Netzwerk:**
   - Läuft nur in einem eigenen Thread mit 10 s Timeout und höchstens zwei parallelen Downloads.
   - Ohne Internet läuft alles weiter, nur die Suche meldet einen Fehler.
   - Der API-Key erscheint nie im Log.
9. **Texturcache:** LRU-Prinzip mit Byte-Budget statt Bildanzahl. Nicht sichtbare Kacheln geben ihre Textur frei. Beim schnellen Scrollen wird der Hero erst nach einer kurzen Pause geladen; die Verzögerung ist einstellbar (Standard 110 ms).
10. **C++-Regeln:**
    - Kein `UL_ASSERT` bei datenabhängigen Fällen. uLaunch bricht z. B. in `ParseNacpFields` per Assert ab, NEXUS-Code verwendet stattdessen Fallbacks.
    - Alle Datei- und Netzwerkfunktionen geben ein Ergebnis zurück, statt Ausnahmen zu werfen.
    - Keine Ausnahmen über Thread-Grenzen.
11. **Log:** `sdmc:/nexus/log.txt`, wird bei 256 KB rotiert und enthält keine geheimen Werte.

## 4. Testplan vor jeder Version

- Fuzz-Test der Konfiguration mit dem gemeinsamen Schema auf dem PC
- Stresstest mit 500 Einträgen in allen 11 Anordnungen
- 30 Minuten Dauerlauf mit Bereichswechseln, danach Speicherverbrauch vergleichen
- Sleep und Wake, Handheld- und Dock-Modus, Applet-Modus und Title-Takeover
- Absichtlich beschädigte `nexus.json` auf der SD-Karte → Start muss funktionieren

## 5. Hinweise für sysNAND

- **Vor dem ersten Test:** NAND-Backup über Hekate (BOOT0/1 + RAW GPP) erstellen und auf dem PC aufbewahren.
- **Die NRO-Phase ist risikoarm:** Die App läuft wie jede andere Homebrew und schreibt nur nach `sdmc:/nexus/`.
- **Online-Nutzung:** Wer mit Custom Firmware auf sysNAND online geht, riskiert unabhängig von NEXUS eine Sperre durch Nintendo. Die SteamGridDB-Suche braucht Internet. Ob und wie du Nintendo-Server blockierst (z. B. per DNS-Sperre), entscheidest du selbst.
- **Erst Phase 8 macht NEXUS zum HOME-Menü.** Rückweg: am PC nur `atmosphere/contents/0100000000001000/exefs.nsp` löschen, oder über Hekate ohne CFW starten.
