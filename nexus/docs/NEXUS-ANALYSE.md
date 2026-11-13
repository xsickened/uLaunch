# NEXUS HOME – Phase 1: Technische Analyse von uLaunch

Stand: 13.11.2026 · Basis: `XorTroll/uLaunch` master (`6da94f9`, 24.07.2026, v1.2.5) · Arbeitsbranch: `nexus-home`

Legende: **[verifiziert]** = im Quellcode/Changelog geprüft · **[zu prüfen]** = muss auf der Konsole oder mit Toolchain bestätigt werden.

---

## 1. Tatsächliche Projektarchitektur

uLaunch ist kein einzelnes Programm, sondern ein Verbund aus mehreren Prozessen **[verifiziert]**:

| Komponente | Rolle | Läuft als | Technik |
|---|---|---|---|
| **uSystem** | Ersetzt `qlaunch` (Titel-ID `0100000000001000`). Verwaltet Anwendungen, Applets, HOME-Button, Sleep, Icon/NACP-Cache (~40 MB im RAM). | System-Applet (`atmosphere/contents/0100000000001000/exefs.nsp`) | Atmosphere-libs (stratosphere), libnx |
| **uMenu** | Die sichtbare Oberfläche (HOME-Menü, Einstellungen, Themes). | Library-Applet über dem *Album*-Applet (`010000000000100D`) | **Plutonium** (eigenes C++-UI-Framework auf **SDL2**) |
| **uLoader** | Fork von nx-hbloader; startet NROs als Applet oder als Anwendung (Homebrew-Takeover). | Applet / Application | libnx |
| **uManager** | NRO zum Aktivieren/Deaktivieren von uLaunch. | Homebrew | Plutonium |
| **uScreen** | PC-Tool für USB-Bildschirmaufnahme. | PC (Java) | Maven |
| **uDesigner** | Theme-Designer. | PC/Web | imgui |
| **uCommon** (`libs/uCommon`) | Gemeinsame Bibliothek: Config, Menüeinträge, Cache, JSON, IPC-Protokoll (SMI). | statische Lib | nlohmann/json, zip |

Kommunikation: uMenu ↔ uSystem über das private **SMI-Protokoll** (`ul/smi/smi_Protocol.hpp`) mit Befehlen wie `LaunchApplication`, `LaunchHomebrewLibraryApplet`, `LaunchHomebrewApplication`. **Die NEXUS-UI muss Systemaktionen ausschließlich über diesen Weg auslösen** → das ist unser `SystemAdapter`.

### Relevante Codestellen für NEXUS

| Datei | Bedeutung für NEXUS |
|---|---|
| `projects/uMenu/source/ul/menu/ui/ui_EntryMenu.cpp` | Aktuelles Kachelraster: **feste quadratische Kacheln** (`DefaultEntrySize = 256`), horizontales Paging. Wird durch die neue Layout-Engine ersetzt. |
| `projects/uMenu/source/ul/menu/ui/ui_MainMenuLayout.cpp` | Hauptmenü. `menu_FocusedEntryChanged(...)` (Z. 505) ist der **Einhängepunkt für den FocusPresentationManager** (Hero + Logo). |
| `libs/uCommon/source/ul/menu/menu_Entries.cpp` | Persistenz der Einträge und Ordner. |
| `libs/uCommon/include/ul/ul_Include.hpp` | Alle SD-Pfade (`sdmc:/ulaunch/...`). |
| `default-theme/ui/UI.json` | Theme-Layout (Koordinaten in 1920×1080). |

---

## 2. Build-Abhängigkeiten

Laut README und Makefiles **[verifiziert]**:

- devkitPro mit **devkitA64** und **libnx** (ggf. libnx-master, wenn Atmosphere-libs neuere Header braucht: `make LIBNX=<pfad>/nx/`)
- Portlibs: `switch-sdl2 switch-freetype switch-glad switch-libdrm_nouveau switch-sdl2_gfx switch-sdl2_image switch-sdl2_ttf switch-sdl2_mixer` sowie (Plutonium) `switch-mesa switch-glm switch-libwebp switch-libpng switch-bzip2 switch-libjpeg-turbo switch-opusfile switch-libopus` und `harfbuzz`
- **Wichtig:** SDL2 muss in Revision **`2.28.5-3`** verwendet werden (nutzt `audout` statt `audren`). Neuere Revisionen lassen beim Suspendieren mancher Spiele das Audio-Sysmodul abstürzen.
- Python (für `arc`-Fehlercode-Generator), `zip`, `build_romfs` (devkitPro switch-tools)
- Optional: Maven + Java (uScreen), nicht nötig für NEXUS
- Git-Submodule (rekursiv klonen): Plutonium, Atmosphere-libs, arc, json, zip, imgui, stb, libnx-ext
- **Neu für NEXUS:** `switch-curl` + `switch-mbedtls` (HTTPS für SteamGridDB) – uMenu enthält bisher **keinen** HTTP-Client.

