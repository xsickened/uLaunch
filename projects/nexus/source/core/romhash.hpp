#pragma once
// NEXUS HOME: Spielerkennung wie RetroAchievements (rcheevos): MD5 der ROM nach festen Regeln je System.
// So erkennen auch Emulatoren wie tico oder RetroArch ein Spiel – unabhängig vom Dateinamen.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus {

    // MD5 (Streaming)
    class Md5 {
        public:
            Md5();
            void Update(const void *data, size_t len);
            std::string HexDigest();   // beendet die Berechnung
        private:
            uint32_t a, b, c, d;
            uint64_t total = 0;
            uint8_t buf[64];
            size_t bufLen = 0;
            void Block(const uint8_t *p);
    };
    std::string Md5Hex(const void *data, size_t len);

    // System aus Ordnername (tico/RetroArch-Kürzel) oder Dateiendung: "nes", "snes", "n64", "gb", "gba", "nds", "md", "arcade", …
    // Leer = unbekannt.
    std::string RaSystemFor(const std::string &folder, const std::string &ext);
    // RetroAchievements-Konsolen-ID für ein System (0 = unbekannt)
    int RaConsoleId(const std::string &system);
    // Wird dieses System über den Dateiinhalt erkannt? (Disc-Systeme wie PS1/PS2/GameCube: nein)
    bool RaHashSupported(const std::string &system);

    // Prüfwert einer ROM-Datei (streamt, braucht wenig Speicher). Leer = nicht möglich.
    std::string RaHashFile(const std::string &path, const std::string &system);
    // Prüfwert aus einem Speicherpuffer (z. B. aus einer ZIP-Datei entpackt)
    std::string RaHashBuffer(const uint8_t *data, size_t size, const std::string &system, const std::string &fileName);

    // Antwort von dorequest.php?r=gameid lesen (0 = unbekannt)
    long long ParseRaGameIdResponse(const std::string &body);

    // Titel aus einem ROM-Dateinamen ("Legend of Zelda, The - Link's Awakening (USA).gb" -> "The Legend of Zelda - Link's Awakening")
    std::string RomTitleFromFile(const std::string &fileName);

}
