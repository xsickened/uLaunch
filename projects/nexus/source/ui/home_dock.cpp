// NEXUS HOME: Systemleiste wie im HOME-Menü (Album, Controller, Einstellungen, Ein/Aus …)
#include "home.hpp"
#include <algorithm>
#include <cmath>

namespace nexus {

    namespace {

        struct DockDef { const char *id, *key, *label, *icon; Col tint; };
        // Reihenfolge wie im HOME-Menü; "Original"-Farben je Knopf
        const DockDef DockDefs[] = {
            { "album", "dock.btnAlbum", "Album", "album", Col{ 30, 120, 224, 255 } },
            { "controller", "dock.btnController", "Controller", "controller", Col{ 70, 76, 88, 255 } },
            { "settings", "dock.btnSettings", "Systemeinstellungen", "settings", Col{ 70, 76, 88, 255 } },
            { "nexus", "dock.btnNexus", "NEXUS-Einstellungen", "sliders", Col{ 0, 0, 0, 0 } },        // a = 0: Akzentfarbe
            { "user", "dock.btnUser", "Benutzerseite", "user", Col{ 236, 120, 32, 255 } },
            { "mii", "dock.btnMii", "Mii", "mii", Col{ 52, 168, 83, 255 } },
            { "web", "dock.btnWeb", "Internet", "web", Col{ 14, 150, 210, 255 } },
            { "homebrew", "dock.btnHomebrew", "Homebrew", "homebrew", Col{ 139, 92, 246, 255 } },
            { "search", "dock.btnSearch", "Suche", "search", Col{ 70, 76, 88, 255 } },
            { "power", "dock.btnPower", "Standby-Modus", "power", Col{ 70, 76, 88, 255 } },
        };

        double Smooth(double cur, double target, double dt, double speed) { return cur + (target - cur) * std::min(1.0, dt * speed); }

    }

    std::string Home::DockSide() const {
        const auto p = this->d->settings.S("dock.position");
        if(p == "top" || p == "left" || p == "right") return p;
        return "bottom";
    }

    double Home::DockBand() const {
        const auto &s = this->d->settings;
        if(!s.B("dock.visible")) return 0;
        int n = 0;
        for(const auto &d: DockDefs) if(s.B(d.key)) n++;
        if(n == 0) return 0;
        const double size = std::clamp(s.N("dock.size"), 20.0, 140.0) * std::max(1.0, s.N("dock.focusScale"));
        const auto side = this->DockSide();
        const bool vert = side == "left" || side == "right";
        // Beschriftung braucht oben/unten Platz (waagerecht) bzw. liegt bei senkrechter Leiste daneben
        const double label = (!vert && s.S("dock.labels") != "none") ? 24 : 0;
        return size + label + 16;
    }

    void Home::LayoutDock(double dt) {
        const auto &s = this->d->settings;
        std::vector<DockBtn> old = std::move(this->dockBtns);
        this->dockBtns.clear();
        if(!s.B("dock.visible") || this->screen != Screen::Main) { this->inDock = false; return; }
        for(const auto &d: DockDefs) {
            if(!s.B(d.key)) continue;
            DockBtn b;
            b.id = d.id; b.label = d.label; b.icon = d.icon; b.tint = d.tint;
            for(const auto &o: old) if(o.id == b.id) b.anim = o.anim;
            this->dockBtns.push_back(b);
        }
        const int n = static_cast<int>(this->dockBtns.size());
        if(n == 0) { this->inDock = false; return; }
        this->dockSel = std::clamp(this->dockSel, 0, n - 1);
        if(!this->dockImgScanned) {
            this->dockImgScanned = true;
            this->dockImg.clear();
            for(const auto &d: DockDefs) {
                for(const char *ext: { ".png", ".jpg" }) {
                    const std::string p = std::string("sdmc:/nexus/dock/") + d.id + ext;
                    if(FileExists(p)) { this->dockImg[d.id] = p; break; }
                }
            }
        }
        const double size = std::clamp(s.N("dock.size"), 20.0, 140.0), gap = std::max(0.0, s.N("dock.gap"));
        const auto pos = s.S("dock.position");
        const auto side = this->DockSide();
        const bool vert = side == "left" || side == "right";
        const double total = n * size + (n - 1) * gap;
        const double band = this->DockBand();
        const LayoutEnv base = [&]() { LayoutEnv e; e.topH = s.B("topbar.visible") ? 60 : 14; e.botH = s.B("hints.visible") ? 46 : 14; return e; }();
        double cx = ScreenW / 2.0, cy = ScreenH / 2.0;
        if(side == "bottom") {
            cy = ScreenH - base.botH - band / 2 + (s.S("dock.labels") != "none" ? 10 : 0);
            if(pos == "bottomLeft") cx = 40 + total / 2;
            else if(pos == "bottomRight") cx = ScreenW - 40 - total / 2;
        }
        else if(side == "top") cy = base.topH + band / 2 - (s.S("dock.labels") != "none" ? 10 : 0);
        else if(side == "left") cx = 24 + band / 2;
        else cx = ScreenW - 24 - band / 2;
        cx += s.N("dock.offsetX");
        cy += s.N("dock.offsetY");
        for(int i = 0; i < n; i++) {
            auto &b = this->dockBtns[i];
            const double o = -total / 2 + size / 2 + i * (size + gap);
            b.x = vert ? cx : cx + o;
            b.y = vert ? cy + o : cy;
            b.r = size / 2;
            const bool sel = this->inDock && i == this->dockSel;
            b.anim = Smooth(b.anim, sel ? 1.0 : 0.0, dt, 14);
        }
    }