---

## 3. Kompatibilität mit Firmware 21.2.0

**[verifiziert]** aus `cur-changelog.md`:

| uLaunch | gebaut gegen Atmosphère | Firmware |
|---|---|---|
| 1.2.2 | 1.10.1 | 21.1.0 |
| **1.2.3** | **1.10.2** | **21.2.0** |
| 1.2.4 | 1.11.1 | 22.1.0 |
| 1.2.5 (master) | 1.11.2 | 22.5.0 |

Atmosphère 1.10.2 hat Basisunterstützung für 21.2.0 eingeführt; Hekate 6.5.0 unterstützt HOS 21.2.0.

**Kernregel:** uSystem linkt **Atmosphere-libs**. Die uSystem-Version muss zur **installierten Atmosphère-Version** passen, nicht nur zur Firmware. Submodule-Stände je Tag:

- 1.2.3 → Atmosphere-libs `9a8703e`
- 1.2.4 → `82f1553`
- 1.2.5 → `d3083af`

**Vorgehen [zu prüfen auf der Konsole]:**
1. Atmosphère-Version ablesen: *Systemeinstellungen → System*. Dort steht z. B. `21.2.0|AMS 1.10.2|E` (das letzte Zeichen: E = emuMMC, S = sysMMC). So siehst du auch gleich die MMC-Umgebung.
2. Bei **AMS 1.10.2** → NEXUS auf Basis **uLaunch 1.2.3** bauen (Submodule auf `9a8703e`).
3. Bei **AMS 1.11.x** (läuft auch auf FW 21.2.0) → Basis 1.2.4/1.2.5 mit passendem Submodule.
4. DeepSea liefert eine bestimmte AMS-Version mit; nach jedem DeepSea-Update muss NEXUS neu gebaut werden.

**Festgelegt (13.11.2026):** Konsole meldet `21.2.0|AMS 1.10.2|S` → Basis **uLaunch 1.2.3**, Atmosphere-libs `9a8703e`.

**Entscheidung:** Die Entwicklung läuft auf `master` (neuester UI-Code). Für das Deployment wird der Atmosphere-libs-Submodule per Build-Skript auf die installierte AMS-Version gesetzt (`scripts/select-ams.sh`).

---

## 4. Vorhandene Funktionen: Ordner, Themes, Anwendungserkennung

### Ordner **[verifiziert]**
- Einträge liegen pro Benutzer und MMC unter `sdmc:/ulaunch/menu/<sys|emu>_<account>/` als `<index>.m.json`.
- Typen (`EntryType`): `Application`, `Homebrew`, `Folder` und Spezialeinträge (Album, Einstellungen, Themes, Controller, Amiibo, Webbrowser, Mii, Benutzerseite).
- Ordner sind **echte Unterverzeichnisse** mit eigenen `.m.json`-Einträgen → Unterordner sind strukturell bereits möglich.
- `Entry::Remove()` auf einen Ordner **verschiebt den Inhalt in den Elternordner** und löscht nur das Verzeichnis. Genau das Verhalten, das NEXUS braucht (nichts wird deinstalliert).
- Pro Eintrag sind bereits `custom_name`, `custom_author`, `custom_version`, `custom_icon_path` möglich.
- Vorhanden: `MoveTo`, `MoveToIndex`, `OrderSwap`, `MoveToParentFolder`.
- **Fehlt:** Kachelgröße, Seitenverhältnis, Hero, Logo, Akzentfarbe, Favoriten, Mehrfachauswahl.

### Themes **[verifiziert]**
- `.ultheme` = ZIP mit `theme/Manifest.json`, `ui/UI.json`, `ui/Background.png`, `ui/Main/*`, `ui/Settings/*`, `sound/*`.
- `UI.json` definiert Farben und Elementpositionen in 1920×1080.
- **Fehlt:** Kachelstile, Layout-Vorgaben, Logo-Positionen, Fokuseffekte. NEXUS erweitert das Format um einen `nexus`-Block, statt es zu ersetzen.

### Anwendungserkennung **[verifiziert]**
- uSystem liest `NsExtApplicationRecord` und `NsExtApplicationView` (neue NS-Befehle seit 1.2.2) und cached Icon/NACP im RAM.
- `EnsureApplicationEntry(...)` legt fehlende Einträge automatisch an; `DeleteApplicationEntryRecursively` räumt deinstallierte Titel auf.
- Homebrew-Einträge verweisen auf `nro_path` plus `nro_argv`.

