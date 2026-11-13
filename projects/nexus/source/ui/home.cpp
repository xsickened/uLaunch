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
        // Erst true, wenn NEXUS innerhalb von uLaunch (uMenu) läuft und über uSystem startet.
        constexpr bool CanLaunchGames = false;
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
            { "look", "Darstellung & Schrift" }, { "ui", "Textboxen & Schrift" }, { "aura", "Kachel-Aura (Partikel)" }, { "bgfx", "Hintergrund-Effekte" }, { "idle", "Bildschirmschoner" }, { "top", "Kopfzeile, Symbole & Hinweise" }, { "layout", "Anordnung der Kacheln" },
            { "anim", "Animationen" }, { "tiles", "Kacheln (Standard)" }, { "hero", "Fokus: Hintergrund" }, { "logo", "Fokus: Logo" },
            { "home", "Home-Hintergrund" }, { "lock", "Sperrbildschirm" }, { "profile", "Profilauswahl" }, { "sections", "Bereiche (Reihenfolge & Sichtbarkeit)" },
            { "lib", "Bibliothek & SteamGridDB" }, { "ra", "RetroAchievements" }, { "sys", "Themes & System" }
        };
        const char *const Presets[8][3] = {
            { "#0b0f17", "#1d2b4a", "#0b0f17" }, { "#1a0b2e", "#4a1d6b", "#0f0a1f" }, { "#0d3b4c", "#1b6e6b", "#0a1f2b" }, { "#2b0f0f", "#6b2a1d", "#1a0a0a" },
            { "#0f2b14", "#2a6b3b", "#0a1a0e" }, { "#2b2b0f", "#6b5a1d", "#14140a" }, { "#e9edf2", "#c9d6ea", "#f6f8fb" }, { "#101010", "#3a3a3a", "#000000" }
        };
        double EaseOut(double t) { t = std::clamp(t, 0.0, 1.0); return 1 - (1 - t) * (1 - t) * (1 - t); }
        std::string Lower(std::string s) { for(auto &c: s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
    }

    // ------------------------------------------------------------------ Grundlagen

    bool Home::Init(SDL_Renderer *rr, Gfx *gg, ImageCache *im, Apps *ap, NexusData *dd, bool safeMode, bool isApplet, Host *h) {
        this->r = rr; this->g = gg; this->img = im; this->apps = ap; this->d = dd; this->applet = isApplet; this->host = h;
        // In uLaunch entscheidet der Unterbau, wann Sperrbildschirm/Profilauswahl kommen
        this->locked = !h && this->d->settings.B("lock.enabled") && !safeMode;
        if(h) this->currentUser = h->CurrentUser();
        this->online.Init(this->d);
        this->online.onArtChanged = [this](const std::string &id) { this->ArtChanged(id); };
        this->online.onFileObsolete = [this](const std::string &f) { this->ForgetFile(f); };
        this->img->SetDiskCache(std::string(CacheDir) + "/img");
        EnsureDir(std::string(ArtDir) + "/backgrounds");
        this->accentP = AccentFrom(this->d->settings);
        this->ImportKeys(true);
        {
            // tägliche Sicherung von nexus.json (backup/, die neuesten 10)
            char date[16];
            const std::time_t now = std::time(nullptr);
            std::strftime(date, sizeof(date), "%Y-%m-%d", std::localtime(&now));
            BackupDaily(DataFile, date);
        }
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
        if(tc == "custom") {
            const Col c = ParseCol(this->d->settings.S("theme.textCustom"));
            return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0 < 0.5;   // dunkle Schrift = helles Design
        }
        const Col u = this->UiCol();
        return (0.299 * u.r + 0.587 * u.g + 0.114 * u.b) / 255.0 > 0.55;
    }
    Col Home::TextCol() const {
        const auto &s = this->d->settings;
        Col c = s.S("theme.textColor") == "custom" ? ParseCol(s.S("theme.textCustom")) : this->Light() ? Col{ 18, 22, 29, 255 } : Col{ 238, 242, 247, 255 };
        if(s.S("theme.textEffect") == "accent") { c = this->Accent(); c.paint = 1; }   // Schrift im Akzent-Verlauf (Symbole: Mittelfarbe)
        return c;
    }
    Col Home::MutedCol() const { return WithA(this->TextCol(), this->d->settings.N("theme.mutedOpacity")); }
    Col Home::AccentAtU(double u) const {
        const Rgb c = AccentAt(this->accentP, u, this->time);
        return Col{ static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b), 255 };
    }
    Col Home::Accent() const { return this->AccentAtU(0.5); }
    ColFn Home::AccentFn(double x, double y, double w, double h) const {
        // Verlauf bezogen auf das jeweilige Element (wie im Prototyp per CSS)
        const AccentParams p = this->accentP;
        const double t = this->time;
        if(p.mode == "solid" && p.anim != "pulse") {
            const Col c = this->Accent();
            return [c](float, float) { return c; };
        }
        return [p, t, x, y, w, h](float px, float py) {
            const Rgb c = AccentAt(p, AccentU(p, px - x, py - y, w, h), t);
            return Col{ static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b), 255 };
        };
    }
    Col Home::UiCol() const { return ParseCol(this->d->settings.S("theme.ui")); }

    std::string Home::DisplayName(const std::string &id) {
        const auto custom = this->d->library.CustomName(id);
        if(!custom.empty()) return custom;
        auto it = this->appMap.find(id);
        return (it == this->appMap.end() || it->second.name.empty()) ? id : it->second.name;
    }

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
        // eigene Auswahl (Bildauswahl / SteamGridDB) hat Vorrang
        for(const char *k: { "grid", "hero", "logo" }) {
            const auto o = this->d->library.ArtOverride(id, k);
            if(o.empty() || !FileExists(o)) continue;
            if(k[0] == 'g') p.grid = o; else if(k[0] == 'h') p.hero = o; else p.logo = o;
        }
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
            const auto custom = lib.CustomName(id);
            e.name = !custom.empty() ? custom : it->second.name.empty() ? id : it->second.name;
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
            std::sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) { return Lower(this->DisplayName(a)) < Lower(this->DisplayName(b)); });
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
        Log("Bereich: %s", s.c_str());
        if(s == "settings") {
            this->CloseAll();
            this->OpenSettings();
            return;
        }
        this->CloseAll();
        this->section = s;
        if(this->d->settings.S("library.startSection") == "last" && JStr(*JGet(this->d->settings.data, "library"), "lastSection", "") != s) {
            JSet(this->d->settings.data, "library.lastSection", json(s));
            this->MarkDirty();
        }
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
#if defined(__cpp_exceptions)
        try {
            wantExit = this->FrameInner(in, dt);
        }
        catch(const std::exception &e) {
            this->CrashReset(e.what());
        }
        catch(...) {
            this->CrashReset("unbekannter Fehler");
        }
#else
        wantExit = this->FrameInner(in, dt);
