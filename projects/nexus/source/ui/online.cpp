#include "online.hpp"
#include <algorithm>
#include <cstdio>
#include <dirent.h>

namespace nexus {

    namespace {

        constexpr char SgdbBase[] = "https://www.steamgriddb.com/api/v2";
        constexpr char RaBase[] = "https://retroachievements.org/API/";
        constexpr char RaCacheFile[] = "sdmc:/nexus/cache/ra.json";
        constexpr char ThumbDir[] = "sdmc:/nexus/cache/sgdb";

        void ClearDir(const std::string &dir) {
            DIR *d = opendir(dir.c_str());
            if(!d) return;
            std::vector<std::string> files;
            while(dirent *e = readdir(d)) {
                const std::string n = e->d_name;
                if(n != "." && n != "..") files.push_back(dir + "/" + n);
            }
            closedir(d);
            for(const auto &f: files) std::remove(f.c_str());
        }

    }

    void Online::Init(NexusData *data) {
        this->d = data;
        EnsureDir(CacheDir);
        EnsureDir(ThumbDir);
        ClearDir(ThumbDir);   // Vorschaubilder nur für die laufende Sitzung
        this->RaLoadCache();
    }

    void Online::Shutdown() {
        this->net.Stop();
    }

    int Online::Request(const std::string &url, const std::vector<std::string> &headers, const std::string &logName, const std::string &toFile, Cb cb, size_t maxBytes) {
        const int id = this->net.Get(url, headers, logName, toFile, maxBytes);
        if(id == 0) {
            NetResult r;
            r.error = "Warteschlange voll";
            if(cb) cb(r);
            return 0;
        }
        this->cbs[id] = std::move(cb);
        return id;
    }

    void Online::Tick() {
        for(auto &r: this->net.Poll()) {
            auto it = this->cbs.find(r.id);
            if(it == this->cbs.end()) continue;
            Cb cb = std::move(it->second);
            this->cbs.erase(it);
            if(cb) cb(r);
        }
        // RetroAchievements regelmäßig aktualisieren
        if(this->RaReady() && !this->ra.loading) {
            const std::time_t now = std::time(nullptr);
            const double every = std::max(5.0, this->d->settings.N("ra.refreshMin")) * 60.0;
            if(now - this->ra.fetched >= every && now - this->lastRaTry >= 60) this->RaRefresh(false);
        }
        if(!this->autoBusy && !this->autoQueue.empty()) this->RunAuto();
    }

    // ------------------------------------------------------------------ SteamGridDB

    bool Online::HasSgdbKey() const {
        return this->d && this->d->settings.S("sgdbKey").size() >= 8;
    }

    std::vector<std::string> Online::SgdbHeaders() const {
        return { "Authorization: Bearer " + this->d->settings.S("sgdbKey"), "Accept: application/json" };
    }

    void Online::SgdbSearch(const std::string &term, std::function<void(std::vector<SgdbGame>, std::string)> done) {
        if(!this->HasSgdbKey()) { done({}, "Kein SteamGridDB-API-Key hinterlegt."); return; }
        const std::string t = SearchTerm(term);
        if(t.empty()) { done({}, "Leerer Suchbegriff."); return; }
        this->Request(std::string(SgdbBase) + "/search/autocomplete/" + UrlEncode(t), this->SgdbHeaders(), "SGDB Suche", "", [done](const NetResult &r) {
            std::vector<SgdbGame> games;
            std::string err;
            if(!r.ok && r.body.empty()) { done({}, r.status == 401 ? "API-Key ungültig (HTTP 401)." : r.error); return; }
            if(!ParseSgdbGames(r.body, games, err)) { done({}, r.status == 401 ? "API-Key ungültig (HTTP 401)." : err); return; }
            done(games, "");
        });
    }

    void Online::SgdbImages(const std::string &kind, long long gameId, std::function<void(std::vector<SgdbImage>, std::string)> done) {
        if(!this->HasSgdbKey()) { done({}, "Kein SteamGridDB-API-Key hinterlegt."); return; }
        std::string q = "?types=static&nsfw=false&humor=any";
        if(kind == "grids") {
            const auto dims = this->d->settings.S("sgdb.gridDims");
            if(dims != "any") q += "&dimensions=" + dims;
            q += "&mimes=image/png,image/jpeg";
        }
        else if(kind == "heroes") q += "&mimes=image/png,image/jpeg";
        else if(kind == "logos") q += "&mimes=image/png";
        this->Request(std::string(SgdbBase) + "/" + kind + "/game/" + std::to_string(gameId) + q, this->SgdbHeaders(), "SGDB " + kind, "", [done](const NetResult &r) {
            std::vector<SgdbImage> list;
            std::string err;
            if(!r.ok && r.body.empty()) { done({}, r.error); return; }
            if(!ParseSgdbImages(r.body, list, err)) { done({}, err); return; }
            done(list, "");
        });
    }

    void Online::Download(const std::string &url, const std::string &path, std::function<void(bool, std::string)> done, size_t maxBytes) {
        this->Request(url, {}, "Bild laden", path, [done](const NetResult &r) { if(done) done(r.ok, r.error); }, maxBytes);
    }