---

## 5. Implementierungsplan für die neue UI

Die NEXUS-UI wird **innerhalb von uMenu** als neues Layout umgesetzt (`NexusHomeLayout`), nicht als separate App. Plutonium bleibt die Render-Basis, weil es auf SDL2 sitzt und die Texturverwaltung (`pu::sdl2::Texture`) mitbringt.

Schritte:

1. **Datenmodell zuerst** (`nexus::model`): Tile, Folder, Artwork, Theme. Dieselbe JSON-Struktur wie im PC-Prototyp (`prototype/nexus-prototype.html` → `NexusData`). Prototyp und Switch teilen also das Format.
2. **LayerStack** als eigenes `pu::ui::elm::Element`, das 6 Ebenen in fester Reihenfolge zeichnet:
   1. Hero-Hintergrund
   2. Hintergrundeffekte (Abdunkelung, Verlauf; Unschärfe als **vorberechnete** verkleinerte Textur, kein Echtzeit-Blur)
   3. schwebendes Logo
   4. Kacheln
   5. Fokus und Navigation
   6. Dialoge
3. **FocusPresentationManager** hängt an `menu_FocusedEntryChanged`. Er überblendet zwei Hero-Texturen (A/B-Puffer, Alpha-Rampe) und animiert das Logo unabhängig von der Kachelposition.
4. **TileLayoutEngine** ersetzt `EntryMenu`: Belegungsraster in Zellen, First-Fit-Platzierung, Kollisionsprüfung, räumliche Navigation (nächster Kachelmittelpunkt in Richtung, gewichtet nach Achsabweichung). Die Logik ist im Prototyp bereits implementiert und wird 1:1 nach C++ portiert.
5. Kontextmenü (X) und Kachel-Editor als Plutonium-Dialoge mit Live-Vorschau auf einer Kopie der Kachel (Übernehmen, Abbrechen, Zurücksetzen).
6. Die bestehenden uMenu-Layouts (Settings, Themes, Lockscreen, Startup) bleiben zunächst unverändert erhalten.

**Speicherbudget [zu prüfen]:** uMenu läuft als Library-Applet und hat deutlich weniger RAM als eine Anwendung. Ein Hero in 1920×1080 RGBA belegt etwa 8 MB. Deshalb:
- Heroes auf der SD-Karte auf **1280×720 JPEG** normalisieren (≈ 3,5 MB als Textur).
- Höchstens drei Heroes gleichzeitig im Speicher (aktueller, vorheriger, ein vorgeladener Nachbar).
- Logos höchstens 800×310 PNG.
- Echte Speichermessung in Phase 3 mit `svcGetInfo(TotalMemorySize/UsedMemorySize)`.

---

## 6. Modulare Projektstruktur

```
projects/uMenu/
  include/nexus/            source/nexus/
    model/      Tile, Folder, Artwork, ThemeProfile, NexusData (JSON-Serialisierung)
    store/      DataStore (atomare Writes, Migrationen, Backup)
    layout/     TileLayoutEngine, Navigation
    present/    FocusPresentationManager, LayerStack, Animators
    artwork/    ArtworkManager, TextureCache (LRU, Budget in Bytes), AsyncDecoder
    sgdb/       SteamGridDBClient (curl, JSON, Rate-Limit-Backoff)
    theme/      ThemeManager (lädt .ultheme + nexus-Block)
    app/        ApplicationManager (Wrapper um menu_Entries + Forwarder-Klassifikation)
    sys/        SystemAdapter (einziger Pfad zu SMI/uSystem)
    ui/         NexusHomeLayout, ContextMenu, TileEditor, SettingsPages, OnScreenKeyboard
nexus/
  docs/         Analyse, Entscheidungen, Fortschritt
  prototype/    PC-Prototyp (HTML)
  scripts/      Build- und Deploy-Skripte
```

**SD-Daten (getrennt von uLaunch, damit dessen Daten unangetastet bleiben):**

```
sdmc:/nexus/
  nexus.json          Einstellungen + Bibliothek (versioniert, "schema": 1)
  nexus.json.bak      letzte gute Version
  secrets.json        SteamGridDB-API-Key (nie geloggt)
  art/<titleid>/      grid.jpg, hero.jpg, logo.png, icon.png
  art/folder-<uuid>/
  themes/*.nxtheme.json
  backups/<datum>.zip
```

