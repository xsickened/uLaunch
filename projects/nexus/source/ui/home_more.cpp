// NEXUS HOME: Suche, Mehrfachauswahl, Schnellstart, Homebrew, Aufkleber, Spielzeit, Spielfarbe,
// automatischer Themewechsel, Darstellung pro Benutzer, Sounds
#include "home.hpp"
#include "../core/themepack.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace nexus {

    namespace {
        constexpr char UsersDir[] = "sdmc:/nexus/users";

        Col StickerCol(const std::string &id) {
            if(id == "new") return ParseCol("#3ee0c8");
            if(id == "progress") return ParseCol("#ffb347");
            if(id == "done") return ParseCol("#5fd35f");
            if(id == "100") return ParseCol("#ffd447");
            if(id == "mp") return ParseCol("#00a2ff");
            if(id == "coop") return ParseCol("#e86cff");
            return ParseCol("#ff5f6d");
        }
    }

    void Home::Sfx(const char *ev) {
        this->snd.Play(ev);
    }

    // ------------------------------------------------------------------ Suche & Mehrfachauswahl

    void Home::OpenSearch() {
        this->AskText("Suche (Spiele, Homebrew, Ordner)", this->searchQuery, 40, [this](std::string q) {
            this->searchQuery = q;
            this->openFolder.clear();
            this->focus = 0;
            this->scroll = 0;
            this->entryAnim = true;
            this->listDirty = true;
            if(!q.empty()) this->Sfx("open");
        });
    }

    void Home::OpenMultiMenu() {
        auto dl = std::make_shared<Dialog>();
        dl->title = std::to_string(this->multiSel.size()) + " ausgewählt";
        dl->w = 420; dl->x = this->DialogX(420); dl->y = 70; dl->shade = true;
        dl->rows = [this]() {
            const std::vector<std::string> ids(this->multiSel.begin(), this->multiSel.end());
            auto finish = [this]() { this->multi = false; this->multiSel.clear(); this->listDirty = true; this->MarkDirty(); };
            std::vector<Row> rows;
            rows.push_back(Row{ "In Ordner verschieben …", nullptr, nullptr, [this, ids, finish]() { this->Pop(); this->OpenFolderPicker(ids); finish(); } });
            rows.push_back(Row{ "Neuen Ordner daraus erstellen …", nullptr, nullptr, [this, ids, finish]() { this->Pop(); this->CreateFolderFlow(ids); finish(); } });
            rows.push_back(Row{ "Zu Favoriten hinzufügen", nullptr, nullptr, [this, ids, finish]() {
                this->Pop(); for(const auto &i: ids) if(!this->d->library.IsFav(i)) this->d->library.ToggleFav(i); finish(); this->Toast("Zu Favoriten hinzugefügt.");
            } });
            rows.push_back(Row{ "Aus Favoriten entfernen", nullptr, nullptr, [this, ids, finish]() {
                this->Pop(); for(const auto &i: ids) if(this->d->library.IsFav(i)) this->d->library.ToggleFav(i); finish();
            } });
            rows.push_back(Row{ "Aus Ordnern entfernen", nullptr, nullptr, [this, ids, finish]() { this->Pop(); for(const auto &i: ids) this->d->library.RemoveFromFolder(i); finish(); } });
            rows.push_back(Row{ "Aufkleber setzen", nullptr, nullptr, nullptr, true });
            for(const char *st: StickerIds) {
                const std::string sid = st;
                rows.push_back(Row{ std::string("„") + StickerLabel(sid) + "“ setzen", nullptr, nullptr, [this, ids, sid]() {
                    for(const auto &i: ids) { const auto have = this->d->library.Stickers(i); if(std::find(have.begin(), have.end(), sid) == have.end()) this->d->library.ToggleSticker(i, sid); }
                    this->MarkDirty(); this->Toast("Aufkleber gesetzt.");
                } });
            }
            rows.push_back(Row{ "Auswahl", nullptr, nullptr, nullptr, true });
            rows.push_back(Row{ "Alle in diesem Bereich auswählen", nullptr, nullptr, [this]() { for(const auto &e: this->list) if(!e.folder) this->multiSel.insert(e.id); this->Pop(); } });
            rows.push_back(Row{ "Auswahl beenden", nullptr, nullptr, [this, finish]() { this->Pop(); finish(); } });
            return rows;
        };
        this->Push(dl);
    }

    // ------------------------------------------------------------------ Starten

    void Home::QuickLaunch() {
        std::string best;
        double t = 0;
        for(const auto &id: this->installed) { const double l = this->d->library.Launched(id); if(l > t) { t = l; best = id; } }
        auto it = this->appMap.find(best);
        if(best.empty() || it == this->appMap.end()) { this->Toast("Noch kein Spiel gestartet."); this->Sfx("error"); return; }
        if(!this->host) { this->Toast("Schnellstart geht nur, wenn NEXUS das HOME-Menü ist."); return; }
        const uint64_t tid = it->second.tid;
        const std::string name = this->DisplayName(best);
        if(this->host->SuspendedTid() == tid) { this->host->Resume(); return; }
        if(this->host->HasSuspended()) {
            this->Confirm("Laufendes Spiel schließen?", "Für den Schnellstart wird das geöffnete Spiel beendet.", "Schließen und " + name + " starten", [this, tid, best, name]() { this->host->CloseSuspended(); this->DoLaunch(tid, best, name); });
            return;
        }
        this->Toast("Schnellstart: " + name);
        this->DoLaunch(tid, best, name);
    }

    void Home::LaunchHb(const HbInfo &h) {
        if(!this->host) { this->Toast("Homebrew starten geht nur, wenn NEXUS das HOME-Menü ist."); return; }
        bool asApp = this->d->settings.S("library.hbMode") == "app";
        if(asApp && !this->host->CanLaunchHomebrewAsApp()) {
            asApp = false;
            this->Toast("Anwendungsmodus ist in uLaunch nicht eingerichtet – starte als Applet.");
        }
        auto go = [this, h, asApp]() {
            this->d->library.MarkLaunched(h.id, static_cast<double>(std::time(nullptr)));
            this->Save();
            Log("Homebrew starten: %s (%s)", h.path.c_str(), asApp ? "Anwendung" : "Applet");
            const uint32_t rc = this->host->LaunchHomebrew(h.path, asApp);
            if(rc != 0) { char b[96]; std::snprintf(b, sizeof(b), "Start fehlgeschlagen (0x%X).", rc); this->Toast(b); this->Sfx("error"); }
        };
        if(asApp && this->host->HasSuspended()) {
            this->Confirm("Laufendes Spiel schließen?", "Homebrew als Anwendung braucht den Platz des geöffneten Spiels.", "Schließen und starten", [this, go, h]() { this->host->CloseSuspended(); this->StartLaunchFx(h.id, h.name, go); });
            return;
        }
        this->StartLaunchFx(h.id, h.name, go);
    }

    // ------------------------------------------------------------------ Aufkleber

    void Home::OpenStickers(const std::string &id) {
        auto dl = std::make_shared<Dialog>();
        dl->title = "Aufkleber";
        dl->sub = "Erscheinen auf der Kachel (Position unter Bibliothek einstellbar).";
        dl->w = 420; dl->x = this->DialogX(420); dl->y = 90; dl->shade = true;
        dl->rows = [this, id]() {
            std::vector<Row> rows;
            const auto have = this->d->library.Stickers(id);
            for(const char *st: StickerIds) {
                const std::string sid = st;
                const bool on = std::find(have.begin(), have.end(), sid) != have.end();
                rows.push_back(Row{ StickerLabel(sid), [on]() { return on ? std::string("an") : std::string("aus"); }, nullptr, [this, id, sid]() { this->d->library.ToggleSticker(id, sid); this->MarkDirty(); this->Sfx("toggle"); } });
            }
            return rows;
        };
        this->Push(dl);
    }

    void Home::DrawStickers(const Entry &e, const Xf &xf, double w, double h, double op) {
        if(!this->d->settings.B("library.stickers") || w < 70) return;
        const auto list = this->d->library.Stickers(e.id);
        if(list.empty()) return;
        const auto pos = this->d->settings.S("library.stickerPos");
        const bool right = pos != "bottomLeft", top = pos == "topRight";
        double x = right ? xf.cx + w / 2 - 8 : xf.cx - w / 2 + 8;
        const bool titled = JBool(this->Tile(e.id), "showTitle", false);
        const double y = top ? xf.cy - h / 2 + (this->d->library.IsFav(e.id) ? 30 : 8) : xf.cy + h / 2 - 28 - (titled ? 30 : 0);
        int n = 0;
        for(const auto &s: list) {
            if(++n > 3) break;
            const std::string t = StickerLabel(s);
            const int tw = this->g->TextW(t, 11, true) + 14;
            const double bx = right ? x - tw : x;
            this->g->RoundRect(bx, y - (n - 1) * (top ? -22 : 22), tw, 19, 9.5, WithA(StickerCol(s), 0.95 * op));
            this->g->Text(t, 11, bx + 7, y - (n - 1) * (top ? -22 : 22) + 2, Col{ 0, 0, 0, static_cast<uint8_t>(255 * op) }, 0, 0, true);
        }
    }

    // ------------------------------------------------------------------ Spielzeit

    const Home::PlayCache &Home::Play(const std::string &id) {
        auto it = this->play.find(id);
        if(it != this->play.end()) return it->second;
        PlayCache pc;
        auto app = this->appMap.find(id);
        if(app != this->appMap.end()) pc.ok = plat::PlayStats(app->second.tid, this->currentUser.id, pc.info);
        return this->play[id] = pc;
    }

    double Home::DrawPlaytime(const Entry &e, double x, double y, double w, double alpha) {
        if(!this->d->settings.B("logo.showPlaytime") || e.folder || !this->appMap.count(e.id)) return 0;
        const auto &p = this->Play(e.id);
        if(!p.ok || (p.info.minutes == 0 && p.info.launches == 0)) return 0;
        std::string t;
        if(p.info.minutes < 60) t = std::to_string(p.info.minutes) + " Min. gespielt";
        else t = std::to_string(p.info.minutes / 60) + " Std. " + std::to_string(p.info.minutes % 60) + " Min. gespielt";
        if(p.info.launches > 0) t += "  ·  " + std::to_string(p.info.launches) + "× gestartet";
        this->g->Icon("recent", x + 9, y + 10, 16, WithA(Col{ 255, 255, 255, 255 }, alpha), false);
        this->g->Text(t, 15, x + 24, y + 1, WithA(Col{ 255, 255, 255, 230 }, alpha), 0, static_cast<int>(std::max(240.0, w)));
        return 26;
    }

    // ------------------------------------------------------------------ Akzent aus dem Spielbild

    void Home::UpdateGameColor(double dt) {
        if(this->d->settings.S("theme.accentSource") != "game" || this->list.empty() || this->focus >= static_cast<int>(this->list.size())) return;
        const auto &e = this->list[this->focus];
        const auto &a = this->Art(e.id);
        uint8_t r = 0, g = 0, b = 0;
        bool have = false;
        for(const auto &k: { a.hero, a.grid, e.iconKey }) {
            if(k.empty()) continue;
            if(this->img->ColorOf(k, r, g, b)) { have = true; break; }
            this->img->Get(k, 4);   // anstoßen, damit die Farbe bestimmt wird
        }
        if(!have && e.folder) { const Col c = ParseCol(e.accent); r = c.r; g = c.g; b = c.b; have = true; }
        if(!have) return;
        if(this->gameR < 0) { this->gameR = r; this->gameG = g; this->gameB = b; return; }
        const double k = std::min(1.0, dt * 4);
        this->gameR += (r - this->gameR) * k;
        this->gameG += (g - this->gameG) * k;
        this->gameB += (b - this->gameB) * k;
    }

    // ------------------------------------------------------------------ Tag-/Nacht-Theme

    void Home::UpdateAutoTheme(double dt) {
        this->autoCheck -= dt;
        if(this->autoCheck > 0) return;
        this->autoCheck = 30;
        auto &s = this->d->settings;
        if(!s.B("auto.enabled")) return;
        const std::time_t now = std::time(nullptr);
        const int hr = std::localtime(&now)->tm_hour;
        const int ds = static_cast<int>(s.N("auto.dayStart")), ns = static_cast<int>(s.N("auto.nightStart"));
        const bool day = ds <= ns ? (hr >= ds && hr < ns) : (hr >= ds || hr < ns);
        const std::string phase = day ? "day" : "night";
        if(JStr(*JGet(s.data, "auto"), "lastPhase", "") == phase) return;
        const std::string path = s.S(day ? "auto.dayTheme" : "auto.nightTheme");
        JSet(s.data, "auto.lastPhase", json(phase));
        this->MarkDirty();
        if(path.empty() || !FileExists(path)) return;
        json result;
        std::string err;
        const bool pack = path.size() > 8 && path.compare(path.size() - 8, 8, ".nxtheme") == 0;
        const bool ok = pack ? ApplyThemePack(path, ArtDir, s.data, result, err) : ApplyUserTheme(path, s.data, result);
        if(!ok) return;
        s.data = result;
        JSet(s.data, "auto.lastPhase", json(phase));
        this->listDirty = true; this->layoutDirty = true;
        this->Toast(day ? "Tag-Theme aktiv." : "Nacht-Theme aktiv.");
    }

    // ------------------------------------------------------------------ Darstellung pro Benutzer

    void Home::SwitchUserLook(const std::string &newUser) {
        auto &s = this->d->settings;
        if(newUser.empty()) return;
        if(!s.B("profile.perUser")) { this->lookUser = newUser; return; }
        std::string from = this->lookUser;
        if(from.empty()) {
            const json *pr = JGet(s.data, "profile");
            from = pr ? JStr(*pr, "lastUser", "") : std::string();
        }
        if(from == newUser) { this->lookUser = newUser; return; }
        EnsureDir(UsersDir);
        auto favs = [this]() {
            json f = json::array();
            for(auto it = this->d->library.apps.begin(); it != this->d->library.apps.end(); ++it) if(JBool(it.value(), "favorite", false)) f.push_back(it.key());
            return f;
        };
        if(!from.empty()) {
            json j = json::object();
            j["settings"] = ThemeSnapshot(s.data);
            j["favorites"] = favs();
            WriteFileAtomic(std::string(UsersDir) + "/" + from + ".json", SafeDump(j, 1));
        }
        std::string raw;
        if(ReadFile(std::string(UsersDir) + "/" + newUser + ".json", raw)) {
            const json j = json::parse(raw, nullptr, false);
            const json *ts = j.is_object() ? JGet(j, "settings") : nullptr;
            if(ts && ts->is_object()) {
                json merged = ThemeSnapshot(*ts);
                for(const char *k: { "ra", "sgdbKey", "sgdb", "library", "auto" }) { const json *v = JGet(s.data, k); if(v) merged[k] = *v; }
                JSet(merged, "profile.perUser", json(true));
                s.data = Settings::Sanitize(merged, nullptr);
                const json *f = JGet(j, "favorites");
                for(auto it = this->d->library.apps.begin(); it != this->d->library.apps.end(); ++it) if(it.value().is_object()) it.value()["favorite"] = false;
                if(f && f->is_array()) for(const auto &id: *f) if(id.is_string() && this->d->library.apps.contains(id.get<std::string>())) this->d->library.apps[id.get<std::string>()]["favorite"] = true;
                this->listDirty = true; this->layoutDirty = true;
                Log("Darstellung des Benutzers geladen");
            }
        }
        this->lookUser = newUser;
        JSet(s.data, "profile.lastUser", json(newUser));
        this->MarkDirty();
    }

}
