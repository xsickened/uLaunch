# NEXUS HOME (Branch `nexus-home`)

Anpassbares HOME-Menü auf Basis von uLaunch (GPLv2).

- `docs/NEXUS-ANALYSE.md` – Phase 1: Architektur, Abhängigkeiten, FW-21.2.0-Kompatibilität, Pläne, Aufgabenliste, Sicherheit
- `docs/STABILITAET.md` – Stabilitätskonzept für den Betrieb als Homebrew auf sysNAND
- `docs/settings-schema.json` – alle Einstellungen (Typ, Grenzen, Standard); gemeinsame Quelle für Prototyp und C++
- `prototype/nexus-prototype.html` – Phase 2: interaktiver Prototyp v0.2 (Browser; Tastatur, Maus, Gamepad, Touch)
- `scripts/build-ulaunch.sh` – Build passend zur installierten Atmosphère-Version (WSL2 oder Docker `devkitpro/devkita64`)

Der Originalcode von uLaunch ist in diesem Branch unverändert. NEXUS-Code kommt in Phase 3 nach `projects/uMenu/{include,source}/nexus/` bzw. zunächst als eigenständige NRO.

## Switch-Test-App (`projects/nexus`)

- Wird bei jedem Push auf `nexus-home` von GitHub Actions gebaut (`.github/workflows/nexus.yml`), Ergebnis: Artefakt **NEXUS-HOME-SD**.
- Installation: Inhalt des Artefakts in die oberste Ebene der SD-Karte kopieren (`switch/nexus/nexus.nro`), dann ein Spiel mit gedrückter **R**-Taste starten → Homebrew-Menü → NEXUS HOME.
- Abgesicherter Modus: **L** beim Start gedrückt halten.
- Bedienung: Steuerkreuz/Stick, A Starten/Öffnen, B Zurück, X Optionen, Y Favorit, L/R Bereich, − Sperrbildschirm, + Beenden.
- Daten: nur `sdmc:/nexus/` (nexus.json, cache/, art/, log.txt).
- Kern-Tests lokal: `g++ -std=gnu++20 -Ilibs/json/single_include -Iprojects/nexus/source/core nexus/tests/core_test.cpp projects/nexus/source/core/*.cpp && ./a.out`
- `nexus/docs/RETROACHIEVEMENTS.md` – RetroAchievements-Anzeige (Phase 5, umgesetzt)

## Phase 5: Online-Funktionen (v0.2)

- **SteamGridDB:** X auf einer Kachel → „SteamGridDB: Bilder suchen …“ → Spiel → Cover / Hintergrund / Logo mit großer Vorschau. Download nach `sdmc:/nexus/art/<id>/sgdb-*.png|jpg`, Auswahl in `nexus.json` (`apps.<id>.art`). Option „Fehlende Bilder automatisch laden“.
- **RetroAchievements:** Punkte/Pokale in der Kopfzeile, Fortschritt auf Kacheln und unter dem Logo, letzter Erfolg auf dem Sperrbildschirm, Erfolgsliste und Zuordnung im Kontextmenü. Nur lesend über die Web-API.
- **Schlüssel:** im Menü per Tastatur oder über `sdmc:/nexus/keys.txt` (`sgdb=`, `ra_user=`, `ra_key=`; wird nach dem Import gelöscht). Nie im Log.
- **Netz:** ein Hintergrund-Thread (`nx/net.cpp`, curl), Zeitlimits, Größenlimits, ein Wiederholversuch bei HTTP 429. Ohne Netz oder Schlüssel passiert nichts.
- **Akzent:** einfarbig, Verlauf (2/3 Farben) oder RGB-Regenbogen, mit Richtung und Bewegung (fließend, Farbwechsel, Pulsieren). Gemeinsame Rechnung in `core/accent.cpp` und im Prototyp.
- **Bildauswahl:** Home-, Sperr- und Profilhintergrund sowie Cover/Hintergrund/Logo pro Titel aus `sdmc:/nexus/art/…` oder dem Album.
