// NEXUS HOME: Bildauswahl, SteamGridDB, RetroAchievements, Tastatur-Aufschub
#include "home.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

namespace nexus {

    namespace {

        bool IsImageFile(const std::string &n) {
            std::string l = n;
            for(auto &c: l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for(const char *e: { ".jpg", ".jpeg", ".png", ".webp" }) {
                const std::string x(e);
                if(l.size() > x.size() && l.compare(l.size() - x.size(), x.size(), x) == 0) return true;
            }
            return false;
        }

        // Einträge eines Ordners (nur Namen), absteigend sortiert = neueste Album-Ordner zuerst
        std::vector<std::string> ListDir(const std::string &dir, bool wantDirs) {
            std::vector<std::string> out;
            DIR *d = opendir(dir.c_str());
            if(!d) return out;
            while(dirent *e = readdir(d)) {
                const std::string n = e->d_name;
                if(n.empty() || n[0] == '.') continue;
                bool isDir = false;
#ifdef DT_DIR
                if(e->d_type == DT_DIR) isDir = true;
                else if(e->d_type == DT_UNKNOWN)
#endif
                {
                    struct stat st;
                    isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
                }
                if(isDir == wantDirs && (wantDirs || IsImageFile(n))) out.push_back(n);
                if(out.size() >= 400) break;
            }
            closedir(d);
            std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) { return a > b; });
            return out;
        }

        std::string BaseName(const std::string &p) {
            const auto k = p.find_last_of('/');
            return k == std::string::npos ? p : p.substr(k + 1);
        }

        std::string Masked(const std::string &key) {
            if(key.empty()) return "nicht hinterlegt";
            return "••••" + key.substr(key.size() > 4 ? key.size() - 4 : 0);
        }