    void Home::DrawDock() {
        this->LayoutDock(this->frameDt);
        if(this->dockBtns.empty()) return;
        const auto &s = this->d->settings;
        const auto shape = s.S("dock.shape"), style = s.S("dock.style"), iconMode = s.S("dock.iconColors"), labels = s.S("dock.labels");
        const auto side = this->DockSide();
        const bool vert = side == "left" || side == "right";
        const double fs = std::max(1.0, s.N("dock.focusScale"));
        const Col btnCol = ParseCol(s.S("dock.btnColor"));
        const double btnA = std::clamp(s.N("dock.btnOpacity"), 0.0, 1.0);
        const Col labCol = ParseCol(s.S("dock.labelColor"));
        // Leiste dahinter
        if(s.B("dock.bar")) {
            const auto &f = this->dockBtns.front(), &l = this->dockBtns.back();
            const double pad = 12, r = f.r * fs;
            const double x0 = std::min(f.x, l.x) - r - pad, x1 = std::max(f.x, l.x) + r + pad;
            const double y0 = std::min(f.y, l.y) - r - pad, y1 = std::max(f.y, l.y) + r + pad;
            this->g->RoundRect(x0, y0, x1 - x0, y1 - y0, std::min(x1 - x0, y1 - y0) / 2, WithA(ParseCol(s.S("dock.barColor")), std::clamp(s.N("dock.barOpacity"), 0.0, 1.0)));
        }
        for(size_t i = 0; i < this->dockBtns.size(); i++) {
            const auto &b = this->dockBtns[i];
            const double sc = 1 + (fs - 1) * b.anim;
            const double r = b.r * sc;
            Xf xf; xf.cx = b.x; xf.cy = b.y; xf.w = xf.h = 2 * r;
            const bool hex = shape == "hex";
            const double rad = shape == "circle" ? r : shape == "rounded" ? r * 0.42 : shape == "square" ? 4 : 0;
            // Symbolfarbe
            Col ic = b.tint;
            if(iconMode == "accent" || ic.a == 0) ic = this->Accent();
            if(iconMode == "custom") ic = ParseCol(s.S("dock.iconColor"));
            if(shape == "none" && iconMode == "original" && b.tint.a == 0) ic = this->Accent();
            if(shape != "none") {
                if(s.B("dock.shadow")) {
                    Xf sh = xf; sh.cy += 3; sh.w += 4; sh.h += 4;
                    this->g->Shape(sh, rad + 2, hex, Col{ 0, 0, 0, static_cast<uint8_t>(70 * btnA) });
                }
                if(style == "filled") this->g->Shape(xf, rad, hex, WithA(btnCol, btnA));
                else if(style == "glass") {
                    this->g->Shape(xf, rad, hex, WithA(btnCol, 0.22 * btnA));
                    this->g->ShapeRingFn(xf, rad, hex, 1.5, [](float, float) { return Col{ 255, 255, 255, 255 }; }, 0.55 * btnA);
                }
                else this->g->ShapeRingFn(xf, rad, hex, std::max(2.0, r * 0.07), [btnCol](float, float) { return btnCol; }, btnA);
                // Glas/Gefüllt: Symbol bei gefülltem hellen Knopf in Originalfarbe, sonst gut lesbar
                if(style != "filled" && iconMode == "original") ic = btnCol;
            }
            auto img = this->dockImg.find(b.id);
            SDL_Texture *tex = img != this->dockImg.end() ? this->img->Get(img->second, 0) : nullptr;
            if(tex) {
                Xf ix = xf;
                if(shape != "none") { ix.w = ix.h = 2 * r * 0.86; }
                this->g->ShapeTex(tex, ix, shape == "none" ? 0 : rad * 0.86, hex, 1.0, true, 50, 50);
            }
            else this->g->Icon(b.icon, b.x, b.y, r * (shape == "none" ? 1.5 : 1.05), ic, true);
            // Auswahl: gleiche Hervorhebung wie bei den Kacheln
            if(this->inDock && static_cast<int>(i) == this->dockSel) {
                Xf hx = xf;
                if(shape == "none") { hx.w = hx.h = 2 * r * 1.15; }
                this->DrawHighlight(hx, shape == "none" ? r * 1.15 : rad, hex);
            }
            // Beschriftung
            const bool showLab = labels == "always" || (labels == "focus" && this->inDock && static_cast<int>(i) == this->dockSel);
            if(showLab) {
                const double la = labels == "always" ? 1.0 : std::clamp(b.anim * 1.4, 0.0, 1.0);
                const Col lc = WithA(labCol, la);
                if(side == "left") this->g->Text(b.label, 15, b.x + r + 12, b.y - 9, lc, 0, 0, true);
                else if(side == "right") this->g->Text(b.label, 15, b.x - r - 12, b.y - 9, lc, 2, 0, true);
                else if(side == "top") this->g->Text(b.label, 15, b.x, b.y + r + 6, lc, 1, 0, true);
                else this->g->Text(b.label, 15, b.x, b.y - r - 24, lc, 1, 0, true);
            }
            (void)vert;
        }
    }