**Datenhaltung – Entscheidung:** nur **JSON**, kein SQLite. Selbst 500 Titel ergeben weniger als 1 MB JSON. nlohmann/json ist bereits eingebunden, und SQLite würde eine weitere Abhängigkeit plus Port-Risiko bringen. Atomares Schreiben läuft über `nexus.json.tmp` → `fsync` → `rename`, die vorherige Version bleibt als `.bak` erhalten. Bei jeder Änderung des Schemas wird `schema` hochgezählt und eine Migrationsfunktion pro Stufe ausgeführt.

**Stabile IDs:** Titel werden über die Titel-ID identifiziert, Homebrew über einen Hash des NRO-Pfads, Ordner über eine UUID. Die uLaunch-`index`-Werte werden **nicht** als ID verwendet, weil sie sich beim Sortieren ändern.

---

## 7. Plan für die SteamGridDB-Integration

API v2, Basis `https://www.steamgriddb.com/api/v2`, Authentifizierung per `Authorization: Bearer <key>`. **[zu prüfen gegen die aktuelle Doku vor Phase 5]** Die Doku-Seite war aus dieser Umgebung nicht abrufbar; die Endpunkte unten stammen aus der bekannten v2-API.

| Schritt | Endpunkt |
|---|---|
| Spiel suchen | `GET /search/autocomplete/{term}` |
| Cover | `GET /grids/game/{id}?dimensions=600x900,460x215,920x430&types=static` |
| Hero | `GET /heroes/game/{id}?types=static` |
| Logo | `GET /logos/game/{id}?types=static` |
| Icon | `GET /icons/game/{id}?types=static` |

Weitere Festlegungen:
- **Nur `static`**, keine animierten Formate (APNG/WebM sprengen das RAM-Budget). `nsfw=false` ist Standard.
- **Ablauf:** Suchbegriff aus dem NACP-Namen (bereinigt um ™/®), Ergebnisliste, Bildtyp wählen, Vorschauraster mit den `thumb`-URLs, Download des Originals in einen eigenen Worker-Thread, Normalisierung (Größe/JPEG) mit stb, Ablage unter `art/<id>/`, Zuordnung atomar speichern, Textur neu laden.
- **Vorschau-Regel:** Hero → Vorschau der gesamten Hintergrundfläche. Logo → transparent über dem aktuell zugewiesenen Hero. Grid → nur die Kachel.
- **Robustheit:** Timeout 10 s, maximal zwei parallele Downloads, exponentielles Backoff bei HTTP 429. Netzwerkfehler erscheinen als Toast und blockieren nie den UI-Thread.
- **API-Key:** Eingabe über die Bildschirmtastatur (die swkbd-Integration von uMenu ist seit 1.2.3 für alle Tastaturen gefixt) und Speicherung in `secrets.json`. **Hinweis an den Nutzer:** Auf einer modifizierten Konsole kann jede Homebrew die SD-Karte lesen. Der Key ist dort also nicht geheim, nur nicht im Code und nicht in Logs.
- **Risiko [zu prüfen, früh als Spike]:** Ob uMenu im Library-Applet-Kontext (über Album) `nifm` und `bsd:u`-Sockets öffnen darf. **Alternative**, falls nicht: Die Downloads übernimmt eine separate NRO „NEXUS Artwork Fetcher“ (läuft als Anwendung mit vollen Rechten) und tauscht Aufträge und Ergebnisse über `sdmc:/nexus/queue/` aus.
- **Nutzungsbedingungen:** Die Bilder sind Nutzerbeiträge auf SteamGridDB. Sie werden nur lokal gecacht und **nicht** in exportierte Themes eingebettet; der Theme-Export enthält nur Layout und Stil.

---

## 8. Strategie für TICO-Forwarder

- Forwarder sind installierte NSP-Titel mit eigener Titel-ID. Sie erscheinen in uLaunch daher als ganz normale `EntryType::Application` und werden über `LaunchApplication` gestartet wie Spiele. **[verifiziert anhand der Architektur; Startverhalten zu prüfen]**
- NEXUS braucht deshalb **keinen Sonderpfad** zum Starten. Alle Anpassungen (Cover, Hero, Logo, Größe) hängen an der Titel-ID wie bei nativen Spielen.
- Optionale Kennzeichnung als „Retro“ per Heuristik [zu prüfen]: NACP-Publisher oder -Autor, den TICO beim Erstellen des Forwarders setzt, oder Titel-ID-Bereich (`05…`). Nur für Filter und Sortierung, nie für die Startlogik.
- **Testplan auf der Konsole:** (a) Erscheint der Forwarder in uLaunch 1.2.x? (b) Startet er über uLaunch? (c) Kehrt er sauber zum Menü zurück? (d) Funktioniert Suspend und Resume? Ergebnis in `docs/TESTPROTOKOLL.md` dokumentieren.
- NEXUS verändert weder TICO noch dessen Konfiguration, ROMs oder Spielstände.

