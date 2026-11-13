#include "home.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>

namespace nexus {

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        const char *SectionName(const std::string &id) {
            if(id == "start") return "Startseite";
            if(id == "all") return "Alle Spiele";
            if(id == "fav") return "Favoriten";
            if(id == "folders") return "Ordner";
            if(id == "recent") return "Zuletzt gespielt";
            if(id == "homebrew") return "Homebrew";
            if(id == "settings") return "Einstellungen";
            return "?";
        }
        const std::vector<std::pair<std::string, std::string>> Groups = {
            { "look", "Darstellung & Schrift" }, { "top", "Kopfzeile, Symbole & Hinweise" }, { "layout", "Anordnung der Kacheln" },
            { "anim", "Animationen" }, { "tiles", "Kacheln (Standard)" }, { "hero", "Fokus: Hintergrund" }, { "logo", "Fokus: Logo" },
            { "home", "Home-Hintergrund" }, { "lock", "Sperrbildschirm" }, { "sections", "Bereiche (Reihenfolge & Sichtbarkeit)" },
            { "lib", "Bibliothek" }, { "sys", "Themes & System" }
        };
        const char *const Presets[8][3] = {
            { "#0b0f17", "#1d2b4a", "#0b0f17" }, { "#1a0b2e", "#4a1d6b", "#0f0a1f" }, { "#0d3b4c", "#1b6e6b", "#0a1f2b" }, { "#2b0f0f", "#6b2a1d", "#1a0a0a" },
            { "#0f2b14", "#2a6b3b", "#0a1a0e" }, { "#2b2b0f", "#6b5a1d", "#14140a" }, { "#e9edf2", "#c9d6ea", "#f6f8fb" }, { "#101010", "#3a3a3a", "#000000" }
        };
        double EaseOut(double t) { t = std::clamp(t, 0.0, 1.0); return 1 - (1 - t) * (1 - t) * (1 - t); }
        std::string Lower(std::string s) { for(auto &c: s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
    }

    // ------------------------------------------------------------------ Grundlagen

    bool Home::Init(SDL_Renderer *rr, Gfx *gg, ImageCache *im, Apps *ap, NexusData *dd, bool safeMode, bool isApplet) {
        this->r = rr; this->g = gg; this->img = im; this->apps = ap; this->d = dd; this->applet = isApplet;
        this->locked = this->d->settings.B("lock.enabled") && !safeMode;
        if(safeMode) this->Toast("Abgesicherter Modus: Darstellung auf Standard gesetzt. Bibliothek ist erhalten.");
        else if(isApplet) this->Toast("Applet-Modus: wenig Speicher. Besser ein Spiel mit gedrückter R-Taste starten.");
        return true;
    }

    void Home::Save() {
        if(!SaveData(DataFile, *this->d)) this->Toast("Speichern fehlgeschlagen (SD-Karte?)");
        this->dirtySave = false;
    }

    void Home::MarkDirty() {
        this->dirtySave = true;
        this->saveTimer = 1.0;   // gebündelt speichern, schont die SD-Karte
    }

    void Home::Toast(const std::string &t) {
        this->toastText = t;
        this->toastT = 3.2;
        Log("Hinweis: %s", t.c_str());
    }

    LayoutEnv Home::Env() const {
        LayoutEnv e;
        e.topH = this->d->settings.B("topbar.visible") ? 60 : 14;
        e.botH = this->d->settings.B("hints.visible") ? 46 : 14;
        e.crumb = !this->openFolder.empty();
        return e;
    }

    bool Home::Light() const {
        const auto tc = this->d->settings.S("theme.textColor");
        if(tc == "black") return true;
        if(tc == "white") return false;
        const Col u = this->UiCol();
        return (0.299 * u.r + 0.587 * u.g + 0.114 * u.b) / 255.0 > 0.55;
    }
    Col Home::TextCol() const { return this->Light() ? Col{ 18, 22, 29, 255 } : Col{ 238, 242, 247, 255 }; }
    Col Home::MutedCol() const { return WithA(this->TextCol(), 0.62); }
    Col Home::Accent() const { return ParseCol(this->d->settings.S("theme.accent")); }
    Col Home::UiCol() const { return ParseCol(this->d->settings.S("theme.ui")); }

    // ------------------------------------------------------------------ Daten

    void Home::SyncApps() {
        if(this->apps->Version() == this->appsVersion) return;
        int v = 0;
        const auto snap = this->apps->Snapshot(v);
        this->appsVersion = v;
        this->appMap.clear();
        this->installed.clear();
        for(const auto &a: snap) {
            this->appMap[a.id] = a;
            this->installed.push_back(a.id);
        }
        this->d->library.Reconcile(this->installed, nullptr);
        this->listDirty = true;
    }

    const ArtPaths &Home::Art(const std::string &id) {
        auto it = this->art.find(id);
        if(it != this->art.end()) return it->second;
        ArtPaths p;
        const std::string base = std::string(ArtDir) + "/" + id + "/";
        for(const char *n: { "grid.jpg", "grid.png" }) if(p.grid.empty() && FileExists(base + n)) p.grid = base + n;
        for(const char *n: { "hero.jpg", "hero.png" }) if(p.hero.empty() && FileExists(base + n)) p.hero = base + n;
        for(const char *n: { "logo.png", "logo.jpg" }) if(p.logo.empty() && FileExists(base + n)) p.logo = base + n;
        return this->art[id] = p;
    }

    json Home::TileOverride(const std::string &id) {
        if(IsFolderId(id)) {
            auto *f = this->d->library.FindFolder(id);
            return f ? f->tile : json::object();
        }
        const json *t = JGet(this->d->library.apps, id + ".tile");
        return t ? *t : json::object();
    }

    json Home::Tile(const std::string &id) {
        return EffectiveTile(this->d->settings, this->TileOverride(id));
    }

    std::vector<std::string> *Home::ManualOrder() {
        if(!this->openFolder.empty()) {
            auto *f = this->d->library.FindFolder(this->openFolder);
            return (f && f->sort == "manual") ? &f->items : nullptr;
        }
        return this->section == "start" ? &this->d->library.rootOrder : nullptr;
    }

    void Home::BuildList() {
        auto &lib = this->d->library;
        std::vector<Entry> out;
        auto appEntry = [&](const std::string &id) -> bool {
            auto it = this->appMap.find(id);
            if(it == this->appMap.end()) return false;
            Entry e;
            e.id = id;
            e.name = it->second.name.empty() ? id : it->second.name;
            e.iconKey = it->second.iconKey;
            out.push_back(e);
            return true;
        };
        auto folderEntry = [&](const Folder &f) {
            Entry e;
            e.id = f.id; e.folder = true; e.name = f.name; e.accent = f.accent;
            for(const auto &i: f.items) {
                auto it = this->appMap.find(i);
                if(it == this->appMap.end()) continue;
                e.folderCount++;
                if(e.folderIcons.size() < 4 && !it->second.iconKey.empty()) e.folderIcons.push_back(it->second.iconKey);
            }
            out.push_back(e);
        };
        auto byName = [&](std::vector<std::string> ids) {
            std::sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) { return Lower(this->appMap[a].name) < Lower(this->appMap[b].name); });
            return ids;
        };
        if(!this->openFolder.empty()) {
            auto *f = lib.FindFolder(this->openFolder);
            if(!f) this->openFolder.clear();
            else {
                std::vector<std::string> ids = f->items;
                if(f->sort == "name") ids = byName(ids);
                for(const auto &i: ids) appEntry(i);
            }
        }
        if(this->openFolder.empty()) {
            if(this->section == "start" || this->section == "settings") {
                for(const auto &id: lib.rootOrder) {
                    if(IsFolderId(id)) { if(auto *f = lib.FindFolder(id)) folderEntry(*f); }
                    else appEntry(id);
                }
            }
            else if(this->section == "all") {
                std::vector<std::string> ids = this->installed;
                if(this->d->settings.S("library.sort") == "recent")
                    std::stable_sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) { return lib.Launched(a) > lib.Launched(b); });
                else ids = byName(ids);
                for(const auto &i: ids) appEntry(i);
            }
            else if(this->section == "fav") {
                for(const auto &i: byName(this->installed)) if(lib.IsFav(i)) appEntry(i);
            }
            else if(this->section == "folders") {
                for(const auto &f: lib.folders) folderEntry(f);
            }
            else if(this->section == "recent") {
                std::vector<std::string> ids;
                for(const auto &i: this->installed) if(lib.Launched(i) > 0) ids.push_back(i);
                std::sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) { return lib.Launched(a) > lib.Launched(b); });
                if(ids.size() > 12) ids.resize(12);
                for(const auto &i: ids) appEntry(i);
            }
        }
        // Liste geändert? (für Einblend-Animation)
        bool same = out.size() == this->list.size();
        for(size_t i = 0; same && i < out.size(); i++) same = out[i].id == this->list[i].id;
        this->list = out;
        this->litems.clear();
        for(const auto &e: this->list) {
            const json t = this->Tile(e.id);
            LItem li;
            li.size = JStr(t, "size", "M");
            li.ratio = JStr(t, "ratio", "2:3");
            li.customH = JNum(t, "customH", 7);
            li.customRatio = JNum(t, "customRatio", 1);
            this->litems.push_back(li);
        }
        this->focus = std::clamp(this->focus, 0, std::max(0, static_cast<int>(this->list.size()) - 1));
        if(!same && this->entryAnim) { /* bleibt gesetzt bis Relayout */ }
        else if(!same) { /* Liste geändert ohne Bereichswechsel: neue Kacheln blenden ein */ }
        this->listDirty = false;
        this->layoutDirty = true;
    }

    void Home::Relayout() {
        this->lay = ComputeLayout(this->d->settings, this->Env(), this->litems, this->focus, this->scroll);
        const auto &s = this->d->settings;
        const bool animOn = s.B("anim.enabled") && !s.B("anim.reduce");
        const auto entry = s.S("anim.entry");
        for(size_t i = 0; i < this->list.size() && i < this->lay.items.size(); i++) {
            auto &ds = this->disp[this->list[i].id];
            const auto &it = this->lay.items[i];
            if(!ds.init || (this->entryAnim && animOn && entry != "none")) {
                ds = Disp{};
                ds.x = it.x; ds.y = it.y; ds.w = it.w; ds.h = it.h; ds.rot = it.rot; ds.sc = it.sc;
                ds.squash = std::max(0.12, std::cos(it.ry * kPi / 180.0));
                ds.op = it.op;
                ds.init = true;
                if(this->entryAnim && animOn && entry != "none") {
                    ds.op = 0;
                    if(entry == "slideUp") ds.y += 60;
                    else if(entry == "slideSide") ds.x += 120;
                    else if(entry == "pop") ds.sc *= 0.6;
                    else if(entry == "flip") ds.squash = 0.05;
                    ds.delay = std::min<size_t>(i, 24) * s.N("anim.stagger") / 1000.0;
                }
            }
        }
        this->entryAnim = false;
        this->layoutDirty = false;
    }

    void Home::SetFocus(int i) {
        if(this->list.empty()) return;
        i = std::clamp(i, 0, static_cast<int>(this->list.size()) - 1);
        if(i == this->focus) return;
        this->focus = i;
        this->layoutDirty = true;
        this->wobbleT = 0;
    }

    void Home::SwitchSection(const std::string &s) {
        if(s == "settings") {
            this->CloseAll();
            this->OpenSettings();
            return;
        }
        this->CloseAll();
        this->section = s;
        this->openFolder.clear();
        this->focus = 0;
        this->scroll = 0;
        this->moving = false;
        this->entryAnim = true;
        this->listDirty = true;
    }

    void Home::CycleSection(int dir) {
        const auto vis = this->d->settings.VisibleSections();
        auto it = std::find(vis.begin(), vis.end(), this->section);
        int i = it == vis.end() ? 0 : static_cast<int>(it - vis.begin());
        const int n = static_cast<int>(vis.size());
        this->SwitchSection(vis[((i + dir) % n + n) % n]);
    }

    // ------------------------------------------------------------------ Eingabe

    bool Home::Frame(const plat::Input &in, double dt) {
        bool wantExit = false;
        try {
            if((this->fatal || (!this->locked && this->dialogs.empty())) && (in.down & plat::BtnPlus)) wantExit = true;
            this->HandleInput(in);
            this->SyncApps();
            if(this->listDirty) this->BuildList();
            if(this->layoutDirty) this->Relayout();
            this->Update(dt);
            this->Draw();
        }
        catch(const std::exception &e) {
            this->CrashReset(e.what());
        }
        catch(...) {
            this->CrashReset("unbekannter Fehler");
        }
        if(wantExit && this->dirtySave) this->Save();
        return !wantExit;
    }

    void Home::CrashReset(const std::string &what) {
        Log("FEHLER: %s", what.c_str());
        this->crashCount++;
        this->dialogs.clear();
        if(this->crashCount == 1) {
            // Stufe 1: sichere Anordnung, Bibliothek bleibt unangetastet
            const json def = Settings::Defaults();
            JSet(this->d->settings.data, "layout", *JGet(def, "layout"));
            this->d->settings.Set("anim.reduce", true);
            this->listDirty = true;
            this->Toast("Darstellungsfehler – auf sichere Anordnung „Raster“ zurückgesetzt.");
            this->MarkDirty();
        }
        else {
            this->fatal = true;
            this->fatalMsg = what;
        }
    }

    void Home::HandleInput(const plat::Input &in) {
        if(in.tap) this->Tap(in.tapX, in.tapY);
        const uint32_t order[] = { plat::BtnUp, plat::BtnDown, plat::BtnLeft, plat::BtnRight, plat::BtnA, plat::BtnB, plat::BtnX, plat::BtnY, plat::BtnL, plat::BtnR, plat::BtnZL, plat::BtnZR, plat::BtnMinus };
        for(const uint32_t b: order) if(in.down & b) this->Press(b);
    }

    void Home::Press(uint32_t b) {
        using namespace plat;
        if(this->fatal) {
            if(b == BtnA) {
                this->d->settings.data = Settings::Defaults();
                this->fatal = false;
                this->crashCount = 0;
                this->listDirty = true;
                this->Save();
            }
            return;
        }
        if(this->locked) {
            if(!this->unlocking && (b & (BtnA | BtnB | BtnX | BtnY))) { this->unlocking = true; this->unlockT = 0; }
            return;
        }
        if(b == BtnMinus) { this->CloseAll(); this->locked = true; this->unlocking = false; return; }
        if(!this->dialogs.empty()) {
            auto dl = this->dialogs.back();
            auto rows = dl->rows();
            auto valid = [&](int i) { return i >= 0 && i < static_cast<int>(rows.size()) && !rows[i].head; };
            if(!valid(dl->sel)) { for(int i = 0; i < static_cast<int>(rows.size()); i++) if(valid(i)) { dl->sel = i; break; } }
            if(b == BtnUp || b == BtnDown) {
                const int n = static_cast<int>(rows.size());
                for(int k = 1; k <= n; k++) {
                    const int i = ((dl->sel + (b == BtnUp ? -k : k)) % n + n) % n;
                    if(valid(i)) { dl->sel = i; break; }
                }
            }
            else if((b == BtnLeft || b == BtnRight) && valid(dl->sel) && rows[dl->sel].adjust) rows[dl->sel].adjust(b == BtnLeft ? -1 : 1);
            else if(b == BtnA && valid(dl->sel)) {
                if(rows[dl->sel].activate) rows[dl->sel].activate();
                else if(rows[dl->sel].adjust) rows[dl->sel].adjust(1);
            }
            else if(b == BtnB) { if(dl->onB) dl->onB(); else this->Pop(); }
            else if(b == BtnX && dl->onX) dl->onX();
            else if(b == BtnY && dl->onY) dl->onY();
            else if((b == BtnL || b == BtnR || b == BtnZL || b == BtnZR) && dl->settings) this->CycleSection((b == BtnL || b == BtnZL) ? -1 : 1);
            return;
        }
        if(this->moving) {
            auto *order = this->ManualOrder();
            if(!order) { this->moving = false; return; }
            if(b == BtnLeft || b == BtnUp || b == BtnRight || b == BtnDown) {
                auto it = std::find(order->begin(), order->end(), this->moveId);
                if(it == order->end()) return;
                const int i = static_cast<int>(it - order->begin());
                const int j = std::clamp(i + ((b == BtnLeft || b == BtnUp) ? -1 : 1), 0, static_cast<int>(order->size()) - 1);
                if(i != j) {
                    std::swap((*order)[i], (*order)[j]);
                    this->listDirty = true;
                    this->BuildList();
                    for(size_t k = 0; k < this->list.size(); k++) if(this->list[k].id == this->moveId) this->focus = static_cast<int>(k);
                    this->layoutDirty = true;
                }
            }
            else if(b == BtnA) { this->moving = false; this->MarkDirty(); this->Toast("Neue Position gespeichert."); }
            else if(b == BtnB) { *order = this->moveBackup; this->moving = false; this->listDirty = true; this->Toast("Änderung verworfen."); }
            return;
        }
        switch(b) {
            case BtnLeft: this->SetFocus(Navigate(this->lay, this->d->settings, this->focus, -1, 0)); break;
            case BtnRight: this->SetFocus(Navigate(this->lay, this->d->settings, this->focus, 1, 0)); break;
            case BtnUp: this->SetFocus(Navigate(this->lay, this->d->settings, this->focus, 0, -1)); break;
            case BtnDown: this->SetFocus(Navigate(this->lay, this->d->settings, this->focus, 0, 1)); break;
            case BtnL: case BtnZL: this->CycleSection(-1); break;
            case BtnR: case BtnZR: this->CycleSection(1); break;
            case BtnA: this->Activate(); break;
            case BtnB:
                if(!this->openFolder.empty()) {
                    const auto fid = this->openFolder;
                    this->openFolder.clear();
                    this->scroll = 0;
                    this->entryAnim = true;
                    this->BuildList();
                    for(size_t k = 0; k < this->list.size(); k++) if(this->list[k].id == fid) this->focus = static_cast<int>(k);
                    this->layoutDirty = true;
                }
                break;
            case BtnX: if(!this->list.empty()) this->OpenContext(); break;
            case BtnY:
                if(!this->list.empty() && !this->list[this->focus].folder) {
                    this->d->library.ToggleFav(this->list[this->focus].id);
                    this->Toast(this->d->library.IsFav(this->list[this->focus].id) ? "Zu Favoriten hinzugefügt" : "Aus Favoriten entfernt");
                    this->MarkDirty();
                    if(this->section == "fav") this->listDirty = true;
                }
                break;
            default: break;
        }
    }

    void Home::Activate() {
        if(this->list.empty()) return;
        const auto &e = this->list[this->focus];
        if(e.folder) {
            this->openFolder = e.id;
            this->focus = 0;
            this->scroll = 0;
            this->entryAnim = true;
            this->listDirty = true;
            return;
        }
        auto it = this->appMap.find(e.id);
        if(it == this->appMap.end()) return;
        this->d->library.MarkLaunched(e.id, static_cast<double>(std::time(nullptr)));
        this->Save();
        const uint32_t rc = plat::LaunchApplication(it->second.tid);
        if(rc == 0) this->Toast("Starte " + e.name + " …");
        else {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "Start aus der Test-App nicht möglich (0x%X). Starten klappt, sobald NEXUS das HOME-Menü ist.", rc);
            this->Toast(buf);
        }
    }

    void Home::Tap(float x, float y) {
        if(this->fatal) { this->Press(plat::BtnA); return; }
        if(this->locked) { this->Press(plat::BtnA); return; }
        if(!this->dialogs.empty()) {
            const auto &p = this->panelHit;
            if(x < p.x || x > p.x + p.w || y < p.y || y > p.y + p.h) { this->Press(plat::BtnB); return; }
            for(const auto &h: this->rowHits) {
                if(x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) {
                    auto dl = this->dialogs.back();
                    if(dl->sel == h.idx) this->Press(plat::BtnA);
                    else dl->sel = h.idx;
                    return;
                }
            }
            return;
        }
        for(const auto &h: this->tabHits) {
            if(x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) { this->SwitchSection(h.id); return; }
        }
        // Kacheln von oben nach unten prüfen (gedrehte Rechtecke)
        int best = -1, bestZ = -100000;
        for(size_t i = 0; i < this->list.size(); i++) {
            auto it = this->disp.find(this->list[i].id);
            if(it == this->disp.end() || it->second.op < 0.05) continue;
            const auto &ds = it->second;
            const double cx = ds.x + ds.w / 2, cy = ds.y + ds.h / 2;
            const double a = -ds.rot * kPi / 180.0;
            const double lx = ((x - cx) * std::cos(a) - (y - cy) * std::sin(a)) / std::max(0.01, ds.sc * ds.squash);
            const double ly = ((x - cx) * std::sin(a) + (y - cy) * std::cos(a)) / std::max(0.01, ds.sc);
            const int z = static_cast<int>(i) == this->focus ? 100000 : (i < this->lay.items.size() ? this->lay.items[i].z : 0);
            if(std::abs(lx) <= ds.w / 2 && std::abs(ly) <= ds.h / 2 && z > bestZ) { best = static_cast<int>(i); bestZ = z; }
        }
        if(best < 0) return;
        if(best == this->focus) this->Activate();
        else this->SetFocus(best);
    }

    // ------------------------------------------------------------------ Dialoge

    void Home::Push(std::shared_ptr<Dialog> dl) { this->dialogs.push_back(dl); }
    void Home::Pop() {
        if(this->dialogs.empty()) return;
        auto dl = this->dialogs.back();
        this->dialogs.pop_back();
        if(dl->onClose) dl->onClose();
    }
    void Home::CloseAll() { while(!this->dialogs.empty()) this->Pop(); }

    double Home::DialogX(double w) const {
        if(this->focus >= 0 && this->focus < static_cast<int>(this->lay.items.size())) {
            const auto &it = this->lay.items[this->focus];
            if(it.x + it.w / 2 > 700) return 20;
        }
        return ScreenW - w - 20;
    }

    void Home::Confirm(const std::string &title, const std::string &sub, const std::string &yes, std::function<void()> act) {
        auto dl = std::make_shared<Dialog>();
        dl->title = title; dl->sub = sub; dl->w = 520; dl->x = 380; dl->y = 220; dl->shade = true;
        dl->rows = [this, yes, act]() {
            return std::vector<Row>{
                Row{ "Abbrechen", nullptr, nullptr, [this]() { this->Pop(); } },
                Row{ yes, nullptr, nullptr, [this, act]() { this->Pop(); act(); } } };
        };
        this->Push(dl);
    }

    void Home::CreateFolderFlow(const std::vector<std::string> &ids) {
        std::string name;
        if(!plat::Keyboard("Name des neuen Ordners", "Neuer Ordner", 40, name)) {
            name = "Ordner " + std::to_string(this->d->library.folders.size() + 1);
            this->Toast("Tastatur hier nicht verfügbar – Ordner heißt „" + name + "“ (später umbenennbar).");
        }
        if(name.empty()) return;
        const int at = (this->section == "start" && this->openFolder.empty()) ? this->focus + 1 : -1;
        const auto fid = this->d->library.CreateFolder(name, at);
        for(const auto &id: ids) this->d->library.AddToFolder(id, fid);
        this->listDirty = true;
        this->MarkDirty();
    }

    void Home::OpenFolderPicker(const std::vector<std::string> &ids) {
        auto dl = std::make_shared<Dialog>();
        dl->title = "Zu Ordner hinzufügen"; dl->w = 420; dl->x = 430; dl->y = 140; dl->shade = true;
        dl->rows = [this, ids]() {
            std::vector<Row> rows;
            for(const auto &f: this->d->library.folders) {
                const auto fid = f.id, fname = f.name;
                rows.push_back(Row{ fname, nullptr, nullptr, [this, ids, fid, fname]() {
                    this->Pop();
                    for(const auto &id: ids) this->d->library.AddToFolder(id, fid);
                    this->listDirty = true; this->MarkDirty();
                    this->Toast("Verschoben nach „" + fname + "“");
                } });
            }
            rows.push_back(Row{ "+ Neuen Ordner erstellen …", nullptr, nullptr, [this, ids]() { this->Pop(); this->CreateFolderFlow(ids); } });
            return rows;
        };
        this->Push(dl);
    }

    void Home::OpenArtInfo(const std::string &id) {
        auto dl = std::make_shared<Dialog>();
        dl->title = "Eigene Bilder"; dl->w = 640; dl->x = 320; dl->y = 160; dl->shade = true;
        dl->sub = "Lege Bilder (z. B. von SteamGridDB) auf der SD-Karte ab. Die SteamGridDB-Suche direkt auf der Switch folgt in Phase 5.";
        const std::string base = std::string(ArtDir) + "/" + id + "/";
        dl->rows = [this, base, id]() {
            const auto &a = this->Art(id);
            return std::vector<Row>{
                Row{ "Cover (nur Kachel)", [base, a]() { return a.grid.empty() ? base + "grid.jpg" : std::string("vorhanden"); }, nullptr, nullptr },
                Row{ "Hintergrund (Ebene 1)", [base, a]() { return a.hero.empty() ? base + "hero.jpg" : std::string("vorhanden"); }, nullptr, nullptr },
                Row{ "Logo (Ebene 3, transparent)", [base, a]() { return a.logo.empty() ? base + "logo.png" : std::string("vorhanden"); }, nullptr, nullptr },
                Row{ "Bilder neu einlesen", nullptr, nullptr, [this, id]() {
                    const auto old = this->Art(id);
                    for(const auto &k: { old.grid, old.hero, old.logo }) if(!k.empty()) this->img->Invalidate(k);
                    this->art.erase(id);
                    this->heroA.key.clear(); this->heroFor.clear(); this->logoId.clear();
                    this->Toast("Bilder neu eingelesen.");
                } } };
        };
        this->Push(dl);
    }

    void Home::OpenContext() {
        if(this->list.empty()) return;
        const Entry e = this->list[this->focus];
        auto dl = std::make_shared<Dialog>();
        dl->title = e.name;
        dl->sub = e.folder ? "Ordner · " + std::to_string(e.folderCount) + " Einträge" : "Titel-ID " + e.id;
        dl->w = 360; dl->x = this->DialogX(360); dl->y = 70; dl->shade = true;
        dl->rows = [this, e]() {
            std::vector<Row> rows;
            rows.push_back(Row{ e.folder ? "Öffnen" : "Starten", nullptr, nullptr, [this]() { this->Pop(); this->Activate(); } });
            rows.push_back(Row{ "Kachel bearbeiten", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenEditor(e.id); } });
            rows.push_back(Row{ "Eigene Bilder (Cover, Hintergrund, Logo)", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenArtInfo(e.id); } });
            rows.push_back(Row{ "Position ändern", nullptr, nullptr, [this, e]() {
                this->Pop();
                auto *order = this->ManualOrder();
                if(!order) { this->Toast("Position nur auf der Startseite oder in Ordnern mit manueller Sortierung änderbar."); return; }
                this->moving = true; this->moveId = e.id; this->moveBackup = *order;
            } });
            rows.push_back(Row{ "Anordnung ändern …", nullptr, nullptr, [this]() { this->Pop(); this->OpenSettings("layout"); } });
            if(e.folder) {
                rows.push_back(Row{ "Ordner umbenennen", nullptr, nullptr, [this, e]() {
                    this->Pop();
                    std::string name;
                    if(!plat::Keyboard("Neuer Ordnername", e.name, 40, name)) { this->Toast("Tastatur hier nicht verfügbar (Applet-Modus)."); return; }
                    if(auto *f = this->d->library.FindFolder(e.id); f && !name.empty()) { f->name = name.substr(0, 60); this->listDirty = true; this->MarkDirty(); }
                } });
                rows.push_back(Row{ "Ordner löschen (Inhalt bleibt)", nullptr, nullptr, [this, e]() {
                    this->Pop();
                    this->Confirm("Ordner „" + e.name + "“ löschen?", "Die Spiele werden nicht deinstalliert. Sie wandern zurück auf die Startseite.", "Ordner löschen", [this, e]() {
                        this->d->library.DeleteFolder(e.id);
                        if(this->openFolder == e.id) this->openFolder.clear();
                        this->listDirty = true; this->MarkDirty();
                        this->Toast("Ordner gelöscht – Inhalt liegt auf der Startseite.");
                    });
                } });
            }
            else {
                rows.push_back(Row{ "Zu Ordner hinzufügen …", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenFolderPicker({ e.id }); } });
                if(!this->openFolder.empty()) rows.push_back(Row{ "Aus Ordner entfernen", nullptr, nullptr, [this, e]() {
                    this->Pop(); this->d->library.RemoveFromFolder(e.id); this->listDirty = true; this->MarkDirty();
                } });
                rows.push_back(Row{ this->d->library.IsFav(e.id) ? "Aus Favoriten entfernen" : "Zu Favoriten hinzufügen", nullptr, nullptr, [this, e]() {
                    this->Pop(); this->d->library.ToggleFav(e.id); this->MarkDirty(); if(this->section == "fav") this->listDirty = true;
                } });
            }
            rows.push_back(Row{ "Neuer Ordner …", nullptr, nullptr, [this]() { this->Pop(); this->CreateFolderFlow({}); } });
            rows.push_back(Row{ "Darstellung zurücksetzen", nullptr, nullptr, [this, e]() {
                this->Pop();
                this->d->library.TileOf(e.id) = json::object();
                this->listDirty = true; this->MarkDirty();
                this->Toast("Kachel zurückgesetzt.");
            } });
            return rows;
        };
        this->Push(dl);
    }

    void Home::OpenEditor(const std::string &id) {
        this->editId = id;
        this->editBackup = this->d->library.TileOf(id);
        auto dl = std::make_shared<Dialog>();
        dl->title = "Kachel bearbeiten";
        dl->sub = "Live-Vorschau. Y = Übernehmen · X = Zurücksetzen · B = Abbrechen";
        dl->w = 400; dl->x = this->DialogX(400); dl->y = this->Env().topH + 4;
        dl->h = ScreenH - dl->y - this->Env().botH - 10;
        dl->hints = { { "↑↓", "Option" }, { "←→", "Wert" }, { "Y", "Übernehmen" }, { "X", "Zurücksetzen" }, { "B", "Abbrechen" } };
        dl->rows = [this, id]() {
            std::vector<Row> rows;
            rows.push_back(Row{ "Kachel (Ebene 4)", nullptr, nullptr, nullptr, true });
            for(const auto &e: Schema()) {
                if(e.group != "tiles") continue;
                const auto key = e.path.substr(std::string("tileDefaults.").size());
                const auto *ep = &e;
                rows.push_back(Row{ e.label,
                    [this, id, key, ep]() { const json t = this->Tile(id); return Settings::DisplayValue(*ep, t.contains(key) ? t[key] : ep->def); },
                    [this, id, key, ep](int dir) {
                        const json t = this->Tile(id);
                        json &ov = this->d->library.TileOf(id);
                        ov[key] = Settings::StepValue(*ep, t.contains(key) ? t[key] : ep->def, dir);
                        this->listDirty = true;
                    }, nullptr });
            }
            return rows;
        };
        dl->onY = [this]() {
            this->d->library.TileOf(this->editId) = SanitizeTile(this->d->library.TileOf(this->editId));
            this->editBackup = this->d->library.TileOf(this->editId);
            this->MarkDirty();
            this->Pop();
            this->Toast("Gespeichert.");
        };
        dl->onX = [this]() { this->d->library.TileOf(this->editId) = json::object(); this->listDirty = true; };
        dl->onClose = [this]() { this->d->library.TileOf(this->editId) = this->editBackup; this->listDirty = true; };
        this->Push(dl);
    }

    bool Home::SettingVisible(const std::string &p) const {
        const auto &s = this->d->settings;
        const auto mode = s.S("layout.mode");
        auto in = [&](std::initializer_list<const char *> l) { for(const char *m: l) if(mode == m) return true; return false; };
        // In v0.1 der Switch-App noch nicht umgesetzt (im Prototyp vorhanden)
        static const char *const unsupported[] = { "hero.blur", "lock.blur", "theme.font", "lock.clockFont", "home.image", "lock.image", "hints.layerLabel" };
        for(const char *u: unsupported) if(p == u) return false;
        if(p == "layout.position" || p == "layout.height") return !IsVertical(s);
        if(p == "layout.side") return in({ "vgrid", "list" });
        if(p == "layout.rowCount") return in({ "rows" });
        if(p == "layout.columns") return in({ "vgrid" });
        if(p == "layout.align") return in({ "grid", "rows", "honeycomb" });
        if(p == "layout.focusMode") return in({ "grid", "rows", "honeycomb", "vgrid", "list" });
        if(p == "layout.wrap") return in({ "carousel", "arc", "coverflow", "list", "stack", "wave" });
        if(p == "layout.arcDir" || p == "layout.arcRadius" || p == "layout.arcSpread" || p == "layout.rotateTiles") return in({ "arc" });
        if(p == "layout.ringRadius") return in({ "ring" });
        if(p == "layout.tilt") return in({ "coverflow", "ring" });
        if(p == "layout.waveAmp" || p == "layout.waveFreq") return in({ "wave" });
        if(p == "layout.stackOffset") return in({ "stack" });
        if(p == "layout.sideScale") return in({ "carousel", "arc", "coverflow", "wave", "ring" });
        if(p == "layout.sideOpacity") return in({ "carousel", "arc", "coverflow", "wave", "ring", "stack" });
        if(p == "home.preset") { const auto t = s.S("home.bgType"); return t == "gradient" || t == "animated" || t == "image"; }
        if(p == "home.color") return s.S("home.bgType") == "color";
        if(p == "lock.preset") { const auto t = s.S("lock.bgType"); return t == "gradient" || t == "image"; }
        if(p == "lock.color") return s.S("lock.bgType") == "color";
        return true;
    }

    void Home::ApplyTheme(int idx) {
        json s = Settings::Defaults();
        const json keepLib = *JGet(this->d->settings.data, "library");
        const json *ra = JGet(this->d->settings.data, "ra");
        const json keepRa = ra ? *ra : json::object();
        const char *names[] = { "Nexus Dunkel", "Aurora", "Kino", "Rad", "Hell & klar" };
        idx = std::clamp(idx, 0, 4);
        auto set = [&](const char *p, json v) { JSet(s, p, v); };
        if(idx == 1) { set("theme.accent", "#e86cff"); set("theme.ui", "#120a1f"); set("logo.anchorX", "center"); set("layout.mode", "carousel"); set("anim.focus", "glow"); set("home.bgType", "animated"); set("home.preset", 2.0); set("tileDefaults.radius", 18.0); }
        if(idx == 2) { set("theme.accent", "#ffd447"); set("theme.ui", "#000000"); set("logo.anchorX", "right"); set("logo.anchorY", "middle"); set("logo.scale", 1.15); set("layout.mode", "coverflow"); set("layout.uniform", true); set("topbar.tabStyle", "icons"); set("hints.style", "plain"); set("tileDefaults.ratio", "16:9"); set("tileDefaults.radius", 4.0); }
        if(idx == 3) { set("layout.mode", "arc"); set("layout.uniform", true); set("tileDefaults.ratio", "1:1"); set("logo.anchorX", "center"); set("topbar.tabStyle", "icons"); set("topbar.align", "center"); set("anim.easing", "spring"); }
        if(idx == 4) { set("theme.textColor", "black"); set("theme.ui", "#e9edf2"); set("theme.accent", "#00a2ff"); set("icons.color", "black"); set("topbar.tabStyle", "both"); set("topbar.bg", "glass"); set("hero.dim", 0.05); set("layout.mode", "rows"); set("home.bgType", "color"); set("home.color", "#e9edf2"); }
        set("theme.name", names[idx]);
        s["library"] = keepLib;
        s["ra"] = keepRa;
        this->d->settings.data = Settings::Sanitize(s, nullptr);
        this->listDirty = true;
        this->MarkDirty();
        this->Toast(std::string("Theme „") + names[idx] + "“ aktiv. Eigene Kachelanpassungen bleiben erhalten.");
    }

    void Home::OpenSettings(const std::string &group) {
        if(this->section != "settings") {
            this->section = "settings";
            this->openFolder.clear();
            this->focus = 0;
            this->scroll = 0;
            this->entryAnim = true;
            this->listDirty = true;
        }
        auto dl = std::make_shared<Dialog>();
        dl->title = "Einstellungen";
        dl->sub = "Alle Werte wirken sofort und werden automatisch gespeichert. Die Startseite dient als Vorschau.";
        dl->settings = true; dl->w = 470;
        dl->x = (IsVertical(this->d->settings) && ComputeArea(this->d->settings, this->Env()).side == "right") ? 20 : ScreenW - 490;
        dl->y = this->Env().topH + 4; dl->h = ScreenH - dl->y - this->Env().botH - 10;
        dl->hints = { { "A", "Öffnen" }, { "L/R", "Bereich" }, { "B", "Zur Startseite" } };
        dl->rows = [this]() {
            std::vector<Row> rows;
            for(const auto &[id, label]: Groups) {
                const auto gid = id;
                rows.push_back(Row{ label, nullptr, nullptr, [this, gid]() { this->OpenGroup(gid); } });
            }
            rows.push_back(Row{ "RetroAchievements & SteamGridDB", []() { return std::string("folgt in Phase 5"); }, nullptr, nullptr });
            return rows;
        };
        dl->onB = [this]() { this->CloseAll(); this->SwitchSection("start"); };
        this->Push(dl);
        if(!group.empty()) {
            for(size_t i = 0; i < Groups.size(); i++) if(Groups[i].first == group) dl->sel = static_cast<int>(i);
            this->OpenGroup(group);
        }
    }

    void Home::OpenGroup(const std::string &gid) {
        auto dl = std::make_shared<Dialog>();
        for(const auto &[id, label]: Groups) if(id == gid) dl->title = label;
        dl->settings = true; dl->w = 470;
        dl->x = this->dialogs.empty() ? ScreenW - 490 : this->dialogs.back()->x;
        dl->y = this->Env().topH + 4; dl->h = ScreenH - dl->y - this->Env().botH - 10;
        const bool preview = gid == "layout" || gid == "anim" || gid == "tiles" || gid == "hero" || gid == "logo";
        dl->sub = gid == "sections" ? "A = ein/aus · ←→ = Reihenfolge" : preview ? "Y = Vorschau-Fokus zur nächsten Kachel" : "";
        dl->hints = { { "↑↓", "Option" }, { "←→", "Wert" }, { "A", "Wählen" }, { "B", "Zurück" } };
        if(preview) dl->onY = [this]() { if(!this->list.empty()) this->SetFocus((this->focus + 1) % static_cast<int>(this->list.size())); };
        auto changed = [this]() { this->listDirty = true; this->layoutDirty = true; this->MarkDirty(); };
        if(gid == "sections") {
            dl->rows = [this, changed]() {
                std::vector<Row> rows;
                json &secs = this->d->settings.data["sections"];
                for(size_t i = 0; i < secs.size(); i++) {
                    const std::string id = JStr(secs[i], "id", "");
                    rows.push_back(Row{ SectionName(id),
                        [this, i]() { return JBool(this->d->settings.data["sections"][i], "visible", true) ? std::string("sichtbar") : std::string("versteckt"); },
                        [this, i, changed](int dir) {
                            json &a = this->d->settings.data["sections"];
                            const int j = std::clamp(static_cast<int>(i) + dir, 0, static_cast<int>(a.size()) - 1);
                            if(j != static_cast<int>(i)) { std::swap(a[i], a[j]); this->dialogs.back()->sel = j; changed(); }
                        },
                        [this, i, id, changed]() {
                            if(id == "settings") { this->Toast("Einstellungen bleiben immer erreichbar."); return; }
                            json &x = this->d->settings.data["sections"][i];
                            x["visible"] = !JBool(x, "visible", true);
                            changed();
                        } });
                }
                return rows;
            };
        }
        else if(gid == "sys") {
            dl->rows = [this]() {
                const char *names[] = { "Nexus Dunkel", "Aurora", "Kino", "Rad", "Hell & klar" };
                std::vector<Row> rows;
                for(int i = 0; i < 5; i++) rows.push_back(Row{ std::string("Theme: ") + names[i], nullptr, nullptr, [this, i]() { this->ApplyTheme(i); } });
                rows.push_back(Row{ "Sicherung", nullptr, nullptr, nullptr, true });
                rows.push_back(Row{ "Jetzt speichern", nullptr, nullptr, [this]() { this->Save(); this->Toast("Gespeichert: sdmc:/nexus/nexus.json"); } });
                rows.push_back(Row{ "Darstellung auf Standard (Bibliothek bleibt)", nullptr, nullptr, [this]() {
                    this->Confirm("Darstellung zurücksetzen?", "Ordner, Favoriten und Kachelanpassungen bleiben erhalten.", "Zurücksetzen", [this]() {
                        const json *ra = JGet(this->d->settings.data, "ra");
                        const json keepRa = ra ? *ra : json::object();
                        this->d->settings.data = Settings::Defaults();
                        this->d->settings.data["ra"] = keepRa;
                        this->d->settings.data = Settings::Sanitize(this->d->settings.data, nullptr);
                        this->listDirty = true; this->MarkDirty();
                    });
                } });
                rows.push_back(Row{ "Konfiguration prüfen und reparieren", nullptr, nullptr, [this]() {
                    std::vector<std::string> rep;
                    this->d->settings.data = Settings::Sanitize(this->d->settings.data, &rep);
                    this->d->library = Library::FromJson(this->d->library.ToJson(), &rep);
                    this->d->library.Reconcile(this->installed, nullptr);
                    this->listDirty = true; this->MarkDirty();
                    this->Toast(rep.empty() ? "Konfiguration ist in Ordnung." : std::to_string(rep.size()) + " Werte repariert.");
                } });
                rows.push_back(Row{ "Info", nullptr, nullptr, nullptr, true });
                rows.push_back(Row{ "Version", []() { return std::string("NEXUS HOME 0.1 (Test-App)"); }, nullptr, nullptr });
                rows.push_back(Row{ "Modus", [this]() { return std::string(this->applet ? "Applet (wenig Speicher)" : "Anwendung (voller Speicher)"); }, nullptr, nullptr });
                rows.push_back(Row{ "Texturspeicher", [this]() { return std::to_string(this->img->UsedBytes() / (1024 * 1024)) + " MB"; }, nullptr, nullptr });
                rows.push_back(Row{ "Beenden (+)", nullptr, nullptr, [this]() { this->Toast("Zum Beenden + drücken (außerhalb der Menüs)."); } });
                return rows;
            };
        }
        else {
            dl->rows = [this, gid, changed]() {
                std::vector<Row> rows;
                for(const auto &e: Schema()) {
                    if(e.group != gid || e.hidden || !this->SettingVisible(e.path)) continue;
                    const auto *ep = &e;
                    rows.push_back(Row{ e.label, [this, ep]() { return this->d->settings.Display(*ep); },
                        [this, ep, changed](int dir) { this->d->settings.Step(*ep, dir); changed(); }, nullptr });
                }
                if(gid == "lock") rows.push_back(Row{ "Sperrbildschirm jetzt anzeigen (−)", nullptr, nullptr, [this]() { this->CloseAll(); this->locked = true; this->unlocking = false; } });
                if(gid == "lib") rows.push_back(Row{ "Bilder neu einlesen", nullptr, nullptr, [this]() { this->art.clear(); this->img->Clear(); this->heroA.key.clear(); this->heroFor.clear(); this->logoId.clear(); this->Toast("Bilder werden neu geladen."); } });
                return rows;
            };
        }
        this->Push(dl);
    }

    // ------------------------------------------------------------------ Animation

    void Home::Update(double dt) {
        this->time += dt;
        this->frameDt = std::clamp(dt, 0.0, 0.1);
        this->wobbleT += dt;
        if(this->toastT > 0) this->toastT -= dt;
        if(this->dirtySave) {
            this->saveTimer -= dt;
            if(this->saveTimer <= 0) this->Save();
        }
        if(this->unlocking) {
            this->unlockT += dt;
            if(this->unlockT > 0.5) { this->locked = false; this->unlocking = false; }
        }
        const auto &s = this->d->settings;
        const bool animOn = s.B("anim.enabled");
        const double speed = !animOn ? 0 : (s.B("anim.reduce") ? 120 : s.N("anim.speed")) / 1000.0;
        const auto easing = s.S("anim.easing");
        const auto fx = s.S("anim.focus");
        const bool spring = easing == "spring" || easing == "bounce";
        const double zeta = easing == "bounce" ? 0.35 : 0.55;
        const double tau = std::max(0.001, speed / (easing == "snappy" ? 6.0 : easing == "linear" ? 2.5 : 3.5));
        const double omega = speed > 0 ? 2 * kPi / (speed * 1.6) : 0;
        const double a = speed <= 0 ? 1.0 : 1.0 - std::exp(-dt / tau);
        for(size_t i = 0; i < this->list.size() && i < this->lay.items.size(); i++) {
            auto &ds = this->disp[this->list[i].id];
            const auto &it = this->lay.items[i];
            const bool foc = static_cast<int>(i) == this->focus;
            const json t = this->Tile(this->list[i].id);
            double tsc = it.sc, ty = it.y, trot = it.rot;
            if(foc) {
                if(fx == "zoom" || fx == "glow" || fx == "pulse" || fx == "wobble") tsc *= JNum(t, "focusZoom", 1.06);
                if(fx == "lift") { ty -= 14; tsc *= 1.02; }
                if(fx == "tilt") trot -= 3;
            }
            const double tsq = std::max(0.12, std::cos(it.ry * kPi / 180.0));
            if(ds.delay > 0) { ds.delay -= dt; continue; }
            struct Ch { double &v; double &vel; double t; };
            Ch chans[] = { { ds.x, ds.vx, it.x }, { ds.y, ds.vy, ty }, { ds.w, ds.vw, it.w }, { ds.h, ds.vh, it.h }, { ds.rot, ds.vrot, trot }, { ds.squash, ds.vsq, tsq }, { ds.sc, ds.vsc, tsc }, { ds.op, ds.vop, it.op } };
            for(auto &c: chans) {
                if(speed <= 0) { c.v = c.t; c.vel = 0; continue; }
                if(spring && &c.v != &ds.op) {
                    const double acc = omega * omega * (c.t - c.v) - 2 * zeta * omega * c.vel;
                    c.vel += acc * dt;
                    c.v += c.vel * dt;
                }
                else c.v += (c.t - c.v) * a;
            }
        }
    }

    // ------------------------------------------------------------------ Zeichnen

    void Home::Draw() {
        this->g->BeginFrame();
        const Col ui = this->UiCol();
        SDL_SetRenderDrawColor(this->r, ui.r, ui.g, ui.b, 255);
        SDL_RenderClear(this->r);
        if(this->fatal) { this->DrawFatal(); return; }
        const double dt = this->frameDt;
        this->DrawBackground();
        this->DrawHero(dt);
        this->DrawLogo(dt);
        this->DrawTiles();
        this->DrawTopbar();
        this->DrawHints();
        this->DrawDialogs();
        if(this->locked) this->DrawLock();
        this->DrawToast();
    }

    void Home::DrawBackground() {
        const auto &s = this->d->settings;
        const auto type = s.S("home.bgType");
        if(type == "color") this->g->Rect(0, 0, ScreenW, ScreenH, ParseCol(s.S("home.color")));
        else {
            const int p = std::clamp(static_cast<int>(s.N("home.preset")), 0, 7);
            Col c0 = ParseCol(Presets[p][0]), c1 = ParseCol(Presets[p][1]), c2 = ParseCol(Presets[p][2]);
            double shift = 0.5;
            if(type == "animated" && s.B("anim.enabled") && !s.B("anim.reduce")) shift = 0.5 + 0.5 * std::sin(this->time * 2 * kPi / 24.0);
            this->g->GradV(0, 0, ScreenW, ScreenH * shift + 1, c0, c1);
            this->g->GradV(0, ScreenH * shift, ScreenW, ScreenH * (1 - shift) + 1, c1, c2);
        }
        const double dim = s.N("home.dim");
        if(dim > 0) this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(dim * 255) });
    }

    void Home::DrawHero(double dt) {
        const auto &s = this->d->settings;
        if(!s.B("hero.enabled")) return;
        const std::string fid = this->list.empty() ? std::string() : this->list[this->focus].id;
        if(fid != this->pendingHeroId) { this->pendingHeroId = fid; this->heroTimer = s.N("hero.debounce") / 1000.0; }
        else this->heroTimer -= dt;
        if(this->heroTimer <= 0 && fid != this->heroFor) {
            this->heroFor = fid;
            HeroSlot n;
            if(!fid.empty()) {
                const auto &a = this->Art(fid);
                if(!a.hero.empty()) n.key = a.hero;
                else if(!this->list[this->focus].folder) { n.key = this->list[this->focus].iconKey; n.icon = true; }
                else n.folderAccent = this->list[this->focus].accent;
            }
            this->heroB = this->heroA;
            this->heroA = n;
            this->heroA.alpha = 0;
            if(!n.key.empty()) this->lastHeroKey = n.key;
        }
        const double ms = s.N("hero.transitionMs") / 1000.0;
        const double step = ms <= 0 ? 1.0 : dt / ms;
        this->heroA.alpha = std::min(1.0, this->heroA.alpha + step);
        this->heroB.alpha = std::max(0.0, this->heroB.alpha - step);
        double px = 0;
        if(s.B("hero.parallax") && this->focus < static_cast<int>(this->lay.items.size())) {
            const auto &it = this->lay.items[this->focus];
            px = -((it.x + it.w / 2) - 640) / 40.0;
        }
        const double kb = s.B("hero.kenBurns") ? 1.0 + 0.06 * (1 + std::sin(this->time * 2 * kPi / 36.0)) : 1.0;
        for(HeroSlot *slot: { &this->heroB, &this->heroA }) {
            if(slot->alpha <= 0.01) continue;
            if(!slot->key.empty()) {
                SDL_Texture *t = this->img->Get(slot->key, 0);
                if(t) {
                    const double w = (ScreenW + 60) * kb, h = (ScreenH + 60) * kb;
                    this->g->Image(t, (ScreenW - w) / 2 + px, (ScreenH - h) / 2, w, h, slot->alpha, true);
                    if(slot->icon) this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(110 * slot->alpha) });
                }
            }
            else if(!slot->folderAccent.empty()) {
                this->g->GradV(0, 0, ScreenW, ScreenH, WithA(ParseCol(slot->folderAccent), 0.55 * slot->alpha), WithA(this->UiCol(), slot->alpha));
            }
        }
        // Ebene 2: Abdunkelung + Verlauf
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(s.N("hero.dim") * 255) });
        if(s.B("hero.gradient")) {
            const Col ui = this->UiCol();
            this->g->GradV(0, ScreenH * 0.35, ScreenW, ScreenH * 0.65, WithA(ui, 0), ui);
            this->g->GradH(0, 0, 700, ScreenH, Col{ 0, 0, 0, 128 }, Col{ 0, 0, 0, 0 });
        }
    }

    void Home::DrawLogo(double dt) {
        const auto &s = this->d->settings;
        if(!s.B("logo.visible") || this->list.empty()) return;
        const auto &e = this->list[this->focus];
        if(e.id != this->logoId) {
            this->logoId = e.id;
            this->logoAlpha = 0;
            const auto anim = s.S("logo.anim");
            this->logoSlide = (s.B("anim.enabled") && anim == "slide") ? -30 : 0;
        }
        const bool animOn = s.B("anim.enabled") && s.S("logo.anim") != "none";
        this->logoAlpha = animOn ? std::min(1.0, this->logoAlpha + dt / 0.38) : 1.0;
        this->logoSlide = animOn ? this->logoSlide * std::exp(-dt / 0.09) : 0;
        // Textur: eigenes Logo oder Schriftzug
        SDL_Texture *tex = nullptr;
        int tw = 0, th = 0;
        const auto &a = this->Art(e.id);
        if(!a.logo.empty()) {
            tex = this->img->Get(a.logo, 0);
            if(tex) SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        }
        if(!tex) tex = this->g->TextTex(e.name, 64, Col{ 255, 255, 255, 255 }, 4, Col{ 10, 12, 18, 255 }, tw, th, true);
        if(!tex || tw <= 0 || th <= 0) return;
        // Bereich für das Logo (Sicherheitsbereich: verdeckt keine Kacheln)
        const auto env = this->Env();
        const LArea A = ComputeArea(s, env);
        double bx0 = 0, bx1 = ScreenW, by0 = env.topH, by1 = ScreenH - env.botH;
        if(s.B("logo.safeArea")) {
            if(A.v) {
                if(A.side == "right") bx1 = A.x0 - 20;
                else if(A.side == "left") bx0 = A.x1 + 20;
                else by1 = std::max(by0 + 80, A.y0);
            }
            else {
                const auto pos = s.S("layout.position");
                const double aboveH = A.y0 - 16 - by0, belowH = by1 - (A.y1 + 16);
                if(pos == "bottom" || (pos == "center" && aboveH >= belowH)) by1 = A.y0 - 16;
                else by0 = A.y1 + 16;
            }
        }
        const bool isImg = !a.logo.empty();
        double w = isImg ? 460 * s.N("logo.scale") : tw * 0.9 * s.N("logo.scale");
        double h = w * th / tw;
        const double bw = bx1 - bx0, bh = by1 - by0;
        if(s.B("logo.safeArea") && (w > bw - 20 || h > bh)) {
            const double k = std::max(0.1, std::min((bw - 20) / w, bh / h));
            w *= k; h *= k;
        }
        const auto ax = s.S("logo.anchorX"), ay = s.S("logo.anchorY");
        double x = ax == "left" ? bx0 + 50 : ax == "center" ? (bx0 + bx1 - w) / 2 : bx1 - w - 50;
        double y = ay == "top" ? by0 + 16 : ay == "middle" ? (by0 + by1 - h) / 2 : by1 - h;
        x += s.N("logo.offsetX");
        y += s.N("logo.offsetY");
        if(s.B("logo.safeArea")) {
            x = std::clamp(x, bx0, std::max(bx0, bx1 - w));
            y = std::clamp(y, by0, std::max(by0, by1 - h));
        }
        if(s.B("logo.float") && s.B("anim.enabled") && !s.B("anim.reduce")) y += std::sin(this->time * 2 * kPi / 5.0) * 8;
        x += this->logoSlide;
        const double alpha = this->logoAlpha * s.N("logo.opacity");
        if(s.B("logo.shadow")) this->g->Image(tex, x + 3, y + 6, w, h, alpha * 0.5, true, Col{ 0, 0, 0, 255 });
        this->g->Image(tex, x, y, w, h, alpha, true);
    }

    void Home::DrawTiles() {
        const auto &s = this->d->settings;
        if(this->list.empty()) {
            const LArea A = ComputeArea(s, this->Env());
            const char *msg = this->section == "homebrew" ? "Homebrew-Einträge folgen in einer späteren Version." :
                              this->section == "fav" ? "Noch keine Favoriten – Y auf einer Kachel drücken." :
                              this->apps->Busy() ? "Bibliothek wird geladen …" : "Keine Einträge in diesem Bereich.";
            this->g->Text(msg, 18, 60, (A.y0 + A.y1) / 2, this->MutedCol());
            return;
        }
        const auto fx = s.S("anim.focus");
        const auto idle = (s.B("anim.enabled") && !s.B("anim.reduce")) ? s.S("anim.idle") : std::string("none");
        const bool idleFocusOnly = s.B("anim.idleFocusOnly");
        const bool isList = s.S("layout.mode") == "list";
        std::vector<int> order;
        for(int i = 0; i < static_cast<int>(this->list.size()) && i < static_cast<int>(this->lay.items.size()); i++) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const int za = a == this->focus ? 100000 : this->lay.items[a].z, zb = b == this->focus ? 100000 : this->lay.items[b].z;
            return za < zb;
        });
        Xf focusXf;
        double focusRad = 0;
        bool focusHex = false, haveFocus = false;
        for(const int i: order) {
            const auto &e = this->list[i];
            auto dit = this->disp.find(e.id);
            if(dit == this->disp.end()) continue;
            const Disp &ds = dit->second;
            const json t = this->Tile(e.id);
            const double op = ds.op * JNum(t, "opacity", 1);
            if(op < 0.02 || ds.x > ScreenW + 50 || ds.x + ds.w * ds.sc < -50 || ds.y > ScreenH + 50 || ds.y + ds.h * ds.sc < -50) continue;
            Xf xf;
            xf.cx = ds.x + ds.w / 2; xf.cy = ds.y + ds.h / 2; xf.w = ds.w; xf.h = ds.h;
            xf.rot = ds.rot; xf.squash = ds.squash; xf.sc = ds.sc;
            const bool foc = i == this->focus;
            if(idle != "none" && (!idleFocusOnly || foc)) {
                const double ph = this->time * 2 * kPi / 4.0 + i * 0.7;
                if(idle == "float") xf.cy -= 3 + 3 * std::sin(ph);
                else if(idle == "breathe") xf.sc *= 1.0 + 0.0125 * (1 + std::sin(ph));
                else if(idle == "sway") xf.rot += std::sin(ph * 0.8);
            }
            if(foc && fx == "wobble" && this->wobbleT < 0.5) xf.rot += std::sin(this->wobbleT * 25) * 3 * (1 - this->wobbleT / 0.5);
            const double rad = JNum(t, "radius", 10);
            const bool hex = i < static_cast<int>(this->lay.items.size()) && this->lay.items[i].hex;
            if(isList) {
                // Listenzeile: Hintergrund, Icon links, Name
                this->g->Shape(xf, rad, false, WithA(Col{ 127, 127, 127, 255 }, 0.18 * op));
                Xf ic = xf;
                ic.w = ic.h = ds.h - 8;
                ic.cx = ds.x + 4 + ic.w / 2;
                SDL_Texture *it = e.folder ? nullptr : this->img->Get(this->Art(e.id).grid.empty() ? e.iconKey : this->Art(e.id).grid, 2);
                if(it) this->g->ShapeTex(it, ic, rad * 0.6, false, op, true, 50, 50);
                else this->g->Shape(ic, rad * 0.6, false, WithA(e.folder ? ParseCol(e.accent) : Col{ 60, 70, 90, 255 }, op));
                this->g->Text(e.name, 18, ds.x + ds.h + 14, xf.cy - 20, WithA(this->TextCol(), op), 0, static_cast<int>(ds.w - ds.h - 40));
                this->g->Text(e.folder ? "Ordner · " + std::to_string(e.folderCount) + " Einträge" : "Titel-ID " + e.id, 13, ds.x + ds.h + 14, xf.cy + 4, WithA(this->MutedCol(), op));
            }
            else {
                if(JBool(t, "shadow", true) && !hex) {
                    Xf sh = xf;
                    sh.cy += 8 * xf.sc;
                    this->g->Shape(sh, rad, false, Col{ 0, 0, 0, static_cast<uint8_t>(90 * op) });
                }
                const auto &a = this->Art(e.id);
                const bool cover = JStr(t, "fit", "cover") == "cover";
                const double posX = JNum(t, "posX", 50), posY = JNum(t, "posY", 50);
                SDL_Texture *tex = nullptr;
                if(!a.grid.empty()) tex = this->img->Get(a.grid, 1);
                else if(!e.folder && !e.iconKey.empty()) tex = this->img->Get(e.iconKey, 2);
                if(tex) this->g->ShapeTex(tex, xf, rad, hex, op, cover, posX, posY);
                else if(e.folder) {
                    // Ordnerkachel: Akzentfläche + bis zu 4 Icons
                    this->g->Shape(xf, rad, hex, WithA(ParseCol(e.accent), op));
                    const double cell = std::min(ds.w, ds.h) * 0.36;
                    const double cs = std::cos(xf.rot * kPi / 180.0), sn = std::sin(xf.rot * kPi / 180.0);
                    for(size_t k = 0; k < e.folderIcons.size(); k++) {
                        SDL_Texture *ft = this->img->Get(e.folderIcons[k], 3);
                        if(!ft) continue;
                        const double ox = (k % 2 ? 0.5 : -0.5) * cell * 1.08 * xf.sc * xf.squash, oy = (k / 2 ? 0.5 : -0.5) * cell * 1.08 * xf.sc;
                        Xf m = xf;
                        m.w = m.h = cell;
                        m.cx = xf.cx + ox * cs - oy * sn;
                        m.cy = xf.cy + ox * sn + oy * cs;
                        this->g->ShapeTex(ft, m, 6, false, op, true, 50, 50);
                    }
                }
                else {
                    this->g->Shape(xf, rad, hex, Col{ 40, 48, 64, static_cast<uint8_t>(255 * op) });
                    if(ds.w > 70) this->g->Text(e.name, 13, xf.cx, xf.cy - 8, WithA(Col{ 220, 226, 236, 255 }, op), 1, static_cast<int>(ds.w - 12));
                }
                const double bw = JNum(t, "borderWidth", 0);
                if(bw > 0) this->g->ShapeRing(xf, rad, hex, bw * xf.sc, WithA(ParseCol(JStr(t, "borderColor", "#ffffff")), op));
                if(JBool(t, "showTitle", false) && std::abs(xf.rot) < 0.5 && ds.w > 60) {
                    const double bx = xf.cx - ds.w * xf.sc * xf.squash / 2, by = xf.cy + ds.h * xf.sc / 2;
                    this->g->GradV(bx, by - 34, ds.w * xf.sc * xf.squash, 34, Col{ 0, 0, 0, 0 }, Col{ 0, 0, 0, static_cast<uint8_t>(200 * op) });
                    this->g->Text(e.name, 13, bx + 8, by - 22, WithA(Col{ 255, 255, 255, 255 }, op), 0, static_cast<int>(ds.w * xf.sc * xf.squash - 16));
                }
                if(e.folder && std::abs(xf.rot) < 0.5) {
                    const double rx = xf.cx + ds.w * xf.sc * xf.squash / 2 - 10, ry = xf.cy - ds.h * xf.sc / 2 + 8;
                    const std::string c = std::to_string(e.folderCount);
                    const int cw = this->g->TextW(c, 12, true);
                    this->g->RoundRect(rx - cw - 14, ry, cw + 14, 20, 10, Col{ 0, 0, 0, static_cast<uint8_t>(170 * op) });
                    this->g->Text(c, 12, rx - cw - 7, ry + 2, WithA(Col{ 255, 255, 255, 255 }, op), 0, 0, true);
                }
                if(!e.folder && this->d->library.IsFav(e.id) && std::abs(xf.rot) < 0.5) {
                    this->g->Icon("fav", xf.cx - ds.w * xf.sc * xf.squash / 2 + 14, xf.cy - ds.h * xf.sc / 2 + 14, 16, Col{ 255, 212, 71, static_cast<uint8_t>(255 * op) }, true);
                }
            }
            if(foc) { focusXf = xf; focusRad = rad; focusHex = hex; haveFocus = true; }
        }
        // Ebene 5: Fokusmarkierung (eigene Ebene, nicht Teil der Kachel)
        if(haveFocus) {
            Col acc = this->moving ? Col{ 255, 212, 71, 255 } : this->Accent();
            double pulse = 1.0;
            if(fx == "pulse" && s.B("anim.enabled")) pulse = 0.55 + 0.45 * std::sin(this->time * 4.5);
            const int layers = fx == "glow" ? 5 : 3;
            for(int k = layers; k >= 1; k--) this->g->ShapeRing(focusXf, focusRad, focusHex, 3 + k * (fx == "glow" ? 5 : 3), WithA(acc, 0.12 * pulse / k * (fx == "glow" ? 1.6 : 1)));
            this->g->ShapeRing(focusXf, focusRad, focusHex, 3.5, WithA(acc, pulse));
        }
        if(!this->openFolder.empty()) {
            auto *f = this->d->library.FindFolder(this->openFolder);
            this->g->Text(std::string(SectionName(this->section)) + "  ›  " + (f ? f->name : "") + "   ·   B = zurück", 14, 60, this->Env().topH, this->MutedCol());
        }
        if(this->moving) {
            this->g->RoundRect(490, 70, 300, 30, 15, Col{ 255, 212, 71, 255 });
            this->g->Text("Positionsmodus – A übernehmen, B verwerfen", 13, 640, 76, Col{ 0, 0, 0, 255 }, 1, 0, true);
        }
    }

    void Home::DrawTopbar() {
        const auto &s = this->d->settings;
        this->tabHits.clear();
        if(!s.B("topbar.visible")) return;
        const auto bg = s.S("topbar.bg");
        const bool light = this->Light();
        if(bg == "fade") this->g->GradV(0, 0, ScreenW, 60, light ? Col{ 255, 255, 255, 150 } : Col{ 0, 0, 0, 140 }, Col{ 0, 0, 0, 0 });
        else if(bg == "solid") this->g->Rect(0, 0, ScreenW, 56, this->UiCol());
        else if(bg == "glass") this->g->Rect(0, 0, ScreenW, 56, Col{ 127, 127, 127, 50 });
        const Col tc = this->TextCol(), mc = this->MutedCol();
        const auto icStr = s.S("icons.color");
        const Col ic = icStr == "white" ? Col{ 255, 255, 255, 255 } : icStr == "black" ? Col{ 0, 0, 0, 255 } : icStr == "accent" ? this->Accent() : tc;
        const bool filled = s.S("icons.style") == "filled";
        const double isz = 20 * s.N("icons.size");
        const auto style = s.S("topbar.tabStyle");
        // Status rechts
        double rx = ScreenW - 40;
        if(s.B("topbar.showBattery")) {
            const int pct = plat::BatteryPercent();
            if(s.B("topbar.batteryPct") && pct >= 0) {
                const std::string t = std::to_string(pct) + " %";
                rx -= this->g->TextW(t, 16);
                this->g->Text(t, 16, rx, 18, tc);
                rx -= 8;
            }
            rx -= isz * 1.4;
            this->g->Battery(rx, 28 - isz * 0.31, isz, pct, plat::BatteryCharging(), ic);
            rx -= 16;
        }
        if(s.B("topbar.showWifi")) {
            const int bars = plat::WifiBars();
            this->g->Wifi(rx - isz / 2, 28, isz, bars < 0 ? 0 : bars, bars < 0 ? WithA(ic, 0.4) : ic);
            rx -= isz + 14;
        }
        const std::time_t now = std::time(nullptr);
        const std::tm *lt = std::localtime(&now);
        char buf[32];
        if(s.B("topbar.showClock")) {
            if(s.B("topbar.clock24")) std::strftime(buf, sizeof(buf), "%H:%M", lt);
            else std::strftime(buf, sizeof(buf), "%I:%M %p", lt);
            rx -= this->g->TextW(buf, 16);
            this->g->Text(buf, 16, rx, 18, tc);
            rx -= 16;
        }
        if(s.B("topbar.showDate")) {
            std::strftime(buf, sizeof(buf), "%d.%m.", lt);
            rx -= this->g->TextW(buf, 16);
            this->g->Text(buf, 16, rx, 18, mc);
            rx -= 16;
        }
        // Bereiche
        const auto vis = s.VisibleSections();
        std::vector<double> widths;
        double total = 0;
        for(const auto &id: vis) {
            double w = 28;
            if(style != "icons") w += this->g->TextW(SectionName(id), 15);
            if(style != "text") w += isz + (style == "both" ? 8 : 0);
            widths.push_back(w);
            total += w + 4;
        }
        const auto align = s.S("topbar.align");
        double x = align == "center" ? (ScreenW - total) / 2 : align == "right" ? rx - total - 10 : 40;
        for(size_t i = 0; i < vis.size(); i++) {
            const bool active = vis[i] == this->section && this->openFolder.empty();
            const double w = widths[i];
            if(active) {
                this->g->RoundRect(x, 12, w, 34, 17, Col{ 127, 127, 127, 56 });
                this->g->Rect(x + 14, 43, w - 28, 2.5, this->Accent());
            }
            double cx = x + 14;
            if(style != "text") {
                this->g->Icon(vis[i], cx + isz / 2, 29, isz, active ? ic : WithA(ic, 0.62), filled);
                cx += isz + (style == "both" ? 8 : 0);
            }
            if(style != "icons") this->g->Text(SectionName(vis[i]), 15, cx, 19, active ? tc : mc);
            this->tabHits.push_back(Hit{ x, 8, w, 42, vis[i], static_cast<int>(i) });
            x += w + 4;
        }
    }

    void Home::DrawHints() {
        const auto &s = this->d->settings;
        if(!s.B("hints.visible")) return;
        if(s.S("hints.style") == "bar") this->g->Rect(0, ScreenH - 36, ScreenW, 36, this->Light() ? Col{ 255, 255, 255, 150 } : Col{ 0, 0, 0, 115 });
        std::vector<std::pair<std::string, std::string>> h;
        if(this->locked) h = { { "A", "Entsperren" } };
        else if(!this->dialogs.empty()) h = this->dialogs.back()->hints.empty() ? std::vector<std::pair<std::string, std::string>>{ { "A", "Auswählen" }, { "←→", "Wert" }, { "B", "Zurück" } } : this->dialogs.back()->hints;
        else if(this->moving) h = { { "←→", "Verschieben" }, { "A", "Übernehmen" }, { "B", "Verwerfen" } };
        else {
            const bool folder = !this->list.empty() && this->list[this->focus].folder;
            h = { { "A", folder ? "Öffnen" : "Starten" } };
            if(!this->openFolder.empty()) h.push_back({ "B", "Zurück" });
            h.push_back({ "X", "Optionen" });
            h.push_back({ "Y", "Favorit" });
            h.push_back({ "L/R", "Bereich" });
            h.push_back({ "−", "Sperren" });
            h.push_back({ "+", "Beenden" });
        }
        double x = ScreenW - 40;
        const Col tc = this->TextCol(), mc = this->MutedCol();
        for(auto it = h.rbegin(); it != h.rend(); ++it) {
            const int lw = this->g->TextW(it->second, 14);
            x -= lw;
            this->g->Text(it->second, 14, x, ScreenH - 27, mc);
            const int bw = std::max(20, this->g->TextW(it->first, 12, true) + 10);
            x -= bw / 2.0 + 6;
            this->g->Button(it->first, x, ScreenH - 18, 20, tc, this->UiCol());
            x -= bw / 2.0 + 18;
        }
    }

    void Home::DrawDialogs() {
        this->rowHits.clear();
        this->panelHit = Hit{ 0, 0, 0, 0, "", -1 };
        if(this->dialogs.empty()) return;
        const auto &s = this->d->settings;
        const bool light = this->Light();
        const auto ps = s.S("theme.panelStyle");
        const Col panel = ps == "solid" ? this->UiCol() : ps == "outline" ? Col{ 0, 0, 0, 90 } : (light ? Col{ 246, 248, 251, 240 } : Col{ 14, 18, 28, 238 });
        const Col tc = this->TextCol(), mc = this->MutedCol(), acc = this->Accent();
        for(size_t di = 0; di < this->dialogs.size(); di++) {
            auto &dl = *this->dialogs[di];
            const bool top = di + 1 == this->dialogs.size();
            if(dl.shade) this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, 110 });
            auto rows = dl.rows();
            const double headH = 46 + (dl.sub.empty() ? 0 : 22 * (1 + static_cast<int>(this->g->TextW(dl.sub, 13) / (dl.w - 40))));
            double contentH = 0;
            for(const auto &rw: rows) contentH += rw.head ? 30 : 38;
            const double maxH = ScreenH - dl.y - this->Env().botH - 6;
            const double h = dl.h > 0 ? dl.h : std::min(maxH, headH + contentH + 14);
            this->g->RoundRect(dl.x, dl.y, dl.w, h, 14, panel);
            if(ps == "outline") this->g->RoundRectOutline(dl.x, dl.y, dl.w, h, 14, 2, acc);
            this->g->Text(dl.title, 20, dl.x + 20, dl.y + 14, tc, 0, static_cast<int>(dl.w - 40), true);
            if(!dl.sub.empty()) {
                // einfacher Zeilenumbruch
                std::string line, word, rest = dl.sub + " ";
                double yy = dl.y + 44;
                for(char c: rest) {
                    if(c == ' ') {
                        const std::string cand = line.empty() ? word : line + " " + word;
                        if(this->g->TextW(cand, 13) > dl.w - 40 && !line.empty()) { this->g->Text(line, 13, dl.x + 20, yy, mc); yy += 20; line = word; }
                        else line = cand;
                        word.clear();
                    }
                    else word += c;
                }
                if(!line.empty()) this->g->Text(line, 13, dl.x + 20, yy, mc);
            }
            // Scrollen, damit die Auswahl sichtbar ist
            const double areaY = dl.y + headH, areaH = h - headH - 10;
            double selTop = 0, y = 0;
            for(int i = 0; i < static_cast<int>(rows.size()); i++) { if(i == dl.sel) selTop = y; y += rows[i].head ? 30 : 38; }
            if(selTop - dl.scroll < 0) dl.scroll = selTop;
            if(selTop + 38 - dl.scroll > areaH) dl.scroll = selTop + 38 - areaH;
            dl.scroll = std::clamp(dl.scroll, 0.0, std::max(0.0, contentH - areaH));
            y = areaY - dl.scroll;
            for(int i = 0; i < static_cast<int>(rows.size()); i++) {
                const auto &rw = rows[i];
                const double rh = rw.head ? 30 : 38;
                if(y + rh > areaY - 1 && y < areaY + areaH - 2) {
                    if(rw.head) this->g->Text(rw.label, 12, dl.x + 22, y + 12, acc, 0, 0, true);
                    else {
                        const bool sel = i == dl.sel && top;
                        if(sel) {
                            this->g->RoundRect(dl.x + 10, y, dl.w - 20, rh - 2, 8, light ? Col{ 0, 0, 0, 20 } : Col{ 255, 255, 255, 26 });
                            this->g->Rect(dl.x + 10, y + 4, 3, rh - 10, acc);
                        }
                        const std::string val = rw.value ? rw.value() : std::string();
                        const int vw = val.empty() ? 0 : this->g->TextW(val, 13);
                        this->g->Text(rw.label, 15, dl.x + 22, y + 9, tc, 0, static_cast<int>(dl.w - 60 - vw));
                        if(!val.empty()) {
                            const bool adj = static_cast<bool>(rw.adjust);
                            const double vx = dl.x + dl.w - 30 - (adj && sel ? 14 : 0);
                            this->g->Text(val, 13, vx, y + 11, mc, 2);
                            if(adj && sel) {
                                this->g->Text("<", 14, vx - vw - 16, y + 10, mc, 0, 0, true);
                                this->g->Text(">", 14, dl.x + dl.w - 34, y + 10, mc, 0, 0, true);
                            }
                        }
                        else if(rw.activate) this->g->Text(">", 14, dl.x + dl.w - 34, y + 10, mc, 0, 0, true);
                        if(top) this->rowHits.push_back(Hit{ dl.x + 10, y, dl.w - 20, rh, "", i });
                    }
                }
                y += rh;
            }
            if(top) this->panelHit = Hit{ dl.x, dl.y, dl.w, h, "", -1 };
        }
    }

    void Home::DrawLock() {
        const auto &s = this->d->settings;
        const double t = this->unlocking ? EaseOut(this->unlockT / 0.5) : 0;
        const auto anim = s.S("lock.anim");
        const double oy = anim == "slideUp" ? -ScreenH * t : 0;
        const double alpha = anim == "slideUp" ? 1.0 : 1.0 - t;
        const auto type = s.S("lock.bgType");
        SDL_Texture *hero = nullptr;
        if(type == "hero" && !this->lastHeroKey.empty()) hero = this->img->Get(this->lastHeroKey, 0);
        if(hero) this->g->Image(hero, 0, oy, ScreenW, ScreenH, alpha, true);
        else if(type == "color") this->g->Rect(0, oy, ScreenW, ScreenH, WithA(ParseCol(s.S("lock.color")), alpha));
        else if(type == "home") {
            const int p = std::clamp(static_cast<int>(s.N("home.preset")), 0, 7);
            this->g->GradV(0, oy, ScreenW, ScreenH, WithA(ParseCol(Presets[p][0]), alpha), WithA(ParseCol(Presets[p][1]), alpha));
        }
        else {
            const int p = std::clamp(static_cast<int>(s.N("lock.preset")), 0, 7);
            this->g->GradV(0, oy, ScreenW, ScreenH / 2 + 1, WithA(ParseCol(Presets[p][0]), alpha), WithA(ParseCol(Presets[p][1]), alpha));
            this->g->GradV(0, oy + ScreenH / 2, ScreenW, ScreenH / 2, WithA(ParseCol(Presets[p][1]), alpha), WithA(ParseCol(Presets[p][2]), alpha));
        }
        this->g->Rect(0, oy, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(s.N("lock.dim") * 255 * alpha) });
        const Col col = WithA(s.S("lock.textColor") == "black" ? Col{ 17, 17, 17, 255 } : Col{ 255, 255, 255, 255 }, alpha);
        const std::time_t now = std::time(nullptr);
        const std::tm *lt = std::localtime(&now);
        char clk[16], date[64];
        std::strftime(clk, sizeof(clk), s.B("lock.clock24") ? "%H:%M" : "%I:%M", lt);
        static const char *const days[] = { "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag" };
        static const char *const months[] = { "Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember" };
        std::snprintf(date, sizeof(date), "%s, %d. %s", days[lt->tm_wday % 7], lt->tm_mday, months[lt->tm_mon % 12]);
        const int size = static_cast<int>(s.N("lock.clockSize") * 0.8);
        const auto pos = s.S("lock.clockPos");
        const int cw = this->g->TextW(clk, size);
        double x = 70, y = 80;
        int align = 0;
        if(pos == "center") { x = ScreenW / 2; y = ScreenH * 0.34 - size * 0.4; align = 1; }
        else if(pos == "bottomLeft") { y = ScreenH - 140 - size; }
        else if(pos == "bottomRight") { x = ScreenW - 70; y = ScreenH - 140 - size; align = 2; }
        (void)cw;
        this->g->Text(clk, size, x, y + oy, col, align);
        if(s.B("lock.showDate")) this->g->Text(date, 24, x, y + oy + size * 1.15, col, align);
        if(s.B("lock.showBattery")) {
            const int pct = plat::BatteryPercent();
            if(pct >= 0) {
                const std::string ps = std::to_string(pct) + " %";
                this->g->Text(ps, 16, ScreenW - 40, 26 + oy, col, 2);
                this->g->Battery(ScreenW - 80 - this->g->TextW(ps, 16), 28 + oy, 20, pct, plat::BatteryCharging(), col);
            }
        }
        if(s.B("lock.hint")) {
            const double pa = 0.55 + 0.45 * std::sin(this->time * 2.8);
            this->g->Text("Zum Entsperren A drücken oder tippen", 16, ScreenW / 2, ScreenH - 70 + oy, WithA(col, pa), 1);
        }
    }

    void Home::DrawToast() {
        if(this->toastT <= 0 || this->toastText.empty()) return;
        const double a = std::min(1.0, this->toastT / 0.3);
        const int w = std::min(1100, this->g->TextW(this->toastText, 14) + 36);
        const double x = (ScreenW - w) / 2, y = ScreenH - 92;
        this->g->RoundRect(x, y, w, 38, 10, WithA(this->Light() ? Col{ 246, 248, 251, 245 } : Col{ 30, 38, 54, 245 }, a));
        this->g->Text(this->toastText, 14, ScreenW / 2, y + 10, WithA(this->TextCol(), a), 1, w - 24);
    }

    void Home::DrawFatal() {
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 5, 7, 11, 255 });
        this->g->Text("NEXUS konnte die Oberfläche nicht darstellen.", 24, ScreenW / 2, 280, Col{ 238, 242, 247, 255 }, 1);
        this->g->Text(this->fatalMsg, 14, ScreenW / 2, 324, Col{ 147, 160, 180, 255 }, 1, 1000);
        this->g->Text("A = Darstellung zurücksetzen (Bibliothek bleibt erhalten)   ·   + = Beenden", 16, ScreenW / 2, 380, Col{ 62, 224, 200, 255 }, 1);
    }

}