    bool Home::DockEnter(int dx, int dy) {
        if(this->dockBtns.empty()) return false;
        const auto side = this->DockSide();
        const bool ok = (side == "bottom" && dy > 0) || (side == "top" && dy < 0) || (side == "left" && dx < 0) || (side == "right" && dx > 0);
        if(!ok) return false;
        // Knopf, der der gewählten Kachel am nächsten liegt
        int best = 0;
        if(!this->lay.items.empty() && this->focus >= 0 && this->focus < static_cast<int>(this->lay.items.size())) {
            const auto &it = this->lay.items[this->focus];
            const bool vert = side == "left" || side == "right";
            double bd = 1e9;
            for(size_t i = 0; i < this->dockBtns.size(); i++) {
                const double d = vert ? std::fabs(this->dockBtns[i].y - it.y) : std::fabs(this->dockBtns[i].x - it.x);
                if(d < bd) { bd = d; best = static_cast<int>(i); }
            }
        }
        this->inDock = true;
        this->dockSel = best;
        this->Sfx("move");
        return true;
    }

    bool Home::DockPress(uint32_t b) {
        using namespace plat;
        const int n = static_cast<int>(this->dockBtns.size());
        if(n == 0) { this->inDock = false; return false; }
        const auto side = this->DockSide();
        const bool vert = side == "left" || side == "right";
        const uint32_t prev = vert ? BtnUp : BtnLeft, next = vert ? BtnDown : BtnRight;
        const uint32_t back = side == "bottom" ? BtnUp : side == "top" ? BtnDown : side == "left" ? BtnRight : BtnLeft;
        if(b == prev || b == next) {
            const int j = std::clamp(this->dockSel + (b == prev ? -1 : 1), 0, n - 1);
            if(j != this->dockSel) { this->dockSel = j; this->Sfx("move"); }
            return true;
        }
        if(b == back || b == BtnB) { this->inDock = false; this->Sfx(b == BtnB ? "back" : "move"); return true; }
        if(b == BtnUp || b == BtnDown || b == BtnLeft || b == BtnRight) return true;   // vom Bildschirmrand weg: nichts
        if(b == BtnA) { this->Sfx("select"); this->DockActivate(this->dockBtns[this->dockSel].id); return true; }
        if(b == BtnX || b == BtnY) return true;   // Kachel-Optionen gelten hier nicht
        return false;   // L/R, +, −, ZR usw. wie gewohnt
    }

    void Home::DockActivate(const std::string &id) {
        if(id == "nexus") { this->OpenSettings(); return; }
        if(id == "search") { this->OpenSearch(); return; }
        if(!this->host) { this->Toast("Nur im HOME-Menü (uLaunch) verfügbar."); return; }
        const auto acts = this->host->SystemActions();
        auto run = [this](const SysAction &act) {
            if(act.confirm.empty()) { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); }
            else this->Confirm(act.label + "?", act.confirm, act.label, [this, act]() { this->CloseAll(); this->Save(); this->host->BeforeLeave(); act.act(); });
        };
        if(id == "power") {
            auto dl = std::make_shared<Dialog>();
            dl->title = "Ein/Aus";
            dl->w = 380; dl->x = (ScreenW - 380) / 2; dl->y = 200; dl->shade = true;
            dl->rows = [acts, run]() {
                std::vector<Row> rows;
                for(const auto &a: acts) {
                    if(a.id != "sleep" && a.id != "reboot" && a.id != "shutdown") continue;
                    const SysAction act = a;
                    rows.push_back(Row{ a.id == "sleep" ? "Standby-Modus" : a.label, nullptr, nullptr, [run, act]() { run(act); } });
                }
                return rows;
            };
            this->Push(dl);
            return;
        }
        for(const auto &a: acts) if(a.id == id) { run(a); return; }
        this->Toast("Diese Funktion ist hier nicht verfügbar.");
    }

}