---

## 9. Priorisierte Aufgabenliste

| # | Aufgabe | Phase | Status |
|---|---|---|---|
| 1 | AMS-Version und MMC auf der Konsole ablesen | 1 | **erledigt: FW 21.2.0, AMS 1.10.2, sysMMC → Basis uLaunch 1.2.3** |
| 2 | Unverändertes uLaunch passend zur AMS-Version bauen (Docker/WSL2) | 1 | Skript bereit, Build offen |
| 3 | PC-Prototyp: Layer-Stack, Layout-Engine, Ordner, Editor | 2 | **erledigt (v0.1)** |
| 4 | Datenformat `NexusData` v1 festschreiben | 2 | im Prototyp definiert |
| 5 | Spike: Speicherbudget uMenu + Sockets im Applet | 3 | offen |
| 6 | C++: model/store (JSON, atomar, Migration) | 3 | offen |
| 7 | C++: TileLayoutEngine + Navigation (Port aus Prototyp) | 3 | offen |
| 8 | C++: LayerStack + FocusPresentationManager | 3 | offen |
| 9 | Kontextmenü + Kachel-Editor + Ordnerverwaltung | 4 | offen |
| 10 | Globale Einstellungen + ThemeManager | 4 | offen |
| 11 | SteamGridDB-Client + Bildbrowser + lokaler Bildbrowser | 5 | offen |
| 12 | TICO-Testprotokoll | 6 | offen |
| 13 | TextureCache-Tuning, Backups, Stresstest mit 300 Einträgen | 7 | offen |
| 14 | Aktivierung als HOME-Menü auf emuMMC | 8 | offen |

---

## 10. Sicherheit und Wiederherstellung

1. **Kein emuMMC vorhanden (Stand 13.11.2026):** NEXUS läuft bis Phase 7 als eigenständige Homebrew-App (NRO) auf sysMMC und verändert dabei das System nicht. Details und Schutzmechanismen: `STABILITAET.md`. Vor Phase 8 entscheiden: emuMMC anlegen oder kontrolliert auf sysMMC aktivieren.
2. **NAND-Backup** (Hekate → Tools → Backup eMMC, BOOT0/1 + RAW GPP) vor der ersten Aktivierung.
3. **SD-Backup** von `atmosphere/contents/0100000000001000/` und `ulaunch/` vor jeder Installation.
4. Zurück zum Original-HOME-Menü:
   - per uManager deaktivieren, oder
   - am PC **nur** `atmosphere/contents/0100000000001000/exefs.nsp` löschen. Nicht den ganzen Ordner, sonst gehen NXThemes verloren.
5. NEXUS schreibt seine Daten ausschließlich nach `sdmc:/nexus/`. uLaunch-Daten werden nur gelesen. Ein Rückfall auf vanilla uLaunch bleibt dadurch jederzeit möglich.
6. Bis Phase 8 läuft NEXUS als **Testvariante**: separates uMenu-Build, das nur über einen Debug-Schalter aktiv wird. Bei einem Absturz beim Start lädt uSystem nach zwei Fehlstarts das unveränderte uMenu (umzusetzen in Phase 8, vorhandenes `PreviousLaunchFailure` in `smi_Protocol.hpp` nutzen).
7. **Lizenz:** uLaunch ist **GPLv2**, also muss NEXUS als abgeleitetes Werk ebenfalls GPLv2 sein und den Quellcode mitliefern. Plutonium: MIT. nlohmann/json, imgui: MIT. stb: Public Domain/MIT.
8. Keine erfundenen System-APIs: Alle Systemaufrufe gehen über bestehende uLaunch-SMI-Befehle. Neue Befehle werden nur hinzugefügt, wenn das zugrunde liegende libnx/NS-Kommando auf switchbrew.org dokumentiert ist.

---

## Build-Status in dieser Umgebung

- Repository geklont, Submodule Plutonium und libnx-ext geholt, Branch `nexus-home` angelegt.
- **Kompilierung hier nicht möglich:** In der Analyseumgebung ist kein devkitPro installiert, und Paketinstallationen sind gesperrt. Der Build muss auf deinem Windows-PC laufen → `nexus/scripts/build-ulaunch.sh` (WSL2 oder Docker-Image `devkitpro/devkita64`).
