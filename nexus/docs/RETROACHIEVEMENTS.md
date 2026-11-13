# NEXUS HOME – RetroAchievements-Anbindung

## Rollenverteilung

- **Freischalten** von Erfolgen passiert weiterhin im Emulator. TICO 0.8.0 hat eine eigene RetroAchievements-Anmeldung pro Benutzerkonto. NEXUS greift in diesen Vorgang nicht ein und verändert nichts an TICO.
- **NEXUS zeigt nur an:** Es liest den Fortschritt über die offizielle Web-API von retroachievements.org, nur lesend.

## Zugangsdaten

- Benutzername und **Web-API-Key**: retroachievements.org → Einstellungen → Abschnitt „Keys“.
- Werden in `sdmc:/nexus/nexus.json` unter `settings.ra.user` und `settings.ra.key` gespeichert.
- Erscheinen nie im Log und sind nicht Teil exportierter Themes.
- Hinweis wie bei SteamGridDB: Auf einer modifizierten Konsole kann jede Homebrew die SD-Karte lesen.

## Verwendete Endpunkte (Web-API v1, Abfrage per GET, Parameter `y` = Web-API-Key)

| Zweck | Endpunkt |
|---|---|
| Fortschritt eines Spiels inkl. Erfolgsliste | `API_GetGameInfoAndUserProgress.php?g=<Spiel-ID>&u=<Benutzer>` |
| Automatische Zuordnung (Titelvergleich) | `API_GetUserRecentlyPlayedGames.php?u=<Benutzer>&c=50` |
| Profil, Punkte, letzte Erfolge | `API_GetUserSummary.php` / `API_GetUserProfile.php` (Felder vor Phase 5 gegen die Doku prüfen) |

## Zuordnung Titel ↔ RetroAchievements-Spiel

- **Automatisch:** Der Name des Forwarders wird mit den zuletzt gespielten Spielen des Kontos verglichen. Dabei werden Groß-/Kleinschreibung, Sonderzeichen und Zusätze wie „(USA)“ ignoriert. Nur eindeutige Treffer werden übernommen.
- **Manuell:** Kontextmenü (X) → „RetroAchievements…“ → Spiel-ID eintragen. Die ID ist die Zahl in der Spiel-URL auf retroachievements.org.
- Gespeichert wird `apps.<Titel-ID>.raGameId`. Native Switch-Spiele haben keine RetroAchievements und bleiben unberührt.

## Anzeige (alles abschaltbar, Einstellungsgruppe „RetroAchievements“)

- **Kacheln:** Balken, Prozent oder Pokal mit Anzahl. Gemeisterte Spiele werden golden dargestellt.
- **Fokus:** „🏆 12 / 40 Erfolge · 30 %“ mit Balken unter dem schwebenden Logo. Liegt in Ebene 3 und bewegt sich mit dem Logo, nie mit der Kachel.
- **Kopfzeile:** Benutzername und Punkte.
- **Sperrbildschirm:** zuletzt freigeschalteter Erfolg.
- **Kontextmenü:** vollständige Erfolgsliste des Spiels.

## Stabilität

- Abfragen laufen im gemeinsamen Netzwerk-Thread mit SteamGridDB: 10 s Timeout, Backoff bei HTTP 429.
- Aktualisierung höchstens alle `ra.refreshMin` Minuten (Standard 15) und beim Fokussieren nur aus dem Cache.
- Antworten werden unter `sdmc:/nexus/cache/ra/<Spiel-ID>.json` gespeichert. Offline wird der zuletzt bekannte Stand angezeigt.
- Fehlende Zugangsdaten, kein Netz oder ungültige Antworten blenden die Anzeige einfach aus.

## Umsetzung

Phase 5, zusammen mit SteamGridDB, weil beide denselben HTTPS-Client (curl + mbedTLS) nutzen. Einstellungen und Validierung sind bereits im Schema (`settings-schema.json`) und in der Switch-App enthalten.