        std::string Trim(const std::string &s) {
            size_t a = 0, b = s.size();
            while(a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
            while(b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
            return s.substr(a, b - a);
        }

        constexpr char KeysFile[] = "sdmc:/nexus/keys.txt";
        constexpr double kHalfPi = 1.57079632679489661923;

    }

    // ------------------------------------------------------------------ Tastatur

    void Home::AskText(const std::string &header, const std::string &initial, size_t maxLen, std::function<void(std::string)> done) {
        auto run = [this, header, initial, maxLen, done]() {
            std::string out;
            if(!plat::Keyboard(header, initial, maxLen, out)) {
                this->Toast("Tastatur nicht verfügbar. Alternativ Werte in sdmc:/nexus/keys.txt eintragen.");
                return;
            }
            done(Trim(SanitizeUtf8(out)));
        };
        // In uMenu nicht mitten im Zeichnen ein Applet öffnen: in der nächsten Eingabe-Phase ausführen
        if(this->host) this->deferred = run;
        else run();
    }

    void Home::RunDeferred() {
        if(!this->deferred) return;
        auto f = std::move(this->deferred);
        this->deferred = nullptr;
        f();
    }

    void Home::Shutdown() {
        this->online.Shutdown();
        if(this->dirtySave) this->Save();
    }

    // ------------------------------------------------------------------ Bildauswahl

    void Home::OpenImagePicker(const std::string &title, const std::vector<std::pair<std::string, std::string>> &dirs, bool album, bool dark, std::function<void(const std::string &)> onPick, const std::string &noneLabel) {
        struct Item { std::string label, path; bool head; };
        auto items = std::make_shared<std::vector<Item>>();
        if(!noneLabel.empty()) items->push_back(Item{ noneLabel, "", false });
        for(const auto &[label, dir]: dirs) {
            const auto files = ListDir(dir, false);
            if(files.empty()) continue;
            items->push_back(Item{ label, "", true });
            for(const auto &f: files) items->push_back(Item{ f, dir + "/" + f, false });
        }
        if(album) {
            // Screenshots aus dem Album (sdmc:/Nintendo/Album/JJJJ/MM/TT), neueste zuerst
            const std::string root = "sdmc:/Nintendo/Album";
            int count = 0;
            bool head = false;
            for(const auto &yy: ListDir(root, true)) {
                for(const auto &mm: ListDir(root + "/" + yy, true)) {
                    for(const auto &dd: ListDir(root + "/" + yy + "/" + mm, true)) {
                        const std::string dir = root + "/" + yy + "/" + mm + "/" + dd;
                        for(const auto &f: ListDir(dir, false)) {
                            if(!head) { items->push_back(Item{ "Album (Screenshots)", "", true }); head = true; }
                            items->push_back(Item{ dd + "." + mm + "." + yy + "  ·  " + f.substr(0, 14), dir + "/" + f, false });
                            if(++count >= 150) break;
                        }
                        if(count >= 150) break;
                    }
                    if(count >= 150) break;
                }
                if(count >= 150) break;
            }
        }
        auto dl = std::make_shared<Dialog>();
        dl->title = title;
        const bool empty = items->size() <= (noneLabel.empty() ? 0u : 1u);
        dl->sub = empty ? "Noch keine Bilder gefunden. Lege JPG- oder PNG-Dateien in sdmc:/nexus/art/backgrounds/ ab (z. B. 1280×720) oder nutze Screenshots aus dem Album."
                        : "Eigene Bilder: sdmc:/nexus/art/backgrounds/ · A = übernehmen";
        dl->w = 1000; dl->x = 140; dl->y = 74; dl->shade = true; dl->preview = true;
        dl->h = ScreenH - dl->y - this->Env().botH - 8;
        dl->hints = { { "↑↓", "Bild" }, { "A", "Übernehmen" }, { "B", "Abbrechen" } };
        dl->rows = [this, items, onPick, dark]() {
            std::vector<Row> rows;
            for(const auto &it: *items) {
                Row rw{ it.label, nullptr, nullptr, nullptr, it.head };
                if(!it.head) {
                    const std::string path = it.path;
                    rw.thumb = path;
                    rw.dark = dark;
                    rw.activate = [this, path, onPick]() { this->Pop(); onPick(path); this->Toast(path.empty() ? "Bild entfernt." : "Bild übernommen: " + BaseName(path)); };
                }
                rows.push_back(rw);
            }
            return rows;
        };
        this->Push(dl);
    }

    void Home::OpenArtKind(const std::string &id, const std::string &kind) {
        const std::string title = kind == "grid" ? "Cover wählen" : kind == "hero" ? "Hintergrund wählen" : "Logo wählen";
        std::vector<std::pair<std::string, std::string>> dirs = { { "Bilder dieses Titels", std::string(ArtDir) + "/" + id } };
        if(kind != "logo") dirs.push_back({ "Eigene Hintergründe", std::string(ArtDir) + "/backgrounds" });
        this->OpenImagePicker(title, dirs, kind == "hero", kind == "logo", [this, id, kind](const std::string &path) {
            this->d->library.SetArtOverride(id, kind, path);
            this->ArtChanged(id);
            this->MarkDirty();
            if(!path.empty() && kind != "logo") this->OpenCropEditor(path, kind == "grid" ? "Cover-Ausschnitt" : "Hintergrund-Ausschnitt", kind == "grid" ? this->TileAspect() : ScreenW / ScreenH);
        }, "Automatisch (grid/hero/logo-Datei im Titelordner)");
    }

    // ------------------------------------------------------------------ Bildausschnitt

    double Home::TileAspect() const {
        const auto r = this->d->settings.S("tileDefaults.ratio");
        const auto k = r.find(':');
        if(k == std::string::npos) return 2.0 / 3.0;
        const double a = std::atof(r.substr(0, k).c_str()), b = std::atof(r.substr(k + 1).c_str());
        return (a > 0 && b > 0) ? a / b : 2.0 / 3.0;
    }

    void Home::OpenCropEditor(const std::string &path, const std::string &title, double aspect, std::function<void()> done) {
        if(path.empty() || path.rfind("sdmc:/", 0) != 0) { this->Toast("Für dieses Bild ist kein Ausschnitt möglich."); return; }
        this->crop.active = true;
        this->crop.path = path;
        this->crop.title = title;
        this->crop.aspect = std::clamp(aspect, 0.3, 4.0);
        this->crop.backup = this->d->library.CropOf(path);
        this->crop.c = this->crop.backup;
        this->crop.c.set = true;
        this->crop.done = std::move(done);
    }

    void Home::CropInput(const plat::Input &in) {
        using namespace plat;
        auto &c = this->crop.c;
        const double dt = std::clamp(this->frameDt, 0.0, 0.05);
        const double mv = 45.0 * dt, zm = 0.9 * dt;
        if(in.held & BtnLeft) c.x -= mv;
        if(in.held & BtnRight) c.x += mv;
        if(in.held & BtnUp) c.y -= mv;
        if(in.held & BtnDown) c.y += mv;
        if(in.held & (BtnR | BtnZR)) c.zoom += zm * c.zoom;
        if(in.held & (BtnL | BtnZL)) c.zoom -= zm * c.zoom;
        c.x = std::clamp(c.x, 0.0, 100.0);
        c.y = std::clamp(c.y, 0.0, 100.0);
        c.zoom = std::clamp(c.zoom, 1.0, 4.0);
        // Live-Vorschau: der Ausschnitt gilt sofort überall
        this->d->library.SetCrop(this->crop.path, c);
        if(in.down & BtnY) { c.zoom = 1; c.x = 50; c.y = 50; }
        if(in.down & BtnA) {
            this->crop.active = false;
            this->MarkDirty();
            this->Toast("Bildausschnitt gespeichert.");
            if(this->crop.done) this->crop.done();
        }
        else if(in.down & BtnB) {
            this->d->library.SetCrop(this->crop.path, this->crop.backup);
            this->crop.active = false;
            this->Toast("Ausschnitt verworfen.");
        }
    }

    void Home::DrawCropEditor() {
        const auto &cr = this->crop;
        SDL_Texture *t = this->img->Failed(cr.path) ? nullptr : this->img->Get(cr.path, 0);
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, 235 });
        // Rahmen im Zielformat, so groß wie möglich
        double fw = ScreenW - 120, fh = fw / cr.aspect;
        if(fh > ScreenH - 190) { fh = ScreenH - 190; fw = fh * cr.aspect; }
        const double fx = (ScreenW - fw) / 2, fy = 70 + (ScreenH - 190 - fh) / 2;
        if(t) {
            // ganzes Bild blass dahinter, Ausschnitt hell im Rahmen
            int tw = 0, th = 0;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            if(tw > 0 && th > 0) {
                double sx, sy, sw, sh;
                CoverSrc(tw, th, fw / fh, cr.c.x, cr.c.y, cr.c.zoom, sx, sy, sw, sh);
                const double k = fw / sw;   // Bildpunkte -> Bildschirm
                this->g->ImageEx(t, fx - sx * k, fy - sy * k, tw * k, th * k, 0.25, 1, 50, 50);
            }
            this->g->ImageEx(t, fx, fy, fw, fh, 1.0, cr.c.zoom, cr.c.x, cr.c.y);
        }
        else this->g->Text(this->img->Failed(cr.path) ? "Bild nicht lesbar" : "Bild wird geladen …", 18, ScreenW / 2, ScreenH / 2 - 12, Col{ 220, 226, 236, 255 }, 1);
        Xf rf; rf.cx = fx + fw / 2; rf.cy = fy + fh / 2; rf.w = fw; rf.h = fh;
        this->g->ShapeRingFn(rf, 0, false, 3, this->AccentFn(fx, fy, fw, fh), 1.0);
        this->g->Text(cr.title, 22, ScreenW / 2, 18, Col{ 238, 242, 247, 255 }, 1, 1100, true);
        char info[96];
        std::snprintf(info, sizeof(info), "Zoom %d %%   ·   Position %d %% / %d %%", static_cast<int>(std::lround(cr.c.zoom * 100)), static_cast<int>(std::lround(cr.c.x)), static_cast<int>(std::lround(cr.c.y)));
        this->g->Text(info, 15, ScreenW / 2, 48, Col{ 147, 160, 180, 255 }, 1);
        this->g->Text("Stick/Steuerkreuz = verschieben   ·   R/ZR = näher   ·   L/ZL = weiter   ·   Y = zurücksetzen   ·   A = speichern   ·   B = abbrechen", 14, ScreenW / 2, ScreenH - 40, Col{ 200, 208, 220, 255 }, 1, 1220);
    }

    // ------------------------------------------------------------------ Logo frei verschieben

    void Home::StartLogoMove() {
        if(this->list.empty()) { this->Toast("Zum Verschieben eine Kachel fokussieren (Startseite)."); return; }
        const json *lg = JGet(this->d->settings.data, "logo");
        this->logoMove.backup = lg ? *lg : json::object();
        auto &s = this->d->settings;
        if(s.S("logo.placement") != "free") {
            // Startpunkt: ungefähr dort, wo das Logo gerade steht
            const auto ax = s.S("logo.anchorX"), ay = s.S("logo.anchorY");
            s.Set("logo.posX", json(ax == "left" ? 25.0 : ax == "center" ? 50.0 : 75.0));
            s.Set("logo.posY", json(ay == "top" ? 22.0 : ay == "middle" ? 50.0 : 70.0));
            s.Set("logo.placement", json("free"));
        }
        s.Set("logo.visible", json(true));
        this->CloseAll();
        this->logoMove.active = true;
    }

    void Home::LogoMoveInput(const plat::Input &in) {
        using namespace plat;
        auto &s = this->d->settings;
        const double dt = std::clamp(this->frameDt, 0.0, 0.05);
        double x = s.N("logo.posX"), y = s.N("logo.posY"), sc = s.N("logo.scale");
        const double mv = 30.0 * dt;
        if(in.held & BtnLeft) x -= mv;
        if(in.held & BtnRight) x += mv;
        if(in.held & BtnUp) y -= mv;
        if(in.held & BtnDown) y += mv;
        if(in.held & (BtnR | BtnZR)) sc += 0.6 * dt;
        if(in.held & (BtnL | BtnZL)) sc -= 0.6 * dt;
        if(in.down & BtnY) { x = 50; y = 50; }
        s.Set("logo.posX", json(std::round(std::clamp(x, 0.0, 100.0) * 10) / 10));
        s.Set("logo.posY", json(std::round(std::clamp(y, 0.0, 100.0) * 10) / 10));
        s.Set("logo.scale", json(std::clamp(sc, 0.4, 2.0)));
        if(in.down & BtnA) {
            this->logoMove.active = false;
            // Werte auf das Raster des Schemas bringen
            s.Set("logo.posX", json(std::round(s.N("logo.posX"))));
            s.Set("logo.posY", json(std::round(s.N("logo.posY"))));
            s.Set("logo.scale", json(std::round(s.N("logo.scale") * 20) / 20));
            this->MarkDirty();
            this->Toast("Logo-Position gespeichert (Einstellungen → Fokus: Logo → Platzierung).");
        }
        else if(in.down & BtnB) {
            this->d->settings.data["logo"] = this->logoMove.backup;
            this->d->settings.data = Settings::Sanitize(this->d->settings.data, nullptr);
            this->logoMove.active = false;
            this->Toast("Verschieben abgebrochen.");
        }
    }

    // ------------------------------------------------------------------ Partikel

    void Home::DrawParticles(int layer) {
        if(this->fx.parts.empty()) return;
        const bool aura = layer < 2;
        const std::string shape = aura ? this->auraC.shape : BgShape(this->bgC);
        SDL_Texture *tex = nullptr;
        bool tint = true;
        if(shape == "custom") {
            const auto path = this->d->settings.S(aura ? "aura.image" : "bgfx.image");
            if(!path.empty() && !this->img->Failed(path)) tex = this->img->Get(path, 1);
            if(!tex) tex = this->g->ParticleSprite("glow");
        }
        else tex = this->g->ParticleSprite(shape);
        if(!tex) return;
        std::vector<Gfx::Sprite> list;
        list.reserve(this->fx.parts.size());
        for(const auto &p: this->fx.parts) {
            if(p.layer != layer) continue;
            float a, sz;
            ParticleSystem::Look(p, this->time, a, sz);
            if(a < 0.01f) continue;
            const float rot = p.stretch ? static_cast<float>(std::atan2(p.vy, p.vx) - kHalfPi) : p.rot;
            list.push_back(Gfx::Sprite{ p.x, p.y, sz, rot, Col{ tint ? p.r : uint8_t(255), tint ? p.g : uint8_t(255), tint ? p.b : uint8_t(255), static_cast<uint8_t>(a * 255) } });
        }
        this->g->Sprites(tex, list, aura ? this->auraC.additive : this->bgC.additive);
    }

    // ------------------------------------------------------------------ Bildschirmschoner & Startbereich

    void Home::UpdateIdle(double dt, bool input) {
        const auto &s = this->d->settings;
        const auto mode = s.S("idle.mode");
        if(input) {
            this->idleActive = false;
            this->idleT = 0;
            return;
        }
        if(mode == "off" || this->screen != Screen::Main || this->locked || !this->dialogs.empty() || this->crop.active || this->logoMove.active) {
            this->idleT = 0;
            this->idleActive = false;
            return;
        }
        this->idleT += dt;
        if(!this->idleActive && this->idleT >= s.N("idle.minutes") * 60.0) {
            if(mode == "lock") { this->locked = true; this->unlocking = false; this->idleT = 0; return; }
            this->idleActive = true;
            this->slideT = 1e9;   // sofort erstes Bild
        }
        if(this->idleActive && mode == "slideshow") {
            this->slideT += dt;
            if(this->slideT >= s.N("idle.slideSeconds") && !this->installed.empty()) {
                this->slideT = 0;
                // nächster Titel mit eigenem Hintergrundbild
                for(size_t k = 0; k < this->installed.size(); k++) {
                    this->slideIdx = (this->slideIdx + 1) % static_cast<int>(this->installed.size());
                    const auto &id = this->installed[this->slideIdx];
                    const auto &a = this->Art(id);
                    if(!a.hero.empty()) { this->slidePrev = this->slideKey; this->slideKey = a.hero; this->slideId = id; break; }
                }
            }
        }
    }

    void Home::DrawIdle() {
        const auto &s = this->d->settings;
        const auto mode = s.S("idle.mode");
        const std::time_t now = std::time(nullptr);
        const std::tm *lt = std::localtime(&now);
        char clk[16];
        std::strftime(clk, sizeof(clk), s.B("topbar.clock24") ? "%H:%M" : "%I:%M", lt);
        if(mode == "dim") {
            this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, 170 });
            return;
        }
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 0, 0, 0, 255 });
        if(mode == "slideshow" && !this->slideKey.empty()) {
            const double fadeT = std::min(1.0, this->slideT / 1.2);
            if(!this->slidePrev.empty() && fadeT < 1) {
                if(SDL_Texture *t = this->img->Get(this->slidePrev, 0)) this->g->ImageEx(t, 0, 0, ScreenW, ScreenH, 1 - fadeT, 1.05, 50, 50);
            }
            if(SDL_Texture *t = this->img->Get(this->slideKey, 0)) {
                const double z = 1.0 + 0.06 * std::min(1.0, this->slideT / std::max(1.0, s.N("idle.slideSeconds")));   // langsamer Zoom
                const Crop c = this->d->library.CropOf(this->slideKey);
                this->g->ImageEx(t, 0, 0, ScreenW, ScreenH, fadeT, (c.set ? c.zoom : 1.0) * z, c.set ? c.x : 50, c.set ? c.y : 50);
            }
            this->g->GradV(0, ScreenH * 0.6, ScreenW, ScreenH * 0.4, Col{ 0, 0, 0, 0 }, Col{ 0, 0, 0, 200 });
            const auto &a = this->Art(this->slideId);
            SDL_Texture *lg = a.logo.empty() ? nullptr : this->img->Get(a.logo, 1);
            if(lg) {
                int tw = 0, th = 0;
                SDL_QueryTexture(lg, nullptr, nullptr, &tw, &th);
                if(tw > 0 && th > 0) { const double w = 380, h = w * th / tw; this->g->Image(lg, 60, ScreenH - 60 - h, w, h, fadeT, true); }
            }
            this->g->Text(clk, 44, ScreenW - 60, ScreenH - 100, Col{ 255, 255, 255, 230 }, 2);
            return;
        }
        // große Uhr, langsam wandernd (schont den Bildschirm)
        const double px = ScreenW / 2 + std::sin(this->time * 0.05) * 260, py = ScreenH / 2 - 70 + std::cos(this->time * 0.037) * 150;
        this->g->TextFn(clk, 120, px, py, this->AccentFn(px - 220, py, 440, 140), 1.0, 1);
    }

    void Home::ApplyStartSection() {
        this->startApplied = true;
        const auto mode = this->d->settings.S("library.startSection");
        std::string target = mode;
        if(mode == "last") {
            const json *l = JGet(this->d->settings.data, "library.lastSection");
            target = (l && l->is_string()) ? l->get<std::string>() : std::string("start");
        }
        if(target == "start" || target == this->section) return;
        const auto vis = this->d->settings.VisibleSections();
        if(std::find(vis.begin(), vis.end(), target) != vis.end() && target != "settings") this->SwitchSection(target);
    }

    // ------------------------------------------------------------------ Eigene Themes

    void Home::OpenThemes() {
        auto dl = std::make_shared<Dialog>();
        dl->title = "Eigene Themes";
        dl->sub = "Gespeichert in sdmc:/nexus/themes/ – Updates löschen sie nicht. Schlüssel, RetroAchievements-Konto und Bibliothek bleiben beim Laden unverändert.";
        dl->w = 600; dl->x = this->DialogX(600); dl->y = 74; dl->shade = true;
        dl->h = ScreenH - dl->y - this->Env().botH - 8;
        dl->rows = [this]() {
            std::vector<Row> rows;
            rows.push_back(Row{ "Aktuelle Darstellung als Theme speichern …", nullptr, nullptr, [this]() {
                this->AskText("Name des Themes", this->d->settings.S("theme.name"), 40, [this](std::string name) {
                    if(name.empty()) return;
                    this->d->settings.Set("theme.name", json(name));
                    std::string path;
                    if(SaveUserTheme(ThemesDir, name, this->d->settings.data, &path)) this->Toast("Theme „" + name + "“ gespeichert.");
                    else this->Toast("Speichern fehlgeschlagen (SD-Karte?).");
                    this->MarkDirty();
                });
            } });
            const auto list = ListUserThemes(ThemesDir);
            if(list.empty()) rows.push_back(Row{ "Noch keine eigenen Themes gespeichert.", nullptr, nullptr, nullptr });
            else rows.push_back(Row{ "Laden (A) · Löschen (X)", nullptr, nullptr, nullptr, true });
            for(const auto &t: list) {
                const UserTheme th = t;
                rows.push_back(Row{ th.name, nullptr, nullptr, [this, th]() {
                    json result;
                    if(!ApplyUserTheme(th.path, this->d->settings.data, result)) { this->Toast("Theme-Datei ist beschädigt."); return; }
                    this->d->settings.data = result;
                    this->listDirty = true; this->layoutDirty = true;
                    this->MarkDirty();
                    this->Toast("Theme „" + th.name + "“ geladen.");
                } });
            }
            return rows;
        };
        dl->onX = [this, dl]() {
            const auto list = ListUserThemes(ThemesDir);
            // Zeilen: 0 = Speichern, 1 = Überschrift, ab 2 die Themes
            const int i = dl->sel - 2;
            if(i < 0 || i >= static_cast<int>(list.size())) return;
            const UserTheme th = list[i];
            this->Confirm("Theme „" + th.name + "“ löschen?", "Die Datei wird von der SD-Karte entfernt.", "Löschen", [this, th]() {
                std::remove(th.path.c_str());
                this->Toast("Theme gelöscht.");
            });
        };
        dl->hints = { { "A", "Laden" }, { "X", "Löschen" }, { "B", "Zurück" } };
        this->Push(dl);
    }

    void Home::ArtChanged(const std::string &id) {
        auto it = this->art.find(id);
        if(it != this->art.end()) {
            for(const auto &k: { it->second.grid, it->second.hero, it->second.logo }) if(!k.empty()) this->img->Invalidate(k);
            this->art.erase(it);
        }
        this->heroA.key.clear();
        this->heroFor.clear();
        this->logoId.clear();
        this->listDirty = true;
        this->MarkDirty();
    }

    bool Home::DrawBgImage(const std::string &path, double oy, double alpha) {
        if(path.empty()) return false;
        if(this->img->Failed(path)) return false;
        SDL_Texture *t = this->img->Get(path, 0);
        if(!t) return false;
        const Crop c = this->d->library.CropOf(path);
        this->g->ImageEx(t, 0, oy, ScreenW, ScreenH, alpha, c.zoom, c.x, c.y);
        return true;
    }

    // ------------------------------------------------------------------ Schlüssel

    void Home::ImportKeys(bool quiet) {
        std::string txt;
        if(!ReadFile(KeysFile, txt, 64 * 1024)) {
            if(!quiet) this->Toast("sdmc:/nexus/keys.txt nicht gefunden.");
            return;
        }
        int n = 0;
        size_t pos = 0;
        while(pos < txt.size()) {
            size_t nl = txt.find('\n', pos);
            if(nl == std::string::npos) nl = txt.size();
            const std::string line = Trim(txt.substr(pos, nl - pos));
            pos = nl + 1;
            const auto eq = line.find('=');
            if(line.empty() || line[0] == '#' || eq == std::string::npos) continue;
            const std::string k = Trim(line.substr(0, eq)), v = SanitizeUtf8(Trim(line.substr(eq + 1)));
            if(v.empty() || v.size() > 120) continue;
            if(k == "sgdb" || k == "steamgriddb") { this->d->settings.Set("sgdbKey", json(v)); n++; }
            else if(k == "ra_user") { this->d->settings.Set("ra.user", json(v.substr(0, 60))); n++; }
            else if(k == "ra_key") { this->d->settings.Set("ra.key", json(v)); n++; }
        }
        // Die Klartext-Datei nach dem Übernehmen entfernen
        std::remove(KeysFile);
        this->Save();
        this->Toast(n > 0 ? std::to_string(n) + " Schlüssel aus keys.txt übernommen (Datei gelöscht)." : "keys.txt enthielt keine gültigen Einträge.");
    }

    // ------------------------------------------------------------------ SteamGridDB

    std::vector<Row> Home::SgdbRows() {
        std::vector<Row> rows;
        rows.push_back(Row{ "SteamGridDB", nullptr, nullptr, nullptr, true });
        rows.push_back(Row{ "API-Key", [this]() { return Masked(this->d->settings.S("sgdbKey")); }, nullptr, [this]() {
            this->AskText("SteamGridDB-API-Key (steamgriddb.com → Einstellungen → API)", "", 100, [this](std::string v) {
                if(v.empty()) return;
                this->d->settings.Set("sgdbKey", json(v));
                this->Save();
                this->Toast("API-Key gespeichert (nur lokal, nie im Log).");
            });
        } });
        rows.push_back(Row{ "Schlüssel aus sdmc:/nexus/keys.txt laden", nullptr, nullptr, [this]() { this->ImportKeys(false); } });
        rows.push_back(Row{ "Fehlende Bilder jetzt laden", [this]() {
                const int n = this->online.AutoRemaining();
                return n > 0 ? std::to_string(n) + " in Arbeit" : std::string();
            }, nullptr, [this]() { this->QueueMissingArt(true); } });
        return rows;
    }

    void Home::QueueMissingArt(bool force) {
        if(!this->online.HasSgdbKey()) { if(force) this->OpenSgdbKeyMissing(); return; }
        if(!Online::IsOnline()) { if(force) this->Toast("Keine Internetverbindung."); return; }
        int n = 0;
        for(const auto &id: this->installed) {
            const auto &a = this->Art(id);
            if(!a.grid.empty() && !a.hero.empty() && !a.logo.empty()) continue;
            auto it = this->appMap.find(id);
            if(it == this->appMap.end() || it->second.name.empty()) continue;
            const int before = this->online.AutoRemaining();
            this->online.QueueAutoArt(id, it->second.name, a.grid.empty(), a.hero.empty(), a.logo.empty());
            if(this->online.AutoRemaining() > before) n++;
        }
        if(force) this->Toast(n > 0 ? std::to_string(n) + " Titel werden im Hintergrund ergänzt." : "Nichts zu tun: alle Titel haben Bilder (oder wurden schon versucht).");
        else if(n > 0) Log("SGDB automatisch: %d Titel", n);
    }

    void Home::OpenSgdbKeyMissing() {
        auto dl = std::make_shared<Dialog>();
        dl->title = "SteamGridDB-API-Key fehlt";
        dl->sub = "Kostenlos auf steamgriddb.com → Einstellungen → API. Eingabe hier oder bequem am PC: Datei sdmc:/nexus/keys.txt mit der Zeile sgdb=DEIN_KEY anlegen.";
        dl->w = 600; dl->x = 340; dl->y = 180; dl->shade = true;
        dl->rows = [this]() {
            return std::vector<Row>{
                Row{ "API-Key eingeben", nullptr, nullptr, [this]() {
                    this->Pop();
                    this->AskText("SteamGridDB-API-Key", "", 100, [this](std::string v) {
                        if(v.empty()) return;
                        this->d->settings.Set("sgdbKey", json(v));
                        this->Save();
                        this->Toast("API-Key gespeichert.");
                    });
                } },
                Row{ "Aus keys.txt laden", nullptr, nullptr, [this]() { this->Pop(); this->ImportKeys(false); } },
                Row{ "Abbrechen", nullptr, nullptr, [this]() { this->Pop(); } } };
        };
        this->Push(dl);
    }

    void Home::OpenSgdbFlow(const std::string &id, const std::string &name, const std::string &term) {
        if(!this->online.HasSgdbKey()) { this->OpenSgdbKeyMissing(); return; }
        struct St { bool loading = true; std::string err; std::vector<SgdbGame> games; };
        auto st = std::make_shared<St>();
        auto dl = std::make_shared<Dialog>();
        dl->title = "SteamGridDB · " + name;
        dl->sub = "Suche nach „" + SearchTerm(term) + "“";
        dl->w = 560; dl->x = 360; dl->y = 90; dl->shade = true;
        dl->rows = [this, st, id, name]() {
            std::vector<Row> rows;
            if(st->loading) rows.push_back(Row{ "Suche läuft …", nullptr, nullptr, nullptr });
            else if(!st->err.empty()) rows.push_back(Row{ st->err, nullptr, nullptr, nullptr });
            else if(st->games.empty()) rows.push_back(Row{ "Keine Treffer.", nullptr, nullptr, nullptr });
            for(const auto &g: st->games) {
                const SgdbGame game = g;
                rows.push_back(Row{ g.name, [game]() { return game.verified ? std::string("verifiziert") : std::string(); }, nullptr, [this, id, game]() { this->OpenSgdbKinds(id, game); } });
            }
            rows.push_back(Row{ "Anderen Suchbegriff eingeben …", nullptr, nullptr, [this, id, name]() {
                this->AskText("Suchbegriff für SteamGridDB", SearchTerm(name), 80, [this, id, name](std::string t) {
                    if(t.empty()) return;
                    this->Pop();
                    this->OpenSgdbFlow(id, name, t);
                });
            } });
            return rows;
        };
        this->Push(dl);
        this->online.SgdbSearch(term, [st](std::vector<SgdbGame> games, std::string err) {
            st->loading = false;
            st->err = err;
            st->games = std::move(games);
        });
    }

    void Home::OpenSgdbKinds(const std::string &id, const SgdbGame &game) {
        auto dl = std::make_shared<Dialog>();
        dl->title = game.name;
        dl->sub = "Welche Ebene möchtest du ersetzen?";
        dl->w = 520; dl->x = 380; dl->y = 140; dl->shade = true;
        dl->rows = [this, id, game]() {
            return std::vector<Row>{
                Row{ "Cover (Kachel)", nullptr, nullptr, [this, id, game]() { this->OpenSgdbImages(id, game, "grids", "grid"); } },
                Row{ "Hintergrund (Hero)", nullptr, nullptr, [this, id, game]() { this->OpenSgdbImages(id, game, "heroes", "hero"); } },
                Row{ "Logo", nullptr, nullptr, [this, id, game]() { this->OpenSgdbImages(id, game, "logos", "logo"); } },
                Row{ "Alle drei automatisch (jeweils bestes Ergebnis)", nullptr, nullptr, [this, id, game]() {
                    this->online.QueueAutoArt(id, game.name, true, true, true, game.id, true);
                    this->CloseAll();
                    this->Toast("Cover, Hintergrund und Logo werden geladen …");
                } } };
        };
        this->Push(dl);
    }

    void Home::OpenSgdbImages(const std::string &id, const SgdbGame &game, const std::string &kind, const std::string &key) {
        struct St { bool loading = true; std::string err; std::vector<SgdbImage> list; };
        auto st = std::make_shared<St>();
        auto dl = std::make_shared<Dialog>();
        dl->title = game.name + (key == "grid" ? " · Cover" : key == "hero" ? " · Hintergrund" : " · Logo");
        dl->sub = "A = herunterladen und übernehmen";
        dl->w = 1000; dl->x = 140; dl->y = 74; dl->shade = true; dl->preview = true;
        dl->h = ScreenH - dl->y - this->Env().botH - 8;
        dl->hints = { { "↑↓", "Bild" }, { "A", "Übernehmen" }, { "B", "Zurück" } };
        dl->onSelect = [this, st](int sel) {
            // ausgewähltes und benachbarte Vorschaubilder laden
            if(st->list.empty()) return;
            for(int k = sel - 1; k <= sel + 3; k++) if(k >= 0 && k < static_cast<int>(st->list.size())) this->online.EnsureThumb(st->list[k]);
        };
        dl->rows = [this, st, id, key]() {
            std::vector<Row> rows;
            if(st->loading) { rows.push_back(Row{ "Lädt …", nullptr, nullptr, nullptr }); return rows; }
            if(!st->err.empty()) { rows.push_back(Row{ st->err, nullptr, nullptr, nullptr }); return rows; }
            if(st->list.empty()) { rows.push_back(Row{ "Keine Bilder in diesem Format.", nullptr, nullptr, nullptr }); return rows; }
            for(size_t i = 0; i < st->list.size(); i++) {
                const SgdbImage im = st->list[i];
                Row rw{ "#" + std::to_string(i + 1) + "  " + std::to_string(im.w) + "×" + std::to_string(im.h) + (im.style.empty() ? "" : "  · " + im.style), nullptr, nullptr, nullptr };
                rw.thumb = this->online.ThumbPath(im);
                rw.dark = key == "logo";
                rw.activate = [this, id, key, im]() {
                    const std::string dir = std::string(ArtDir) + "/" + id;
                    EnsureDir(ArtDir);
                    EnsureDir(dir);
                    const std::string path = dir + "/sgdb-" + key + "-" + std::to_string(im.id) + ImageExt(im.mime, im.url);
                    this->Toast("Wird heruntergeladen …");
                    this->online.Download(im.url, path, [this, id, key, path](bool ok, std::string err) {
                        if(!ok) { this->Toast("Download fehlgeschlagen: " + err); return; }
                        this->d->library.SetArtOverride(id, key, path);
                        this->ArtChanged(id);
                        this->Toast("Übernommen und gespeichert.");
                    });
                };
                rows.push_back(rw);
            }
            return rows;
        };
        this->Push(dl);
        this->online.SgdbImages(kind, game.id, [st](std::vector<SgdbImage> list, std::string err) {
            st->loading = false;
            st->err = err;
            st->list = std::move(list);
        });
    }

    // ------------------------------------------------------------------ RetroAchievements

    std::vector<Row> Home::RaRows() {
        std::vector<Row> rows;
        rows.push_back(Row{ "Konto", nullptr, nullptr, nullptr, true });
        rows.push_back(Row{ "Benutzername", [this]() { const auto u = this->d->settings.S("ra.user"); return u.empty() ? std::string("nicht hinterlegt") : u; }, nullptr, [this]() {
            this->AskText("RetroAchievements-Benutzername", this->d->settings.S("ra.user"), 60, [this](std::string v) {
                this->d->settings.Set("ra.user", json(v));
                this->Save();
            });
        } });
        rows.push_back(Row{ "Web-API-Key", [this]() { return Masked(this->d->settings.S("ra.key")); }, nullptr, [this]() {
            this->AskText("RetroAchievements Web-API-Key (retroachievements.org → Einstellungen → Keys)", "", 100, [this](std::string v) {
                if(v.empty()) return;
                this->d->settings.Set("ra.key", json(v));
                this->Save();
                this->Toast("Key gespeichert (nur lokal, nie im Log).");
            });
        } });
        rows.push_back(Row{ "Schlüssel aus sdmc:/nexus/keys.txt laden", nullptr, nullptr, [this]() { this->ImportKeys(false); } });
        rows.push_back(Row{ "Jetzt aktualisieren", [this]() {
                const auto &ra = this->online.ra;
                if(ra.loading) return std::string("lädt …");
                if(!ra.error.empty()) return ra.error;
                if(ra.profile.ok) return std::to_string(this->online.RaPoints()) + " Pkt. · " + std::to_string(ra.games.size()) + " Spiele";
                return std::string();
            }, nullptr, [this]() {
                if(!this->d->settings.B("ra.enabled")) { this->Toast("Zuerst „RetroAchievements anzeigen“ einschalten."); return; }
                if(!this->online.RaReady()) { this->Toast("Bitte Benutzername und Web-API-Key eintragen."); return; }
                this->online.RaRefresh(true);
            } });
        return rows;
    }

    void Home::DrawRaBadge(const Entry &e, const Xf &xf, double w, double h, double op) {
        const auto &s = this->d->settings;
        if(!s.B("ra.enabled") || !s.B("ra.showTileBadge") || w < 70) return;
        const RaGame *g = this->online.RaGameFor(e.id, e.name);
        if(!g || g->max <= 0) return;
        const int got = s.S("ra.pointsType") == "hardcore" ? g->gotHard : g->got;
        const double pct = std::clamp(static_cast<double>(got) / g->max, 0.0, 1.0);
        const bool master = got >= g->max;
        const Col gold{ 255, 212, 71, 255 };
        const Col fill = master ? gold : this->Accent();
        const auto style = s.S("ra.badgeStyle");
        const double x0 = xf.cx - w / 2, y1 = xf.cy + h / 2;
        if(style == "bar") {
            const double bw = w - 20, bx = x0 + 10, by = y1 - 12;
            this->g->RoundRect(bx, by, bw, 5, 2.5, Col{ 0, 0, 0, static_cast<uint8_t>(150 * op) });
            if(pct > 0) this->g->RoundRect(bx, by, std::max(5.0, bw * pct), 5, 2.5, WithA(fill, op));
        }
        else {
            const std::string t = style == "percent" ? std::to_string(static_cast<int>(std::lround(pct * 100))) + " %" : std::to_string(got) + "/" + std::to_string(g->max);
            const int tw = this->g->TextW(t, 12, true);
            const double iw = style == "trophy" ? 16 : 0;
            const double bw = tw + 14 + iw, bx = xf.cx + w / 2 - bw - 8, by = y1 - 30;
            this->g->RoundRect(bx, by, bw, 22, 11, Col{ 0, 0, 0, static_cast<uint8_t>(170 * op) });
            if(iw > 0) this->g->Icon("trophy", bx + 13, by + 11, 13, WithA(gold, op), true);
            this->g->Text(t, 12, bx + 7 + iw, by + 3, WithA(master ? gold : Col{ 255, 255, 255, 255 }, op), 0, 0, true);
        }
    }

    void Home::DrawRaUnderLogo(const Entry &e, double x, double y, double w, double alpha) {
        const auto &s = this->d->settings;
        if(!s.B("ra.enabled") || !s.B("ra.showUnderLogo")) return;
        const RaGame *g = this->online.RaGameFor(e.id, e.name);
        if(!g || g->max <= 0) return;
        const int got = s.S("ra.pointsType") == "hardcore" ? g->gotHard : g->got;
        const double pct = std::clamp(static_cast<double>(got) / g->max, 0.0, 1.0);
        const bool master = got >= g->max;
        const Col gold{ 255, 212, 71, 255 };
        const std::string t = std::to_string(got) + " / " + std::to_string(g->max) + " Erfolge · " + std::to_string(static_cast<int>(std::lround(pct * 100))) + " %";
        const double bw = std::clamp(w, 220.0, 420.0);
        this->g->Icon("trophy", x + 9, y + 10, 16, WithA(gold, alpha), true);
        this->g->Text(t, 15, x + 24, y + 1, WithA(Col{ 255, 255, 255, 255 }, alpha), 0, static_cast<int>(bw - 24), true);
        this->g->RoundRect(x, y + 26, bw, 5, 2.5, Col{ 0, 0, 0, static_cast<uint8_t>(140 * alpha) });
        if(pct > 0) {
            if(master) this->g->RoundRect(x, y + 26, bw * pct, 5, 2.5, WithA(gold, alpha));
            else this->g->RectFn(x, y + 26, std::max(5.0, bw * pct), 5, this->AccentFn(x, y + 26, bw, 5), alpha);
        }
    }

    void Home::OpenRaAssign(const std::string &id) {
        auto dl = std::make_shared<Dialog>();
        dl->title = "RetroAchievements-Spiel zuordnen";
        dl->sub = "Deine gespielten Spiele, zuletzt gespielte zuerst. Nicht dabei? Spiel-ID eingeben (Zahl in der Spiel-URL).";
        dl->w = 640; dl->x = 320; dl->y = 74; dl->shade = true;
        dl->h = ScreenH - dl->y - this->Env().botH - 8;
        auto games = std::make_shared<std::vector<RaGame>>(this->online.ra.games);
        std::stable_sort(games->begin(), games->end(), [](const RaGame &a, const RaGame &b) { return a.lastDate > b.lastDate; });
        dl->rows = [this, id, games]() {
            std::vector<Row> rows;
            rows.push_back(Row{ "Spiel-ID eingeben …", nullptr, nullptr, [this, id]() {
                this->AskText("RetroAchievements-Spiel-ID", "", 8, [this, id](std::string v) {
                    const long long gid = std::atoll(v.c_str());
                    if(gid <= 0) { this->Toast("Ungültige Spiel-ID."); return; }
                    this->d->library.SetRaGameId(id, gid);
                    this->MarkDirty();
                    this->CloseAll();
                    this->Toast("Zugeordnet (ID " + std::to_string(gid) + ").");
                });
            } });
            if(games->empty()) rows.push_back(Row{ "Noch keine Spiele geladen – erst aktualisieren.", nullptr, nullptr, nullptr });
            for(const auto &g: *games) {
                const long long gid = g.id;
                const std::string title = g.title;
                rows.push_back(Row{ g.title, [g]() { return g.console; }, nullptr, [this, id, gid, title]() {
                    this->d->library.SetRaGameId(id, gid);
                    this->MarkDirty();
                    this->CloseAll();
                    this->Toast("Zugeordnet: " + title);
                } });
            }
            return rows;
        };
        this->Push(dl);
    }

    void Home::OpenRaDialog(const std::string &id, const std::string &name) {
        if(!this->online.RaReady()) {
            this->Toast("RetroAchievements: Benutzername und Web-API-Key fehlen (Einstellungen → RetroAchievements).");
            this->OpenSettings("ra");
            return;
        }
        struct St { bool loading = false; std::string err; RaGame game; std::vector<RaAchievement> list; long long gid = 0; };
        auto st = std::make_shared<St>();
        const RaGame *cur = this->online.RaGameFor(id, name);
        st->gid = this->d->library.RaGameId(id);
        if(st->gid <= 0 && cur) st->gid = cur->id;
        if(cur) st->game = *cur;
        auto dl = std::make_shared<Dialog>();
        dl->title = "RetroAchievements · " + name;
        dl->w = 640; dl->x = this->DialogX(640); dl->y = 74; dl->shade = true;
        dl->h = ScreenH - dl->y - this->Env().botH - 8;
        dl->rows = [this, st, id]() {
            std::vector<Row> rows;
            const bool fixed = this->d->library.RaGameId(id) > 0;
            if(st->gid <= 0) rows.push_back(Row{ "Diesem Titel ist kein RetroAchievements-Spiel zugeordnet.", nullptr, nullptr, nullptr });
            else {
                const auto &g = st->game;
                const int got = this->d->settings.S("ra.pointsType") == "hardcore" ? g.gotHard : g.got;
                rows.push_back(Row{ g.title.empty() ? "Spiel " + std::to_string(st->gid) : g.title, [g]() { return g.console; }, nullptr, nullptr });
                if(g.max > 0) rows.push_back(Row{ "Fortschritt", [got, g]() { return std::to_string(got) + " / " + std::to_string(g.max) + " · " + std::to_string(got * 100 / g.max) + " %"; }, nullptr, nullptr });
                rows.push_back(Row{ "Zuordnung", [fixed]() { return fixed ? std::string("fest") : std::string("automatisch (Titelvergleich)"); }, nullptr, nullptr });
            }
            rows.push_back(Row{ "Anderes Spiel zuordnen …", nullptr, nullptr, [this, id]() { this->OpenRaAssign(id); } });
            if(fixed) rows.push_back(Row{ "Feste Zuordnung entfernen", nullptr, nullptr, [this, id]() { this->d->library.SetRaGameId(id, 0); this->MarkDirty(); this->Pop(); this->Toast("Zuordnung entfernt."); } });
            if(st->gid > 0) {
                rows.push_back(Row{ "Erfolge", nullptr, nullptr, nullptr, true });
                if(st->loading) rows.push_back(Row{ "Lädt …", nullptr, nullptr, nullptr });
                else if(!st->err.empty()) rows.push_back(Row{ st->err, nullptr, nullptr, nullptr });
                for(const auto &a: st->list) {
                    const RaAchievement ach = a;
                    rows.push_back(Row{ (a.unlocked ? "+ " : "   ") + a.title, [ach]() { return (ach.unlocked ? std::string(ach.hardcore ? "Hardcore · " : "erhalten · ") : std::string()) + std::to_string(ach.points) + " P."; }, nullptr, nullptr });
                }
            }
            return rows;
        };
        this->Push(dl);
        if(st->gid > 0) {
            st->loading = true;
            this->online.RaLoadGame(st->gid, [st](RaGame g, std::vector<RaAchievement> list, std::string err) {
                st->loading = false;
                st->err = err;
                if(err.empty()) { st->game = g; st->list = std::move(list); }
            });
        }
    }

}