#endif
        if((wantExit || this->exitRequested) && this->dirtySave) this->Save();
        return !wantExit && !this->exitRequested;
    }

    bool Home::FrameInner(const plat::Input &in, double dt) {
        bool wantExit = false;
        if(!this->host && (this->fatal || (!this->locked && this->dialogs.empty())) && (in.down & plat::BtnPlus)) wantExit = true;
        if(this->screen == Screen::Profile && !this->fatal) {
            if(this->pendingAutoSelect) {
                this->pendingAutoSelect = false;
                if(this->host && this->users.size() == 1) { this->host->SelectUser(this->users[0].id); return false; }
            }
            this->HandleInput(in);
            this->Update(dt);
            this->g->BeginFrame();
            this->DrawProfile();
            this->DrawHints();
            this->DrawToast();
            return false;
        }
        this->HandleInput(in);
        this->SyncApps();
        if(this->listDirty) this->BuildList();
        if(this->layoutDirty) this->Relayout();
        this->Update(dt);
        this->Draw();
        return wantExit;
    }

    void Home::CrashReset(const std::string &what) {
        Log("CrashReset (%d): %s", this->crashCount + 1, what.c_str());
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
        const bool any = in.down != 0 || in.tap;
        if(this->idleActive) {
            // erste Eingabe beendet nur den Bildschirmschoner
            if(any) this->UpdateIdle(0, true);
            return;
        }
        if(any) this->idleT = 0;
        if(this->crop.active) { this->CropInput(in); return; }
        if(this->logoMove.active) { this->LogoMoveInput(in); return; }
        if(in.tap) this->Tap(in.tapX, in.tapY);
        const uint32_t order[] = { plat::BtnUp, plat::BtnDown, plat::BtnLeft, plat::BtnRight, plat::BtnA, plat::BtnB, plat::BtnX, plat::BtnY, plat::BtnL, plat::BtnR, plat::BtnZL, plat::BtnZR, plat::BtnMinus, plat::BtnPlus };
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
        if(this->screen == Screen::Profile) { this->ProfilePress(b); return; }
        if(this->locked) {
            if(!this->unlocking && (b & (BtnA | BtnB | BtnX | BtnY))) { this->unlocking = true; this->unlockT = 0; }
            return;
        }
        if(b == BtnPlus) {
            if(this->host && this->dialogs.empty() && !this->moving) this->OpenQuickMenu();
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
        if(this->host) {
            const uint64_t tid = it->second.tid;
            const std::string id = e.id, name = e.name;
            if(this->host->SuspendedTid() == tid) { this->host->Resume(); return; }
            if(this->host->HasSuspended()) {
                this->Confirm("Laufendes Spiel schließen?", "Es ist bereits ein Spiel geöffnet. Nicht gespeicherter Fortschritt geht dabei verloren.", "Schließen und " + name + " starten", [this, tid, id, name]() {
                    this->host->CloseSuspended();
                    this->DoLaunch(tid, id, name);
                });
                return;
            }
            this->DoLaunch(tid, id, name);
            return;
        }
        // Spiele starten ist eine Funktion des HOME-Menüs (bei uLaunch: uSystem als System-Applet).
        // Aus der Test-App (läuft wie ein Spiel) würde das System NEXUS beim Wechsel abrupt beenden
        // -> Abstürze 2349-0004 / 2345-0008. Deshalb in der Test-App bewusst abgeschaltet.
        if(!CanLaunchGames) {
            Log("Start in Test-App blockiert: %s", e.id.c_str());
            this->Toast("Spiele starten geht erst, wenn NEXUS das HOME-Menü ist (über uLaunch). In der Test-App ist das abgeschaltet.");
            return;
        }
        this->d->library.MarkLaunched(e.id, static_cast<double>(std::time(nullptr)));
        this->Save();
        Log("Startanfrage %s (%s)", e.id.c_str(), e.name.c_str());
        const uint32_t rc = plat::LaunchApplication(it->second.tid);
        Log("Startanfrage Ergebnis 0x%X", rc);
        if(rc == 0) {
            // Das System wechselt jetzt zum Spiel und entzieht der App die Anzeige.
            // Weiterzeichnen würde zum Absturz 2349-0004 führen -> sofort sauber beenden.
            this->Toast("Starte " + e.name + " …");
            this->exitRequested = true;
        }
        else {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "Start aus der Test-App nicht möglich (0x%X). Starten klappt, sobald NEXUS das HOME-Menü ist.", rc);
            this->Toast(buf);
        }
    }

    void Home::Tap(float x, float y) {
        if(this->fatal) { this->Press(plat::BtnA); return; }
        if(this->screen == Screen::Profile) {
            for(const auto &h: this->rowHits) {
                if(x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) {
                    if(h.idx == this->userFocus) this->ProfilePress(plat::BtnA);
                    else this->userFocus = h.idx;
                    return;
                }
            }
            return;
        }
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
        const int at = (this->section == "start" && this->openFolder.empty()) ? this->focus + 1 : -1;
        auto make = [this, ids, at](const std::string &name) {
            if(name.empty()) return;
            const auto fid = this->d->library.CreateFolder(name, at);
            for(const auto &id: ids) this->d->library.AddToFolder(id, fid);
            this->listDirty = true;
            this->MarkDirty();
        };
        this->AskText("Name des neuen Ordners", "Neuer Ordner", 40, [this, make](std::string name) {
            if(name.empty()) {
                name = "Ordner " + std::to_string(this->d->library.folders.size() + 1);
                this->Toast("Ordner heißt „" + name + "“ (später umbenennbar).");
            }
            make(name);
        });
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
        dl->title = "Bilder wählen"; dl->w = 640; dl->x = 320; dl->y = 140; dl->shade = true;
        dl->sub = "Drei getrennte Ebenen: Cover nur auf der Kachel, Hintergrund bildschirmfüllend, Logo schwebend darüber.";
        dl->rows = [this, id]() {
            auto name = [](const std::string &p) { const auto k = p.find_last_of('/'); return p.empty() ? std::string("automatisch") : p.substr(k == std::string::npos ? 0 : k + 1); };
            const auto &a = this->Art(id);
            std::vector<Row> rows;
            rows.push_back(Row{ "Cover (Ebene 2, nur Kachel)", [a, name]() { return a.grid.empty() ? std::string("keins") : name(a.grid); }, nullptr, [this, id]() { this->OpenArtKind(id, "grid"); } });
            rows.push_back(Row{ "Hintergrund (Ebene 1)", [a, name]() { return a.hero.empty() ? std::string("keiner") : name(a.hero); }, nullptr, [this, id]() { this->OpenArtKind(id, "hero"); } });
            rows.push_back(Row{ "Logo (Ebene 3, transparent)", [a, name]() { return a.logo.empty() ? std::string("Schriftzug") : name(a.logo); }, nullptr, [this, id]() { this->OpenArtKind(id, "logo"); } });
            if(!a.grid.empty()) rows.push_back(Row{ "   Cover-Ausschnitt (Zoom, Schnitt)", nullptr, nullptr, [this, a]() { this->OpenCropEditor(a.grid, "Cover-Ausschnitt", this->TileAspect()); } });
            if(!a.hero.empty() && a.hero.rfind("mem:", 0) != 0) rows.push_back(Row{ "   Hintergrund-Ausschnitt (Zoom, Schnitt)", nullptr, nullptr, [this, a]() { this->OpenCropEditor(a.hero, "Hintergrund-Ausschnitt", ScreenW / ScreenH); } });
            rows.push_back(Row{ "Bei SteamGridDB suchen …", nullptr, nullptr, [this, id]() {
                std::string nm = id;
                for(const auto &e: this->list) if(e.id == id) nm = e.name;
                this->OpenSgdbFlow(id, nm, nm);
            } });
            rows.push_back(Row{ "Bilder neu einlesen", nullptr, nullptr, [this, id]() { this->ArtChanged(id); this->Toast("Bilder neu eingelesen."); } });
            return rows;
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
            rows.push_back(Row{ "Bilder wählen (Cover, Hintergrund, Logo)", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenArtInfo(e.id); } });
            rows.push_back(Row{ "SteamGridDB: Bilder suchen …", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenSgdbFlow(e.id, e.name, e.name); } });
            if(!e.folder && this->d->settings.B("ra.enabled")) rows.push_back(Row{ "RetroAchievements …", nullptr, nullptr, [this, e]() { this->Pop(); this->OpenRaDialog(e.id, e.name); } });
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
                    this->AskText("Neuer Ordnername", e.name, 40, [this, e](std::string name) {
                        if(auto *f = this->d->library.FindFolder(e.id); f && !name.empty()) { f->name = SanitizeUtf8(name.substr(0, 60)); this->listDirty = true; this->MarkDirty(); }
                    });
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
                rows.push_back(Row{ "Anzeigenamen ändern …", nullptr, nullptr, [this, e]() {
                    this->Pop();
                    this->AskText("Anzeigename für diesen Titel", e.name, 80, [this, e](std::string name) {
                        if(name.empty()) return;
                        this->d->library.SetCustomName(e.id, name);
                        this->listDirty = true; this->logoId.clear(); this->MarkDirty();
                        this->Toast("Angezeigt als „" + name + "“.");
                    });
                } });
                if(!this->d->library.CustomName(e.id).empty()) rows.push_back(Row{ "Originalnamen wiederherstellen", nullptr, nullptr, [this, e]() {
                    this->Pop();
                    this->d->library.SetCustomName(e.id, "");
                    this->listDirty = true; this->logoId.clear(); this->MarkDirty();
                    this->Toast("Originalname wiederhergestellt.");
                } });
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
        static const char *const unsupported[] = { "hero.blur", "lock.blur", "theme.font", "lock.clockFont", "hints.layerLabel" };
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
        if(p == "profile.preset") return s.S("profile.bgType") == "gradient";
        if(p == "profile.color") return s.S("profile.bgType") == "color";
        if(p == "topbar.showUser") return this->host != nullptr;
        if(p == "theme.textCustom") return s.S("theme.textColor") == "custom";
        if(p == "ui.panelColor") return s.S("theme.panelStyle") == "custom";
        if(p == "lock.textCustom") return s.S("lock.textColor") == "custom";
        if(p == "hero.maskSize") return s.S("hero.mask") != "none";
        if(p.rfind("aura.", 0) == 0 && p != "aura.enabled") {
            if(!s.B("aura.enabled")) return false;
            if(p == "aura.image") return s.S("aura.shape") == "custom";
            if(p == "aura.color") return s.S("aura.colorMode") == "fixed";
        }
        if(p.rfind("bgfx.", 0) == 0 && p != "bgfx.type") {
            const auto bt = s.S("bgfx.type");
            if(bt == "none") return false;
            if(p == "bgfx.image") return bt == "custom";
            if(p == "bgfx.color") return s.S("bgfx.colorMode") == "fixed";
        }
        if(p == "idle.minutes") return s.S("idle.mode") != "off";
        if(p == "idle.slideSeconds") return s.S("idle.mode") == "slideshow";
        {
            const bool fr = s.S("logo.placement") == "free";
            if(p == "logo.posX" || p == "logo.posY") return fr;
            if(p == "logo.anchorX" || p == "logo.anchorY" || p == "logo.offsetX" || p == "logo.offsetY" || p == "logo.safeArea") return !fr;
        }
        if(p == "home.image") return s.S("home.bgType") == "image";
        if(p == "lock.image") return s.S("lock.bgType") == "image";
        if(p == "profile.image") return s.S("profile.bgType") == "image";
        const auto am = s.S("theme.accentMode");
        if(p == "theme.accent2") return am == "gradient" || am == "gradient3";
        if(p == "theme.accent3") return am == "gradient3";
        if(p == "theme.accentAngle") return am != "solid";
        if(p == "theme.rainbowRepeat" || p == "theme.rainbowSat") return am == "rainbow";
        if(p == "theme.accentCycle") return s.S("theme.accentAnim") != "none";
        const auto rt = s.S("ra.topbar");
        if(p == "ra.pointsType") return rt == "points" || rt == "both";
        if(p == "ra.topbarIcon" || p == "ra.topbarName") return rt != "none";
        return true;
    }

    void Home::ApplyTheme(int idx) {
        json s = Settings::Defaults();
        const json keepLib = *JGet(this->d->settings.data, "library");
        const json *ra = JGet(this->d->settings.data, "ra");
        const json keepRa = ra ? *ra : json::object();
        const json *sg = JGet(this->d->settings.data, "sgdb");
        const json keepSg = sg ? *sg : json::object();
        const std::string keepKey = this->d->settings.S("sgdbKey");
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
        s["sgdb"] = keepSg;
        s["sgdbKey"] = keepKey;
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
                rows.push_back(Row{ "Eigene Themes (speichern, laden) …", nullptr, nullptr, [this]() { this->OpenThemes(); } });
                rows.push_back(Row{ "Vorlagen", nullptr, nullptr, nullptr, true });
                for(int i = 0; i < 5; i++) rows.push_back(Row{ std::string("Theme: ") + names[i], nullptr, nullptr, [this, i]() { this->ApplyTheme(i); } });
                if(this->host) {
                    rows.push_back(Row{ "Konsole", nullptr, nullptr, nullptr, true });
                    for(const auto &a: this->host->SystemActions()) {
                        const SysAction act = a;
                        rows.push_back(Row{ a.label, nullptr, nullptr, [this, act]() {
                            if(act.confirm.empty()) { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); }
                            else this->Confirm(act.label + "?", act.confirm, act.label, [this, act]() { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); });
                        } });
                    }
                }
                rows.push_back(Row{ "Sicherung", nullptr, nullptr, nullptr, true });
                rows.push_back(Row{ "Jetzt speichern", nullptr, nullptr, [this]() { this->Save(); this->Toast("Gespeichert: sdmc:/nexus/nexus.json"); } });
                rows.push_back(Row{ "Darstellung auf Standard (Bibliothek bleibt)", nullptr, nullptr, [this]() {
                    this->Confirm("Darstellung zurücksetzen?", "Ordner, Favoriten und Kachelanpassungen bleiben erhalten.", "Zurücksetzen", [this]() {
                        const json *ra = JGet(this->d->settings.data, "ra");
                        const json keepRa = ra ? *ra : json::object();
                        const std::string keepKey = this->d->settings.S("sgdbKey");
                        this->d->settings.data = Settings::Defaults();
                        this->d->settings.data["ra"] = keepRa;
                        this->d->settings.data["sgdbKey"] = keepKey;
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
                rows.push_back(Row{ "Version", []() { return std::string("NEXUS HOME 0.2"); }, nullptr, nullptr });
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
                    if(e.type == SType::Str) {
                        rows.push_back(Row{ e.label, [this, ep]() { const auto v = this->d->settings.S(ep->path); return v.empty() ? std::string("(leer, A = eingeben)") : v; }, nullptr, [this, ep, changed]() {
                            const std::string path = ep->path, label = ep->label;
                            this->AskText(label, this->d->settings.S(path), 120, [this, path, changed](std::string v) { this->d->settings.Set(path, json(v)); changed(); });
                        } });
                        continue;
                    }
                    if(e.type == SType::Image) {
                        rows.push_back(Row{ e.label, [this, ep]() {
                                const auto v = this->d->settings.S(ep->path);
                                const auto k = v.find_last_of('/');
                                return v.empty() ? std::string("kein Bild (A = wählen)") : v.substr(k == std::string::npos ? 0 : k + 1);
                            }, nullptr, [this, ep, changed]() {
                                const std::string path = ep->path;
                                this->OpenImagePicker(ep->label, { { "Eigene Hintergründe", std::string(ArtDir) + "/backgrounds" }, { "NEXUS-Bilder", ArtDir } }, true, false,
                                    [this, path, changed](const std::string &pick) {
                                        if(pick.empty()) JSet(this->d->settings.data, path, json(nullptr));
                                        else this->d->settings.Set(path, json(pick));
                                        changed();
                                        if(!pick.empty() && path.rfind("aura.", 0) != 0 && path.rfind("bgfx.", 0) != 0) this->OpenCropEditor(pick, "Bildausschnitt", ScreenW / ScreenH);
                                    }, "Kein Bild");
                            } });
                        if(!this->d->settings.S(e.path).empty() && e.path.rfind("aura.", 0) != 0 && e.path.rfind("bgfx.", 0) != 0) {
                            rows.push_back(Row{ "   Bildausschnitt (Zoom, Schnitt)", [this, ep]() {
                                    const Crop c = this->d->library.CropOf(this->d->settings.S(ep->path));
                                    return c.set ? std::to_string(static_cast<int>(std::lround(c.zoom * 100))) + " %" : std::string("ganzes Bild");
                                }, nullptr, [this, ep]() { this->OpenCropEditor(this->d->settings.S(ep->path), "Bildausschnitt", ScreenW / ScreenH); } });
                        }
                        continue;
                    }
                    rows.push_back(Row{ e.label, [this, ep]() { return this->d->settings.Display(*ep); },
                        [this, ep, changed](int dir) { this->d->settings.Step(*ep, dir); changed(); }, nullptr });
                }
                if(gid == "logo") rows.insert(rows.begin(), Row{ "Logo frei verschieben (Stick) …", nullptr, nullptr, [this]() { this->StartLogoMove(); } });
                if(gid == "lib") for(auto &rw: this->SgdbRows()) rows.push_back(rw);
                if(gid == "ra") for(auto &rw: this->RaRows()) rows.push_back(rw);
                if(gid == "lock") rows.push_back(Row{ "Sperrbildschirm jetzt anzeigen (−)", nullptr, nullptr, [this]() { this->CloseAll(); this->locked = true; this->unlocking = false; } });
                if(gid == "profile" && this->host) rows.push_back(Row{ "Profil wechseln (zur Profilauswahl)", nullptr, nullptr, [this]() { this->CloseAll(); this->Save(); this->host->Logoff(); } });
                if(gid == "lib") rows.push_back(Row{ "Bilder neu einlesen", nullptr, nullptr, [this]() { this->art.clear(); this->img->Clear(); this->heroA.key.clear(); this->heroFor.clear(); this->logoId.clear(); this->Toast("Bilder werden neu geladen."); } });
                return rows;
            };
        }
        this->Push(dl);
    }

    // ------------------------------------------------------------------ Animation

    void Home::Update(double dt) {
        this->time += dt;
        this->accentP = AccentFrom(this->d->settings);
        this->g->fontScale = this->d->settings.N("ui.fontScale");
        this->g->paint = this->AccentFn(0, 0, ScreenW, ScreenH);
        this->online.Tick();
        {
            const auto &st = this->d->settings;
            this->auraC = AuraFrom(st);
            this->bgC = BgFrom(st);
            const bool fxOn = st.B("anim.enabled") && this->screen == Screen::Main && !this->locked && !this->idleActive;
            const AccentParams ap = this->accentP;
            const double tt = this->time;
            auto acc = [ap, tt](double x, double y) { return AccentAt(ap, AccentU(ap, x, y, ScreenW, ScreenH), tt); };
            if(fxOn) {
                this->fx.SpawnAura(this->auraC, this->auraRects, dt, this->time, acc);
                this->fx.SpawnBg(this->bgC, dt, this->time, ScreenW, ScreenH, acc);
            }
            this->fx.Update(dt, this->time);
            if(!this->auraC.enabled) this->fx.parts.erase(std::remove_if(this->fx.parts.begin(), this->fx.parts.end(), [](const Particle &p) { return p.layer < 2; }), this->fx.parts.end());
            if(this->bgC.type == "none") this->fx.parts.erase(std::remove_if(this->fx.parts.begin(), this->fx.parts.end(), [](const Particle &p) { return p.layer >= 2; }), this->fx.parts.end());
            this->UpdateIdle(dt, false);
            if(!this->startApplied && this->appsVersion > 0) this->ApplyStartSection();
            if(!this->gcDone && this->appsVersion > 0 && !this->apps->Busy() && this->time > 15) {
                this->gcDone = true;
                // alle Bilder, die in Benutzung sind: eigene Auswahl, Standarddateien der Titel, Hintergründe
                std::vector<std::string> use = this->d->library.AllArtOverrides();
                for(const auto &id: this->installed) {
                    const std::string b = std::string(ArtDir) + "/" + id + "/";
                    for(const char *n: { "grid.jpg", "grid.png", "hero.jpg", "hero.png", "logo.png", "logo.jpg" }) use.push_back(b + n);
                }
                for(const auto &f: this->d->library.folders) {
                    const std::string b = std::string(ArtDir) + "/" + f.id + "/";
                    for(const char *n: { "grid.jpg", "grid.png", "hero.jpg", "hero.png", "logo.png", "logo.jpg" }) use.push_back(b + n);
                }
                for(const char *p: { "home.image", "lock.image", "profile.image", "aura.image", "bgfx.image" }) { const auto v = st.S(p); if(!v.empty()) use.push_back(v); }
                this->img->CollectGarbage(use);
            }
        }
        if(!this->autoArtQueued && this->appsVersion > 0 && !this->apps->Busy()) {
            this->autoArtQueued = true;
            if(this->d->settings.B("sgdb.autoMissing")) this->QueueMissingArt(false);
        }
        this->frameDt = std::clamp(dt, 0.0, 0.1);
        this->wobbleT += dt;
        if(this->toastT > 0) this->toastT -= dt;
        if(this->dirtySave) {
            this->saveTimer -= dt;
            if(this->saveTimer <= 0) this->Save();
        }
        if(this->unlocking) {
            this->unlockT += dt;
            if(this->unlockT > 0.5) {
                this->locked = false;
                this->unlocking = false;
                if(this->host && this->screen == Screen::Lock) this->host->OnUnlock();
            }
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
        this->DrawParticles(2);
        this->DrawLogo(dt);
        this->DrawTiles();
        this->DrawTopbar();
        this->DrawHints();
        this->DrawParticles(3);
        if(this->idleActive) this->DrawIdle();
        this->DrawDialogs();
        if(this->crop.active) this->DrawCropEditor();
        if(this->logoMove.active) {
            this->g->RoundRect(ScreenW / 2 - 470, ScreenH - 64, 940, 40, 20, Col{ 0, 0, 0, 200 });
            this->g->Text("Logo verschieben: Stick/Steuerkreuz   ·   R/L = größer/kleiner   ·   Y = Mitte   ·   A = speichern   ·   B = abbrechen", 14, ScreenW / 2, ScreenH - 54, Col{ 238, 242, 247, 255 }, 1, 920);
        }
        if(this->locked) this->DrawLock();
        this->DrawToast();
    }

    void Home::DrawBackground() {
        const auto &s = this->d->settings;
        const auto type = s.S("home.bgType");
        if(type == "color") this->g->Rect(0, 0, ScreenW, ScreenH, ParseCol(s.S("home.color")));
        else if(type == "image" && this->DrawBgImage(s.S("home.image"), 0, 1.0)) {}
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
            this->PrefetchAround();
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
        const bool animOn = s.B("anim.enabled");
        const auto tr = animOn ? s.S("hero.transition") : std::string("cut");
        const double ms = s.N("hero.transitionMs") / 1000.0;
        const double step = (ms <= 0 || tr == "cut") ? 1.0 : dt / ms;
        const bool bVisible = this->heroB.alpha > 0 && (!this->heroB.key.empty() || !this->heroB.folderAccent.empty());
        if(tr == "viaHome" && bVisible) {
            // erst bis zum Home-Hintergrund ausblenden, dann das neue Bild einblenden
            this->heroB.alpha = std::max(0.0, this->heroB.alpha - step);
        }
        else {
            this->heroA.alpha = std::min(1.0, this->heroA.alpha + step);
            this->heroB.alpha = std::max(0.0, this->heroB.alpha - step);
        }
        double px = 0;
        if(s.B("hero.parallax") && this->focus < static_cast<int>(this->lay.items.size())) {
            const auto &it = this->lay.items[this->focus];
            px = -((it.x + it.w / 2) - 640) / 40.0;
        }
        const double kb = s.B("hero.kenBurns") ? 1.0 + 0.06 * (1 + std::sin(this->time * 2 * kPi / 36.0)) : 1.0;
        const double maxOp = s.N("hero.opacity");
        const auto mask = s.S("hero.mask");
        const double msz = std::max(0.05, s.N("hero.maskSize"));
        std::function<double(double, double)> fade;
        if(mask == "bottom") fade = [msz](double, double v) { return (1 - v) / msz; };
        else if(mask == "top") fade = [msz](double, double v) { return v / msz; };
        else if(mask == "left") fade = [msz](double u, double) { return u / msz; };
        else if(mask == "right") fade = [msz](double u, double) { return (1 - u) / msz; };
        else if(mask == "vignette") fade = [msz](double u, double v) {
            const double r = std::sqrt((u - 0.5) * (u - 0.5) * 4 + (v - 0.5) * (v - 0.5) * 4) / std::sqrt(2.0);
            return (1 - r) / msz;
        };
        for(HeroSlot *slot: { &this->heroB, &this->heroA }) {
            if(slot->alpha <= 0.01) continue;
            const double e = EaseOut(slot->alpha);
            const bool entering = slot == &this->heroA && slot->alpha < 1;
            const double slideX = (entering && tr == "slide") ? 80 * (1 - e) : 0;
            const double zoomIn = (entering && tr == "zoom") ? 1 + 0.08 * (1 - e) : 1;
            const double op = slot->alpha * maxOp;
            if(!slot->key.empty()) {
                SDL_Texture *t = this->img->Get(slot->key, 0);
                if(t) {
                    // Ausschnitt: eigener Zuschnitt des Bildes, sonst die globalen Werte
                    const Crop c = this->d->library.CropOf(slot->key);
                    const double z = (c.set ? c.zoom : s.N("hero.imageScale")) * kb * zoomIn;
                    const double cx = c.set ? c.x : s.N("hero.posX"), cy = c.set ? c.y : s.N("hero.posY");
                    this->g->ImageEx(t, -30 + px + slideX, -30, ScreenW + 60, ScreenH + 60, op, z, cx, cy, fade);
                    if(slot->icon) this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(110 * op) });
                }
            }
            else if(!slot->folderAccent.empty()) {
                this->g->GradV(0, 0, ScreenW, ScreenH, WithA(ParseCol(slot->folderAccent), 0.55 * op), WithA(this->UiCol(), op));
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
        const bool freePos = s.S("logo.placement") == "free";
        const bool safe = s.B("logo.safeArea") && !freePos;
        double bx0 = 0, bx1 = ScreenW, by0 = env.topH, by1 = ScreenH - env.botH;
        if(freePos) { by0 = 0; by1 = ScreenH; }
        if(safe) {
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
        if(safe && (w > bw - 20 || h > bh)) {
            const double k = std::max(0.1, std::min((bw - 20) / w, bh / h));
            w *= k; h *= k;
        }
        const auto ax = s.S("logo.anchorX"), ay = s.S("logo.anchorY");
        double x = ax == "left" ? bx0 + 50 : ax == "center" ? (bx0 + bx1 - w) / 2 : bx1 - w - 50;
        double y = ay == "top" ? by0 + 16 : ay == "middle" ? (by0 + by1 - h) / 2 : by1 - h;
        x += s.N("logo.offsetX");
        y += s.N("logo.offsetY");
        if(freePos) {
            // frei: Mittelpunkt in Prozent des Bildschirms
            x = ScreenW * s.N("logo.posX") / 100.0 - w / 2;
            y = ScreenH * s.N("logo.posY") / 100.0 - h / 2;
        }
        else if(safe) {
            x = std::clamp(x, bx0, std::max(bx0, bx1 - w));
            y = std::clamp(y, by0, std::max(by0, by1 - h));
        }
        if(s.B("logo.float") && s.B("anim.enabled") && !s.B("anim.reduce")) y += std::sin(this->time * 2 * kPi / 5.0) * 8;
        x += this->logoSlide;
        const double alpha = this->logoAlpha * s.N("logo.opacity");
        if(s.B("logo.shadow")) this->g->Image(tex, x + 3, y + 6, w, h, alpha * 0.5, true, Col{ 0, 0, 0, 255 });
        this->g->Image(tex, x, y, w, h, alpha, true);
        if(!e.folder) this->DrawRaUnderLogo(e, x, std::min(y + h + 10, by1 - 40), w, alpha);
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
        this->DrawParticles(0);
        this->auraRects.clear();
        const double dimOthers = s.N("anim.dimOthers");
        const bool titleOnFocus = s.B("anim.titleOnFocus");
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
                double posX = JNum(t, "posX", 50), posY = JNum(t, "posY", 50), zoom = 1;
                SDL_Texture *tex = nullptr;
                if(!a.grid.empty()) {
                    tex = this->img->Get(a.grid, 1);
                    const Crop c = this->d->library.CropOf(a.grid);   // eigener Bildausschnitt hat Vorrang
                    if(c.set) { posX = c.x; posY = c.y; zoom = c.zoom; }
                }
                else if(!e.folder && !e.iconKey.empty()) tex = this->img->Get(e.iconKey, 2);
                if(tex) this->g->ShapeTex(tex, xf, rad, hex, op, cover, posX, posY, Col{}, zoom);
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
                if((JBool(t, "showTitle", false) || (foc && titleOnFocus)) && std::abs(xf.rot) < 0.5 && ds.w > 60) {
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
                if(!e.folder && std::abs(xf.rot) < 0.5) this->DrawRaBadge(e, xf, ds.w * xf.sc * xf.squash, ds.h * xf.sc, op);
            }
            if(this->host && !e.folder && std::abs(xf.rot) < 0.5) {
                auto ai = this->appMap.find(e.id);
                if(ai != this->appMap.end() && this->host->SuspendedTid() == ai->second.tid) {
                    const double bx = xf.cx - ds.w * xf.sc * xf.squash / 2 + 8, by = xf.cy + ds.h * xf.sc / 2 - 30;
                    const int tw = this->g->TextW("Pausiert", 12, true);
                    this->g->RoundRect(bx, by, tw + 16, 22, 11, WithA(this->Accent(), op));
                    this->g->Text("Pausiert", 12, bx + 8, by + 3, Col{ 0, 0, 0, static_cast<uint8_t>(255 * op) }, 0, 0, true);
                }
            }
            if(!foc && dimOthers > 0) this->g->Shape(xf, rad, hex, Col{ 0, 0, 0, static_cast<uint8_t>(255 * dimOthers * op) });
            if(this->auraC.enabled && op > 0.3) {
                const auto &tg = this->auraC.target;
                if((tg == "focus" && foc) || tg == "all" || (tg == "fav" && !e.folder && this->d->library.IsFav(e.id)))
                    this->auraRects.push_back(EmitRect{ xf.cx, xf.cy, ds.w * xf.sc * xf.squash, ds.h * xf.sc });
            }
            if(foc) { focusXf = xf; focusRad = rad; focusHex = hex; haveFocus = true; }
        }
        // Ebene 5: Fokusmarkierung (eigene Ebene, nicht Teil der Kachel)
        if(haveFocus) {
            const double fw = focusXf.w * focusXf.sc * focusXf.squash, fh = focusXf.h * focusXf.sc;
            const double pad = 30;
            ColFn fn = this->AccentFn(focusXf.cx - fw / 2 - pad, focusXf.cy - fh / 2 - pad, fw + 2 * pad, fh + 2 * pad);
            if(this->moving) fn = [](float, float) { return Col{ 255, 212, 71, 255 }; };
            double pulse = 1.0;
            if(fx == "pulse" && s.B("anim.enabled")) pulse = 0.55 + 0.45 * std::sin(this->time * 4.5);
            const double rw = s.N("anim.ringWidth");
            if(rw > 0 || this->moving) {
                const int layers = fx == "glow" ? 5 : 3;
                for(int k = layers; k >= 1; k--) this->g->ShapeRingFn(focusXf, focusRad, focusHex, rw + k * (fx == "glow" ? 5 : 3), fn, 0.12 * pulse / k * (fx == "glow" ? 1.6 : 1));
                this->g->ShapeRingFn(focusXf, focusRad, focusHex, std::max(rw, 2.0), fn, pulse);
            }
        }
        this->DrawParticles(1);
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
            const bool sec = s.B("topbar.seconds");
            if(s.B("topbar.clock24")) std::strftime(buf, sizeof(buf), sec ? "%H:%M:%S" : "%H:%M", lt);
            else std::strftime(buf, sizeof(buf), sec ? "%I:%M:%S %p" : "%I:%M %p", lt);
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
        // RetroAchievements: Punkte / Anzahl
        if(s.B("ra.enabled") && s.S("ra.topbar") != "none" && this->online.ra.profile.ok) {
            const auto mode = s.S("ra.topbar");
            std::string t;
            auto fmt = [](long long v) {
                std::string n = std::to_string(v), o;
                for(size_t i = 0; i < n.size(); i++) { if(i && (n.size() - i) % 3 == 0) o += '.'; o += n[i]; }
                return o;
            };
            if(s.B("ra.topbarName") && !this->online.ra.profile.user.empty()) t = this->online.ra.profile.user;
            if(mode == "points" || mode == "both") t += (t.empty() ? "" : " · ") + fmt(this->online.RaPoints()) + " Pkt.";
            if(mode == "count" || mode == "both") t += (t.empty() ? "" : " · ") + fmt(this->online.RaTrophies()) + " Pokale";
            rx -= this->g->TextW(t, 16);
            this->g->Text(t, 16, rx, 18, tc);
            if(s.B("ra.topbarIcon")) { rx -= isz + 6; this->g->Icon("trophy", rx + isz / 2, 28, isz, Col{ 255, 212, 71, 255 }, filled); }
            rx -= 18;
        }
        // Profil (nur in uLaunch)
        double userW = 0;
        if(this->host && s.B("topbar.showUser") && !this->currentUser.name.empty()) {
            const double us = 28 * s.N("icons.size");
            SDL_Texture *ut = this->currentUser.iconKey.empty() ? nullptr : this->img->Get(this->currentUser.iconKey, 0);
            Xf ux; ux.cx = 40 + us / 2; ux.cy = 29; ux.w = ux.h = us;
            if(ut) this->g->ShapeTex(ut, ux, us / 2, false, 1.0, true, 50, 50);
            else this->g->Circle(ux.cx, ux.cy, us / 2, this->Accent());
            const int nw = this->g->Text(this->currentUser.name, 15, 40 + us + 8, 19, tc);
            userW = us + 8 + nw + 18;
        }
        if(s.B("topbar.greeting")) {
            const int hr = lt->tm_hour;
            std::string gr = hr < 5 ? "Gute Nacht" : hr < 11 ? "Guten Morgen" : hr < 17 ? "Guten Tag" : hr < 22 ? "Guten Abend" : "Gute Nacht";
            if(!this->currentUser.name.empty()) gr += ", " + this->currentUser.name;
            const int gw = this->g->Text(gr, 15, 40 + userW, 19, mc);
            userW += gw + 18;
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
        double x = align == "center" ? (ScreenW - total) / 2 : align == "right" ? rx - total - 10 : 40 + userW;
        for(size_t i = 0; i < vis.size(); i++) {
            const bool active = vis[i] == this->section && this->openFolder.empty();
            const double w = widths[i];
            if(active) {
                this->g->RoundRect(x, 12, w, 34, 17, Col{ 127, 127, 127, 56 });
                this->g->RectFn(x + 14, 43, w - 28, 2.5, this->AccentFn(x + 14, 43, w - 28, 2.5), 1.0);
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
            const bool susp = !folder && !this->list.empty() && this->host && this->appMap.count(this->list[this->focus].id) && this->host->SuspendedTid() == this->appMap[this->list[this->focus].id].tid;
            h = { { "A", folder ? "Öffnen" : susp ? "Fortsetzen" : ((this->host || CanLaunchGames) ? "Starten" : "Starten (später)") } };
            if(!this->openFolder.empty()) h.push_back({ "B", "Zurück" });
            h.push_back({ "X", "Optionen" });
            h.push_back({ "Y", "Favorit" });
            h.push_back({ "L/R", "Bereich" });
            h.push_back({ "−", "Sperren" });
            h.push_back({ "+", this->host ? "Menü" : "Beenden" });
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
        const double po = s.N("ui.panelOpacity");
        const Col panel = ps == "solid" ? this->UiCol() : ps == "outline" ? Col{ 0, 0, 0, static_cast<uint8_t>(90 * po) } : ps == "custom" ? WithA(ParseCol(s.S("ui.panelColor")), po) : WithA(light ? Col{ 246, 248, 251, 255 } : Col{ 14, 18, 28, 255 }, po);
        const Col tc = this->TextCol(), mc = this->MutedCol(), acc = this->Accent();
        const double prad = s.N("ui.panelRadius"), rowH = s.N("ui.rowHeight"), headRowH = std::max(24.0, rowH * 0.8);
        const int titleSz = static_cast<int>(s.N("ui.titleSize")), textSz = static_cast<int>(s.N("ui.textSize")), valSz = std::max(11, textSz - 2), subSz = std::max(11, textSz - 2);
        const double border = s.N("ui.panelBorder") + (ps == "outline" ? 2 : 0);
        for(size_t di = 0; di < this->dialogs.size(); di++) {
            auto &dl = *this->dialogs[di];
            const bool top = di + 1 == this->dialogs.size();
            if(dl.shade) this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, 110 });
            auto rows = dl.rows();
            if(top && dl.onSelect) dl.onSelect(dl.sel);
            const double lw = dl.preview ? std::min(dl.w * 0.46, 430.0) : dl.w;
            const double titleH = titleSz * 1.35 * this->g->fontScale + 18;
            const double subLine = subSz * 1.5 * this->g->fontScale;
            const double headH = titleH + (dl.sub.empty() ? 0 : subLine * (1 + static_cast<int>(this->g->TextW(dl.sub, subSz) / (dl.w - 40))));
            double contentH = 0;
            for(const auto &rw: rows) contentH += rw.head ? headRowH : rowH;
            const double maxH = ScreenH - dl.y - this->Env().botH - 6;
            const double h = dl.h > 0 ? dl.h : std::min(maxH, headH + contentH + 14);
            this->g->RoundRect(dl.x, dl.y, dl.w, h, prad, panel);
            if(border > 0) { Xf ox; ox.cx = dl.x + dl.w / 2; ox.cy = dl.y + h / 2; ox.w = dl.w; ox.h = h; this->g->ShapeRingFn(ox, prad, false, border, this->AccentFn(dl.x, dl.y, dl.w, h), 1.0); }
            this->g->Text(dl.title, titleSz, dl.x + 20, dl.y + 14, tc, 0, static_cast<int>(dl.w - 40), true);
            if(!dl.sub.empty()) {
                // einfacher Zeilenumbruch
                std::string line, word, rest = dl.sub + " ";
                double yy = dl.y + titleH;
                for(char c: rest) {
                    if(c == ' ') {
                        const std::string cand = line.empty() ? word : line + " " + word;
                        if(this->g->TextW(cand, subSz) > dl.w - 40 && !line.empty()) { this->g->Text(line, subSz, dl.x + 20, yy, mc); yy += subLine; line = word; }
                        else line = cand;
                        word.clear();
                    }
                    else word += c;
                }
                if(!line.empty()) this->g->Text(line, subSz, dl.x + 20, yy, mc);
            }
            // Scrollen, damit die Auswahl sichtbar ist
            const double areaY = dl.y + headH, areaH = h - headH - 10;
            double selTop = 0, y = 0;
            for(int i = 0; i < static_cast<int>(rows.size()); i++) { if(i == dl.sel) selTop = y; y += rows[i].head ? headRowH : rowH; }
            if(selTop - dl.scroll < 0) dl.scroll = selTop;
            if(selTop + rowH - dl.scroll > areaH) dl.scroll = selTop + rowH - areaH;
            dl.scroll = std::clamp(dl.scroll, 0.0, std::max(0.0, contentH - areaH));
            y = areaY - dl.scroll;
            for(int i = 0; i < static_cast<int>(rows.size()); i++) {
                const auto &rw = rows[i];
                const double rh = rw.head ? headRowH : rowH;
                if(y + rh > areaY - 1 && y < areaY + areaH - 2) {
                    if(rw.head) this->g->Text(rw.label, std::max(11, textSz - 3), dl.x + 22, y + rh - (textSz - 3) * 1.4 * this->g->fontScale - 2, acc, 0, 0, true);
                    else {
                        const bool sel = i == dl.sel && top;
                        if(sel) {
                            this->g->RoundRect(dl.x + 10, y, lw - 20, rh - 2, 8, light ? Col{ 0, 0, 0, 20 } : Col{ 255, 255, 255, 26 });
                            this->g->RectFn(dl.x + 10, y + 4, 3, rh - 10, this->AccentFn(dl.x, y, lw, rh), 1.0, 4);
                        }
                        const std::string val = rw.value ? rw.value() : std::string();
                        const int vw = val.empty() ? 0 : std::min(this->g->TextW(val, valSz), static_cast<int>(lw * 0.5));
                        const double ty = y + (rh - textSz * 1.3 * this->g->fontScale) / 2, vy = y + (rh - valSz * 1.3 * this->g->fontScale) / 2;
                        this->g->Text(rw.label, textSz, dl.x + 22, ty, tc, 0, static_cast<int>(lw - 60 - vw));
                        if(!val.empty()) {
                            const bool adj = static_cast<bool>(rw.adjust);
                            const double vx = dl.x + lw - 30 - (adj && sel ? 14 : 0);
                            this->g->Text(val, valSz, vx, vy, mc, 2, static_cast<int>(lw * 0.5));
                            if(adj && sel) {
                                this->g->Text("<", valSz + 1, vx - vw - 16, vy, mc, 0, 0, true);
                                this->g->Text(">", valSz + 1, dl.x + lw - 34, vy, mc, 0, 0, true);
                            }
                        }
                        else if(rw.activate) this->g->Text(">", valSz + 1, dl.x + lw - 34, vy, mc, 0, 0, true);
                        if(top) this->rowHits.push_back(Hit{ dl.x + 10, y, lw - 20, rh, "", i });
                    }
                }
                y += rh;
            }
            if(dl.preview && dl.sel >= 0 && dl.sel < static_cast<int>(rows.size())) {
                // großes Vorschaubild der Auswahl
                const double px = dl.x + lw + 4, py = areaY, pw = dl.w - lw - 24, ph = areaH - 4;
                const auto &rw = rows[dl.sel];
                if(rw.dark) for(int cy = 0; cy < 8; cy++) for(int cx = 0; cx < 8; cx++) this->g->Rect(px + cx * pw / 8, py + cy * ph / 8, pw / 8 + 1, ph / 8 + 1, (cx + cy) % 2 ? Col{ 40, 44, 52, 255 } : Col{ 28, 31, 38, 255 });
                else this->g->RoundRect(px, py, pw, ph, 10, Col{ 0, 0, 0, 70 });
                if(!rw.thumb.empty()) {
                    const bool exists = rw.thumb.rfind("mem:", 0) == 0 || FileExists(rw.thumb);
                    SDL_Texture *t = exists ? this->img->Get(rw.thumb, 0) : nullptr;
                    if(t) this->g->Image(t, px + 8, py + 8, pw - 16, ph - 16, 1.0, false);
                    else this->g->Text(exists && this->img->Failed(rw.thumb) ? "Bild nicht lesbar" : "Vorschau wird geladen …", 15, px + pw / 2, py + ph / 2 - 10, mc, 1);
                }
                else this->g->Text("Keine Vorschau", 15, px + pw / 2, py + ph / 2 - 10, mc, 1);
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
        // Bewusst ohne Spielbild: der Sperrbildschirm zeigt nur eigene Hintergründe
        if(type == "image" && this->DrawBgImage(s.S("lock.image"), oy, alpha)) {}
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
        const auto ltc = s.S("lock.textColor");
        const Col col = WithA(ltc == "black" ? Col{ 17, 17, 17, 255 } : ltc == "custom" ? ParseCol(s.S("lock.textCustom")) : Col{ 255, 255, 255, 255 }, alpha);
        const std::time_t now = std::time(nullptr);
        const std::tm *lt = std::localtime(&now);
        char clk[16], date[64];
        std::strftime(clk, sizeof(clk), s.B("lock.clock24") ? (s.B("lock.seconds") ? "%H:%M:%S" : "%H:%M") : (s.B("lock.seconds") ? "%I:%M:%S" : "%I:%M"), lt);
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
        if(s.B("lock.clockAccent")) {
            const int tw = this->g->TextW(clk, size);
            const double lx = align == 1 ? x - tw / 2.0 : align == 2 ? x - tw : x;
            this->g->TextFn(clk, size, x, y + oy, this->AccentFn(lx, y + oy, tw, size * 1.2), alpha, align);
        }
        else this->g->Text(clk, size, x, y + oy, col, align);
        if(s.B("lock.showDate")) this->g->Text(date, 24, x, y + oy + size * 1.15, col, align);
        const auto msg = s.S("lock.message");
        if(!msg.empty()) this->g->Text(msg, 20, x, y + oy + size * 1.15 + (s.B("lock.showDate") ? 40 : 0), WithA(col, 0.9), align, 1000);
        if(s.B("lock.showBattery")) {
            const int pct = plat::BatteryPercent();
            if(pct >= 0) {
                const std::string ps = std::to_string(pct) + " %";
                this->g->Text(ps, 16, ScreenW - 40, 26 + oy, col, 2);
                this->g->Battery(ScreenW - 80 - this->g->TextW(ps, 16), 28 + oy, 20, pct, plat::BatteryCharging(), col);
            }
        }
        if(s.B("ra.enabled") && s.B("ra.lockRecent") && !this->online.ra.recent.empty()) {
            const auto &rr = this->online.ra.recent.front();
            const std::string t = "Zuletzt freigeschaltet: „" + rr.title + "“ in " + rr.game;
            const int tw = std::min(1000, this->g->TextW(t, 16));
            this->g->Icon("trophy", ScreenW / 2 - tw / 2 - 16, ScreenH - 102 + oy, 18, WithA(Col{ 255, 212, 71, 255 }, alpha), true);
            this->g->Text(t, 16, ScreenW / 2 + 6, ScreenH - 112 + oy, WithA(col, 0.9), 1, 1000);
        }
        if(s.B("lock.hint")) {
            const double pa = 0.55 + 0.45 * std::sin(this->time * 2.8);
            this->g->Text("Zum Entsperren A drücken oder tippen", 16, ScreenW / 2, ScreenH - 70 + oy, WithA(col, pa), 1);
        }
    }

    void Home::DrawToast() {
        if(this->toastT <= 0 || this->toastText.empty()) return;
        const double a = std::min(1.0, this->toastT / 0.3);
        const auto &s = this->d->settings;
        const int w = std::min(1100, this->g->TextW(this->toastText, 14) + 36);
        const double th = 22 * this->g->fontScale + 16;
        const auto pos = s.S("ui.toastPos");
        const double x = (ScreenW - w) / 2, y = pos == "top" ? 70 : pos == "center" ? ScreenH / 2 - th / 2 : ScreenH - 92;
        const Col base = s.S("theme.panelStyle") == "custom" ? ParseCol(s.S("ui.panelColor")) : (this->Light() ? Col{ 246, 248, 251, 245 } : Col{ 30, 38, 54, 245 });
        this->g->RoundRect(x, y, w, th, std::min(s.N("ui.panelRadius"), th / 2), WithA(base, a));
        this->g->Text(this->toastText, 14, ScreenW / 2, y + 8, WithA(this->TextCol(), a), 1, w - 24);
    }

    void Home::DrawFatal() {
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 5, 7, 11, 255 });
        this->g->Text("NEXUS konnte die Oberfläche nicht darstellen.", 24, ScreenW / 2, 280, Col{ 238, 242, 247, 255 }, 1);
        this->g->Text(this->fatalMsg, 14, ScreenW / 2, 324, Col{ 147, 160, 180, 255 }, 1, 1000);
        this->g->Text("A = Darstellung zurücksetzen (Bibliothek bleibt erhalten)   ·   + = Beenden", 16, ScreenW / 2, 380, Col{ 62, 224, 200, 255 }, 1);
    }


    // ------------------------------------------------------------------ uLaunch-Anbindung (Host)

    void Home::SetScreen(Screen s) {
        Log("Bildschirm: %d", static_cast<int>(s));
        this->screen = s;
        this->dialogs.clear();
        if(s == Screen::Lock) {
            this->locked = true;
            this->unlocking = false;
        }
        else if(s == Screen::Profile) {
            this->locked = false;
            this->users = this->host ? this->host->Users() : std::vector<UserInfo>{};
            this->userFocus = 0;
            this->profileT = 0;
            this->pendingAutoSelect = this->d->settings.B("profile.autoSingle") && this->users.size() == 1;
        }
        else {
            this->locked = false;
            if(this->host) this->currentUser = this->host->CurrentUser();
            this->entryAnim = true;
            this->listDirty = true;
        }
    }

    void Home::DrawOnly() {
        // Wird während blockierender Übergänge von uMenu aufgerufen: keine Eingabe, keine Zustandswechsel
        if(this->fatal) { this->DrawFatal(); return; }
        if(this->screen == Screen::Profile) { this->g->BeginFrame(); this->DrawProfile(); return; }
        if(this->listDirty) this->BuildList();
        if(this->layoutDirty) this->Relayout();
        this->Draw();
    }

    void Home::OnHome() {
        if(this->screen == Screen::Lock || this->locked) {
            if(!this->unlocking) { this->unlocking = true; this->unlockT = 0; }
            return;
        }
        if(this->screen == Screen::Profile) return;
        if(!this->dialogs.empty()) { this->CloseAll(); return; }
        if(this->moving) { this->Press(plat::BtnB); return; }
        if(this->host && this->host->HasSuspended()) { this->host->Resume(); return; }
        if(!this->openFolder.empty() || this->section != "start") { this->SwitchSection("start"); return; }
        this->SetFocus(0);
    }

    void Home::DoLaunch(uint64_t tid, const std::string &id, const std::string &name) {
        this->d->library.MarkLaunched(id, static_cast<double>(std::time(nullptr)));
        this->Save();
        Log("Start %s (%s)", id.c_str(), name.c_str());
        if(this->host) this->host->BeforeLeave();
        const uint32_t rc = this->host ? this->host->Launch(tid) : plat::LaunchApplication(tid);
        Log("Start Ergebnis 0x%X", rc);
        if(rc != 0) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s konnte nicht gestartet werden (Fehler 0x%X).", name.c_str(), rc);
            this->Toast(buf);
        }
    }

    void Home::OpenQuickMenu() {
        auto dl = std::make_shared<Dialog>();
        dl->title = "Menü";
        dl->sub = this->currentUser.name.empty() ? "" : "Angemeldet: " + this->currentUser.name;
        dl->w = 400; dl->x = ScreenW - 420; dl->y = this->Env().topH + 4; dl->shade = true;
        dl->rows = [this]() {
            std::vector<Row> rows;
            if(this->host && this->host->HasSuspended()) {
                rows.push_back(Row{ "Laufendes Spiel fortsetzen", nullptr, nullptr, [this]() { this->CloseAll(); this->host->Resume(); } });
                rows.push_back(Row{ "Laufendes Spiel schließen", nullptr, nullptr, [this]() {
                    this->Confirm("Spiel schließen?", "Nicht gespeicherter Fortschritt geht verloren.", "Schließen", [this]() { this->host->CloseSuspended(); this->Toast("Spiel geschlossen."); });
                } });
            }
            rows.push_back(Row{ "Profil wechseln", nullptr, nullptr, [this]() {
                auto go = [this]() { this->CloseAll(); this->Save(); this->host->Logoff(); };
                if(this->host->HasSuspended()) this->Confirm("Profil wechseln?", "Das laufende Spiel wird dabei geschlossen.", "Spiel schließen und wechseln", [this, go]() { this->host->CloseSuspended(); go(); });
                else go();
            } });
            rows.push_back(Row{ "NEXUS-Einstellungen", nullptr, nullptr, [this]() { this->CloseAll(); this->OpenSettings(); } });
            rows.push_back(Row{ "System", nullptr, nullptr, nullptr, true });
            for(const auto &a: this->host->SystemActions()) {
                const SysAction act = a;
                rows.push_back(Row{ a.label, nullptr, nullptr, [this, act]() {
                    if(act.confirm.empty()) { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); }
                    else this->Confirm(act.label + "?", act.confirm, act.label, [this, act]() { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); });
                } });
            }
            return rows;
        };
        this->Push(dl);
    }

    std::vector<UserInfo> Home::ProfileItems() const {
        std::vector<UserInfo> items = this->users;
        if(this->d->settings.B("profile.showAddUser")) items.push_back(UserInfo{ "add", "Hinzufügen", "" });
        return items;
    }

    void Home::ProfilePress(uint32_t b) {
        using namespace plat;
        const auto items = this->ProfileItems();
        if(items.empty()) return;
        const bool vert = this->d->settings.S("profile.style") == "list";
        const int n = static_cast<int>(items.size());
        if(b == (vert ? BtnUp : BtnLeft)) this->userFocus = (this->userFocus + n - 1) % n;
        else if(b == (vert ? BtnDown : BtnRight)) this->userFocus = (this->userFocus + 1) % n;
        else if(b == BtnA) {
            const auto &u = items[std::clamp(this->userFocus, 0, n - 1)];
            if(!this->host) return;
            this->host->BeforeLeave();
            if(u.id == "add") this->host->AddUser();
            else this->host->SelectUser(u.id);
        }
        else if(b == BtnPlus && this->host) {
            // Systemfunktionen auch ohne Anmeldung erreichbar (z. B. Ausschalten)
            this->screen = Screen::Main;
            this->OpenQuickMenu();
            this->screen = Screen::Profile;
        }
    }

    void Home::DrawProfile() {
        const auto &s = this->d->settings;
        this->profileT += this->frameDt;
        this->rowHits.clear();
        // Hintergrund
        const auto bg = s.S("profile.bgType");
        SDL_Texture *hero = (bg == "hero" && !this->lastHeroKey.empty()) ? this->img->Get(this->lastHeroKey, 0) : nullptr;
        if(hero) this->g->Image(hero, 0, 0, ScreenW, ScreenH, 1.0, true);
        else if(bg == "image" && this->DrawBgImage(s.S("profile.image"), 0, 1.0)) {}
        else if(bg == "color") this->g->Rect(0, 0, ScreenW, ScreenH, ParseCol(s.S("profile.color")));
        else if(bg == "gradient") {
            const int p = std::clamp(static_cast<int>(s.N("profile.preset")), 0, 7);
            this->g->GradV(0, 0, ScreenW, ScreenH / 2 + 1, ParseCol(Presets[p][0]), ParseCol(Presets[p][1]));
            this->g->GradV(0, ScreenH / 2, ScreenW, ScreenH / 2, ParseCol(Presets[p][1]), ParseCol(Presets[p][2]));
        }
        else this->DrawBackground();
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, static_cast<uint8_t>(s.N("profile.dim") * 255) });
        const Col tc = this->TextCol();
        const auto pos = s.S("profile.position");
        if(s.B("profile.title")) this->g->Text("Wer spielt?", 34, ScreenW / 2, pos == "top" ? 70 : pos == "bottom" ? 120 : 110, tc, 1, 0, true);
        if(s.B("profile.showClock")) {
            const std::time_t now = std::time(nullptr);
            char buf[16];
            std::strftime(buf, sizeof(buf), s.B("topbar.clock24") ? "%H:%M" : "%I:%M", std::localtime(&now));
            this->g->Text(buf, 18, ScreenW - 40, 22, tc, 2);
        }
        const auto items = this->ProfileItems();
        const int n = static_cast<int>(items.size());
        if(n == 0) {
            this->g->Text("Keine Benutzerprofile gefunden.", 18, ScreenW / 2, ScreenH / 2, tc, 1);
            return;
        }
        this->userFocus = std::clamp(this->userFocus, 0, n - 1);
        const auto style = s.S("profile.style");
        const double sz = s.N("profile.iconSize");
        const bool list = style == "list", cards = style == "cards";
        const double iw = list ? 440 : sz, ih = list ? 76 : (cards ? sz * 1.25 : sz);
        const double gap = list ? 12 : (cards ? 24 : 40);
        const double nameH = (!list && s.B("profile.showNames")) ? 34 : 0;
        const double totalW = list ? iw : n * iw + (n - 1) * gap;
        const double totalH = list ? n * ih + (n - 1) * gap : ih + nameH;
        double y0 = pos == "top" ? 160 : pos == "bottom" ? ScreenH - 90 - totalH : (ScreenH - totalH) / 2 + 30;
        const double x0 = (ScreenW - totalW) / 2;
        const bool animOn = s.B("anim.enabled") && s.S("profile.anim") != "none";
        const auto anim = s.S("profile.anim");
        for(int i = 0; i < n; i++) {
            const auto &u = items[i];
            const double t = animOn ? EaseOut(std::clamp((this->profileT - i * 0.06) / 0.45, 0.0, 1.0)) : 1.0;
            const double a = t;
            double dy = anim == "slideUp" ? 60 * (1 - t) : 0;
            double sc = anim == "pop" ? 0.6 + 0.4 * t : 1.0;
            const bool foc = i == this->userFocus;
            if(foc) sc *= 1.08;
            double cx, cy;
            if(list) { cx = x0 + iw / 2; cy = y0 + i * (ih + gap) + ih / 2 + dy; }
            else { cx = x0 + i * (iw + gap) + iw / 2; cy = y0 + ih / 2 + dy; }
            Xf xf; xf.cx = cx; xf.cy = cy; xf.sc = sc;
            SDL_Texture *tex = u.iconKey.empty() ? nullptr : this->img->Get(u.iconKey, 0);
            if(list) {
                xf.w = iw; xf.h = ih;
                this->g->Shape(xf, 14, false, WithA(Col{ 127, 127, 127, 255 }, (foc ? 0.32 : 0.16) * a));
                if(foc) this->g->ShapeRingFn(xf, 14, false, 3.5, this->AccentFn(cx - iw / 2, cy - ih / 2, iw, ih), a);
                Xf ic; ic.cx = cx - iw / 2 * sc + 14 * sc + 28 * sc; ic.cy = cy; ic.w = ic.h = 56; ic.sc = sc;
                if(tex) this->g->ShapeTex(tex, ic, 28, false, a, true, 50, 50);
                else { this->g->Shape(ic, 28, false, WithA(Col{ 127, 127, 127, 255 }, 0.35 * a)); this->g->Text("+", 26, ic.cx, ic.cy - 18, WithA(tc, a), 1); }
                this->g->Text(u.name, 20, cx - iw / 2 * sc + 90 * sc, cy - 13, WithA(tc, a));
            }
            else {
                xf.w = iw; xf.h = ih;
                const double rad = cards ? 16 : iw / 2;
                const ColFn afn = this->AccentFn(cx - iw / 2 - 20, cy - ih / 2 - 20, iw + 40, ih + 40);
                if(foc) for(int k = 3; k >= 1; k--) this->g->ShapeRingFn(xf, rad, false, 4 + k * 5, afn, 0.13 / k * a);
                if(tex) this->g->ShapeTex(tex, xf, rad, false, a, true, 50, 50);
                else { this->g->Shape(xf, rad, false, WithA(Col{ 127, 127, 127, 255 }, 0.3 * a)); this->g->Text("+", static_cast<int>(sz * 0.4), cx, cy - sz * 0.27, WithA(tc, a), 1); }
                if(foc) this->g->ShapeRingFn(xf, rad, false, 4, afn, a);
                if(nameH > 0) this->g->Text(u.name, 18, cx, cy + ih / 2 * sc + 10, WithA(tc, (foc ? 1.0 : 0.75) * a), 1, static_cast<int>(iw + gap - 6));
            }
            const double hw = iw * sc, hh = ih * sc + nameH;
            this->rowHits.push_back(Hit{ cx - hw / 2, cy - ih * sc / 2, hw, hh, u.id, i });
        }
    }

}
