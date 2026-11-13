#pragma once
#include "settings.hpp"
#include <string>
#include <vector>

// Auswertung der Antworten von SteamGridDB (API v2) und RetroAchievements (Web-API v1).
// Rein und ohne Netz, damit alles am PC getestet werden kann. Ungültige Antworten liefern false,
// nie eine Ausnahme (json::parse ohne Ausnahmen).
namespace nexus {

    struct SgdbGame {
        long long id = 0;
        std::string name;
        bool verified = false;
    };

    struct SgdbImage {
        long long id = 0;
        std::string url, thumb, mime, style;
        int w = 0, h = 0;
    };

    struct RaProfile {
        std::string user;
        long long hardcore = 0, softcore = 0, retro = 0;
        bool ok = false;
    };

    struct RaGame {
        long long id = 0;
        std::string title, console;
        int max = 0, got = 0, gotHard = 0;
        std::string lastDate;
    };

    struct RaRecent {
        std::string title, game, date;
        long long gameId = 0;
        int points = 0;
        bool hardcore = false;
    };

    struct RaAchievement {
        std::string title, desc;
        int points = 0;
        bool unlocked = false, hardcore = false;
        int order = 0;
    };

    std::string UrlEncode(const std::string &s);
    // Suchbegriff aus einem Spielnamen (™ ® © und überflüssige Leerzeichen entfernen)
    std::string SearchTerm(const std::string &name);
    // Dateiendung passend zum Bildtyp (.png/.jpg/.webp), Standard .jpg
    std::string ImageExt(const std::string &mime, const std::string &url);

    bool ParseSgdbGames(const std::string &body, std::vector<SgdbGame> &out, std::string &err);
    bool ParseSgdbImages(const std::string &body, std::vector<SgdbImage> &out, std::string &err);

    bool ParseRaProfile(const std::string &body, RaProfile &out);
    bool ParseRaCompletion(const std::string &body, std::vector<RaGame> &out);
    bool ParseRaRecent(const std::string &body, std::vector<RaRecent> &out);
    // Fortschritt eines einzelnen Spiels inkl. Erfolgsliste
    bool ParseRaGameProgress(const std::string &body, RaGame &game, std::vector<RaAchievement> &list);

    // Titelvergleich: Kleinbuchstaben, Akzente entfernt, Klammerzusätze wie „(USA)“ und „the“ ignoriert
    std::string NormalizeTitle(const std::string &s);
    // Liefert die RA-Spiel-ID bei eindeutigem Treffer, sonst 0
    long long MatchRaGame(const std::string &name, const std::vector<RaGame> &games);

    // JSON-Helfer: Feld ohne Beachtung der Groß-/Kleinschreibung (die RA-Doku zeigt camelCase, die API liefert PascalCase)
    const json *JGetCI(const json &obj, const char *key);

}