    std::string Online::ThumbPath(const SgdbImage &im) const {
        return std::string(ThumbDir) + "/" + std::to_string(im.id) + ImageExt("", im.thumb);
    }

    bool Online::EnsureThumb(const SgdbImage &im) {
        const auto p = this->ThumbPath(im);
        if(FileExists(p)) return true;
        if(this->thumbsRequested.count(p)) return false;
        this->thumbsRequested.insert(p);
        this->Download(im.thumb, p, nullptr, 4 * 1024 * 1024);
        return false;
    }

    void Online::QueueAutoArt(const std::string &id, const std::string &name, bool grid, bool hero, bool logo, long long gameId, bool force) {
        if(!this->HasSgdbKey() || (!grid && !hero && !logo)) return;
        if(this->autoTried.count(id) && !force) return;
        this->autoTried.insert(id);
        this->autoQueue.push_back(AutoJob{ id, name, grid, hero, logo, gameId });
    }

    void Online::RunAuto() {
        AutoJob job = this->autoQueue.front();
        this->autoQueue.pop_front();
        this->autoBusy = true;
        if(job.gid > 0) { this->AutoStep(job, job.gid, 0); return; }
        this->SgdbSearch(job.name, [this, job](std::vector<SgdbGame> games, std::string err) {
            if(games.empty()) {
                Log("SGDB automatisch: kein Treffer für %s %s", job.id.c_str(), err.c_str());
                this->autoBusy = false;
                return;
            }
            // verifizierte Einträge bevorzugen, sonst das erste Ergebnis
            long long gid = games[0].id;
            for(const auto &g: games) if(g.verified && NormalizeTitle(g.name) == NormalizeTitle(job.name)) { gid = g.id; break; }
            this->AutoStep(job, gid, 0);
        });
    }

    void Online::AutoStep(AutoJob job, long long gameId, int stage) {
        static const char *const kinds[] = { "grids", "heroes", "logos" };
        static const char *const keys[] = { "grid", "hero", "logo" };
        while(stage < 3 && !((stage == 0 && job.grid) || (stage == 1 && job.hero) || (stage == 2 && job.logo))) stage++;
        if(stage >= 3) { this->autoBusy = false; return; }
        this->SgdbImages(kinds[stage], gameId, [this, job, gameId, stage](std::vector<SgdbImage> list, std::string) {
            if(list.empty()) { this->AutoStep(job, gameId, stage + 1); return; }
            const SgdbImage im = list[0];
            const std::string dir = std::string(ArtDir) + "/" + job.id;
            EnsureDir(dir);
            const std::string path = dir + "/sgdb-" + keys[stage] + "-" + std::to_string(im.id) + ImageExt(im.mime, im.url);
            this->Download(im.url, path, [this, job, gameId, stage, path](bool ok, std::string) {
                if(ok) {
                    this->d->library.SetArtOverride(job.id, keys[stage], path);
                    if(this->onArtChanged) this->onArtChanged(job.id);
                }
                this->AutoStep(job, gameId, stage + 1);
            });
        });
    }

    // ------------------------------------------------------------------ RetroAchievements

    bool Online::RaReady() const {
        return this->d && this->d->settings.B("ra.enabled") && !this->d->settings.S("ra.user").empty() && this->d->settings.S("ra.key").size() >= 8;
    }

    std::string Online::RaUrl(const std::string &endpoint, const std::string &extra) const {
        return std::string(RaBase) + endpoint + ".php?u=" + UrlEncode(this->d->settings.S("ra.user")) + "&y=" + UrlEncode(this->d->settings.S("ra.key")) + extra;
    }

    void Online::RaRefresh(bool force) {
        if(!this->RaReady() || this->ra.loading) return;
        const std::time_t now = std::time(nullptr);
        if(!force && now - this->lastRaTry < 60) return;
        this->lastRaTry = now;
        this->ra.loading = true;
        this->ra.error.clear();
        this->raPendingParts = 3;
        this->raNextOk = true;
        this->Request(this->RaUrl("API_GetUserProfile", ""), {}, "RA Profil", "", [this](const NetResult &r) {
            RaProfile p;
            if(r.ok && ParseRaProfile(r.body, p)) this->ra.profile = p;
            else { this->raNextOk = false; this->ra.error = r.status == 401 ? "Benutzername oder Web-API-Key falsch." : (r.error.empty() ? "Profil nicht lesbar." : r.error); }
            this->RaFinishPart();
        });
        this->Request(this->RaUrl("API_GetUserCompletionProgress", "&c=500"), {}, "RA Fortschritt", "", [this](const NetResult &r) {
            std::vector<RaGame> g;
            if(r.ok && ParseRaCompletion(r.body, g)) { this->ra.games = g; this->raMap.clear(); }
            else this->raNextOk = false;
            this->RaFinishPart();
        }, 8 * 1024 * 1024);
        this->Request(this->RaUrl("API_GetUserRecentAchievements", "&m=10080"), {}, "RA Erfolge", "", [this](const NetResult &r) {
            std::vector<RaRecent> rec;
            if(r.ok && ParseRaRecent(r.body, rec)) this->ra.recent = rec;
            this->RaFinishPart();
        });
    }

