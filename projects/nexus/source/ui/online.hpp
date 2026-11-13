#pragma once
#include "../core/online_parse.hpp"
#include "../core/store.hpp"
#include "../nx/net.hpp"
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus {

    // SteamGridDB + RetroAchievements. Läuft im Hauptthread (Tick pro Frame), die eigentlichen
    // Abfragen im Netzwerk-Thread. Alles ist optional: ohne Schlüssel oder Netz passiert nichts.
    class Online {
        public:
            using Cb = std::function<void(const NetResult &)>;

            void Init(NexusData *d);
            void Tick();
            void Shutdown();
            int Pending() { return this->net.Pending(); }
            static bool IsOnline() { return Net::Online(); }

            // ---------------- SteamGridDB
            bool HasSgdbKey() const;
            void SgdbSearch(const std::string &term, std::function<void(std::vector<SgdbGame>, std::string)> done);
            // kind: grids | heroes | logos | icons
            void SgdbImages(const std::string &kind, long long gameId, std::function<void(std::vector<SgdbImage>, std::string)> done);
            void Download(const std::string &url, const std::string &path, std::function<void(bool, std::string)> done, size_t maxBytes = 12 * 1024 * 1024);
            std::string ThumbPath(const SgdbImage &im) const;
            // Vorschaubild laden, falls noch nicht vorhanden (true = liegt bereits vor)
            bool EnsureThumb(const SgdbImage &im);
            // Fehlende Bilder eines Titels automatisch laden (erstes passendes Ergebnis)
            void QueueAutoArt(const std::string &id, const std::string &name, bool grid, bool hero, bool logo, long long gameId = 0, bool force = false);
            int AutoRemaining() const { return static_cast<int>(this->autoQueue.size()) + (this->autoBusy ? 1 : 0); }
            std::function<void(const std::string &id)> onArtChanged;
            std::function<void(const std::string &file)> onFileObsolete;   // ersetzte Download-Datei löschen
            std::function<void(const std::vector<RaRecent> &)> onNewAchievements;

            // ---------------- RetroAchievements
            struct RaState {
                RaProfile profile;
                std::vector<RaGame> games;
                std::vector<RaRecent> recent;
                std::time_t fetched = 0;
                bool loading = false;
                std::string error;
                long long trophies = 0, trophiesHard = 0;
            } ra;
            bool RaReady() const;
            void RaRefresh(bool force);
            // Fortschritt für einen Titel: feste Zuordnung (raGameId) oder automatischer Titelvergleich
            const RaGame *RaGameFor(const std::string &tid, const std::string &name);
            void RaLoadGame(long long gameId, std::function<void(RaGame, std::vector<RaAchievement>, std::string)> done);
            long long RaPoints() const;
            long long RaTrophies() const;

        private:
            Net net;
            NexusData *d = nullptr;
            std::unordered_map<int, Cb> cbs;
            std::set<std::string> thumbsRequested;
            std::unordered_map<std::string, long long> raMap;     // tid -> RA-Spiel (0 = keins)
            std::unordered_map<long long, RaGame> raExtra;        // zugeordnete Spiele außerhalb der Liste
            std::set<long long> raExtraRequested;
            int raPendingParts = 0;
            bool raNextOk = true;
            std::time_t lastRaTry = 0;
            std::string raSeen;   // Datum des neuesten bereits gemeldeten Erfolgs

            struct AutoJob { std::string id, name; bool grid, hero, logo; long long gid; };
            std::deque<AutoJob> autoQueue;
            bool autoBusy = false;
            std::set<std::string> autoTried;

            int Request(const std::string &url, const std::vector<std::string> &headers, const std::string &logName, const std::string &toFile, Cb cb, size_t maxBytes = 4 * 1024 * 1024);
            std::vector<std::string> SgdbHeaders() const;
            std::string RaUrl(const std::string &endpoint, const std::string &extra) const;
            void RaFinishPart();
            void RaLoadCache();
            void RaSaveCache();
            void RunAuto();
            void AutoStep(AutoJob job, long long gameId, int stage);
    };

}