    void Online::RaFinishPart() {
        if(--this->raPendingParts > 0) return;
        this->ra.loading = false;
        long long t = 0, th = 0;
        for(const auto &g: this->ra.games) { t += g.got; th += g.gotHard; }
        this->ra.trophies = t;
        this->ra.trophiesHard = th;
        if(this->raNextOk) {
            this->ra.fetched = std::time(nullptr);
            this->RaSaveCache();
        }
    }

    long long Online::RaPoints() const {
        const auto type = this->d->settings.S("ra.pointsType");
        if(type == "softcore") return this->ra.profile.softcore;
        if(type == "retro") return this->ra.profile.retro;
        return this->ra.profile.hardcore;
    }

    long long Online::RaTrophies() const {
        return this->d->settings.S("ra.pointsType") == "hardcore" ? this->ra.trophiesHard : this->ra.trophies;
    }

    const RaGame *Online::RaGameFor(const std::string &tid, const std::string &name) {
        if(!this->d->settings.B("ra.enabled")) return nullptr;
        long long gid = this->d->library.RaGameId(tid);
        if(gid <= 0 && this->d->settings.S("ra.mapping") == "auto") {
            auto it = this->raMap.find(tid);
            if(it == this->raMap.end()) it = this->raMap.emplace(tid, MatchRaGame(name, this->ra.games)).first;
            gid = it->second;
        }
        if(gid <= 0) return nullptr;
        for(const auto &g: this->ra.games) if(g.id == gid) return &g;
        auto ex = this->raExtra.find(gid);
        if(ex != this->raExtra.end()) return &ex->second;
        // fest zugeordnet, aber noch nie gespielt: einmal einzeln abfragen
        if(this->RaReady() && !this->raExtraRequested.count(gid)) {
            this->raExtraRequested.insert(gid);
            this->RaLoadGame(gid, [this, gid](RaGame g, std::vector<RaAchievement>, std::string err) { if(err.empty()) this->raExtra[gid] = g; });
        }
        return nullptr;
    }

    void Online::RaLoadGame(long long gameId, std::function<void(RaGame, std::vector<RaAchievement>, std::string)> done) {
        if(!this->RaReady()) { done({}, {}, "RetroAchievements ist nicht eingerichtet."); return; }
        this->Request(this->RaUrl("API_GetGameInfoAndUserProgress", "&g=" + std::to_string(gameId)), {}, "RA Spiel", "", [done, gameId](const NetResult &r) {
            RaGame g;
            g.id = gameId;
            std::vector<RaAchievement> list;
            if(!r.ok || !ParseRaGameProgress(r.body, g, list)) { done(g, {}, r.error.empty() ? "Antwort nicht lesbar." : r.error); return; }
            done(g, list, "");
        }, 8 * 1024 * 1024);
    }

    void Online::RaLoadCache() {
        std::string s;
        if(!ReadFile(RaCacheFile, s, 8 * 1024 * 1024)) return;
        const json j = json::parse(s, nullptr, false);
        if(j.is_discarded() || !j.is_object()) return;
        // Cache gehört zu genau einem Benutzer
        if(JStr(j, "user", "") != this->d->settings.S("ra.user")) return;
        if(const json *p = JGet(j, "profile")) ParseRaProfile(SafeDump(*p), this->ra.profile);
        if(const json *g = JGet(j, "games")) ParseRaCompletion(SafeDump(*g), this->ra.games);
        if(const json *r = JGet(j, "recent")) ParseRaRecent(SafeDump(*r), this->ra.recent);
        this->ra.fetched = 0;   // beim nächsten Tick trotzdem aktualisieren
        long long t = 0, th = 0;
        for(const auto &g: this->ra.games) { t += g.got; th += g.gotHard; }
        this->ra.trophies = t;
        this->ra.trophiesHard = th;
    }

    void Online::RaSaveCache() {
        json j = json::object();
        j["user"] = this->d->settings.S("ra.user");
        j["profile"] = { { "User", this->ra.profile.user }, { "TotalPoints", this->ra.profile.hardcore }, { "TotalSoftcorePoints", this->ra.profile.softcore }, { "TotalTruePoints", this->ra.profile.retro } };
        json games = json::array();
        for(const auto &g: this->ra.games) games.push_back({ { "GameID", g.id }, { "Title", g.title }, { "ConsoleName", g.console }, { "MaxPossible", g.max }, { "NumAwarded", g.got }, { "NumAwardedHardcore", g.gotHard }, { "MostRecentAwardedDate", g.lastDate } });
        j["games"] = games;
        json rec = json::array();
        for(size_t i = 0; i < this->ra.recent.size() && i < 10; i++) {
            const auto &r = this->ra.recent[i];
            rec.push_back({ { "Title", r.title }, { "GameTitle", r.game }, { "Date", r.date }, { "GameID", r.gameId }, { "Points", r.points }, { "HardcoreMode", r.hardcore ? 1 : 0 } });
        }
        j["recent"] = rec;
        WriteFileAtomic(RaCacheFile, SafeDump(j));
    }

}
