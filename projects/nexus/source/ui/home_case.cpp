// NEXUS HOME: 3D-Hüllen für Kacheln, Startanimationen, Zoom der Startseite
#include "home.hpp"
#include "case3d.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace nexus {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        double Ease(const std::string &mode, double t) {
            t = std::clamp(t, 0.0, 1.0);
            if(mode == "linear") return t;
            if(mode == "in") return t * t * t;
            if(mode == "out") return 1 - std::pow(1 - t, 3);
            if(mode == "back") { const double c = 1.70158; return 1 + (c + 1) * std::pow(t - 1, 3) + c * std::pow(t - 1, 2); }
            return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
        }

    }

    // ------------------------------------------------------------------ 3D-Hüllen (Darstellung: case3d.cpp)

    void Home::DrawCase(const std::string &style, const json &t, SDL_Texture *tex, Xf &xf, const Disp &ds, double op, double, bool, double posX, double posY, double zoom, const std::string &title, bool focused) {
        CaseInput in;
        in.style = style;
        in.fit = JBool(t, "caseFit", true);
        in.depth = JNum(t, "caseDepth", 12);
        in.angle = ds.caseInit ? ds.caseA : JNum(t, "caseAngle", 18);
        in.lean = ds.lean;
        in.lift = ds.caseInit ? ds.lift : 0;
        in.shine = JBool(t, "caseShine", true);
        in.reflect = JBool(t, "caseReflect", false);
        in.spineText = JBool(t, "caseSpineText", true);
        in.shadow = JBool(t, "shadow", true);
        in.live = this->d->settings.B("anim.enabled") && !this->d->settings.B("anim.reduce");
        in.focused = focused;
        in.time = this->time;
        in.op = op;
        in.posX = posX; in.posY = posY; in.zoom = zoom;
        in.title = title;
        const std::string sp = JStr(t, "caseSpine", "left"), al = JStr(t, "caseAlign", "bottom");
        in.spineRight = sp == "right";
        in.align = al == "top" ? -1 : al == "center" ? 0 : 1;
        in.tilt = JNum(t, "caseTilt", 0);
        in.camUp = JNum(t, "caseView", 240);
        in.persp = JNum(t, "casePerspective", 1);
        SDL_FPoint q[4];
        RenderCase(*this->g, tex, xf, in, q);
        if(focused) { for(int i = 0; i < 4; i++) this->caseQuad[i] = q[i]; this->caseQuadTime = this->time; }
    }

    // ------------------------------------------------------------------ Startanimation

    void Home::StartLaunchFx(const std::string &id, const std::string &name, std::function<void()> go) {
        this->Sfx("launch");
        const auto &s = this->d->settings;
        if(s.S("launch.anim") == "none" || !s.B("anim.enabled")) { go(); return; }
        this->CloseAll();
        this->launchFx.active = true;
        this->launchFx.t = 0;
        this->launchFx.id = id;
        this->launchFx.name = name;
        this->launchFx.go = std::move(go);
        this->launchFx.haveFrom = this->haveLastFocus;
        this->launchFx.from = this->lastFocusXf;
    }

    void Home::DrawLaunchFx() {
        const auto &s = this->d->settings;
        const auto anim = s.S("launch.anim");
        const double dur = std::max(0.05, s.N("launch.durationMs") / 1000.0);
        const double p = Ease(s.S("launch.easing"), this->launchFx.t / dur);
        const double pl = std::clamp(this->launchFx.t / dur, 0.0, 1.0);
        const auto cm = s.S("launch.color");
        const auto &art = this->Art(this->launchFx.id);
        Col end = cm == "white" ? Col{ 255, 255, 255, 255 } : cm == "ui" ? this->UiCol() : cm == "accent" ? this->Accent() : Col{ 0, 0, 0, 255 };
        SDL_Texture *hero = (cm == "hero" && !art.hero.empty()) ? this->img->Get(art.hero, 0) : nullptr;
        auto endLayer = [&](double a, double x, double y, double w, double h) {
            if(hero) this->g->ImageEx(hero, x, y, w, h, a, 1, 50, 50);
            else this->g->Rect(x, y, w, h, WithA(end, a));
        };
        // Cover der Kachel (oder Icon)
        std::string ck = art.grid;
        if(ck.empty()) for(const auto &e: this->list) if(e.id == this->launchFx.id) ck = e.iconKey;
        SDL_Texture *cov = ck.empty() ? nullptr : this->img->Get(ck, 0);
        Xf from = this->launchFx.from;
        if(!this->launchFx.haveFrom) { from.cx = ScreenW / 2; from.cy = ScreenH / 2; from.w = 200; from.h = 280; from.sc = 1; from.squash = 1; }
        const double fw = from.w * from.sc * from.squash, fh = from.h * from.sc;
        auto lerp = [](double a, double b, double k) { return a + (b - a) * k; };
        if(anim == "fade") endLayer(p, 0, 0, ScreenW, ScreenH);
        else if(anim == "slideLeft") endLayer(1, ScreenW * (1 - p), 0, ScreenW, ScreenH);
        else if(anim == "slideRight") endLayer(1, -ScreenW * (1 - p), 0, ScreenW, ScreenH);
        else if(anim == "slideUp") endLayer(1, 0, ScreenH * (1 - p), ScreenW, ScreenH);
        else if(anim == "slideDown") endLayer(1, 0, -ScreenH * (1 - p), ScreenW, ScreenH);
        else if(anim == "iris") {
            // Kreisblende: Ring um die Kachel, der sich schließt
            const double maxR = std::sqrt(ScreenW * ScreenW + ScreenH * ScreenH);
            const double rr = std::max(0.5, maxR * (1 - p));
            Xf c; c.cx = from.cx; c.cy = from.cy; c.w = c.h = rr * 2;
            this->g->ShapeRingFn(c, rr, false, maxR * 1.2, [end](float, float) { return end; }, 1.0);
            if(p >= 0.999) endLayer(1, 0, 0, ScreenW, ScreenH);
        }
        else {
            // zoom / spin / flip: Cover wächst von der Kachel auf Vollbild
            this->g->Rect(0, 0, ScreenW, ScreenH, WithA(end, std::min(1.0, p * 0.8)));
            Xf x;
            x.cx = lerp(from.cx, ScreenW / 2, p);
            x.cy = lerp(from.cy, ScreenH / 2, p);
            const double tw = std::max(ScreenW, ScreenH * fw / std::max(1.0, fh)), th = tw * fh / std::max(1.0, fw);
            x.w = lerp(fw, tw, p);
            x.h = lerp(fh, std::max(th, ScreenH), p);
            if(anim == "spin") x.rot = 360.0 * s.N("launch.spinTurns") * p;
            if(anim == "flip") x.squash = std::max(0.02, std::fabs(std::cos(pl * kPi)));
            const bool back = anim == "flip" && pl > 0.5;
            if(cov && !back) this->g->ShapeTex(cov, x, 12 * (1 - p), false, 1.0, true, 50, 50);
            else this->g->Shape(x, 12 * (1 - p), false, end);
            if(p > 0.75) endLayer((p - 0.75) / 0.25, 0, 0, ScreenW, ScreenH);
        }
        // Logo bzw. Name groß
        if(s.B("launch.showLogo")) {
            const double a = std::clamp(pl * 2 - 0.4, 0.0, 1.0);
            SDL_Texture *lg = art.logo.empty() ? nullptr : this->img->Get(art.logo, 0);
            int tw = 0, th = 0;
            if(lg) SDL_QueryTexture(lg, nullptr, nullptr, &tw, &th);
            const double sc = 0.85 + 0.15 * p;
            if(lg && tw > 0 && th > 0) {
                const double w = 520 * sc, h = w * th / tw;
                this->g->Image(lg, ScreenW / 2 - w / 2, ScreenH / 2 - h / 2, w, h, a, true);
            }
            else {
                const Col tc = (cm == "white") ? Col{ 20, 20, 24, 255 } : Col{ 255, 255, 255, 255 };
                this->g->Text(this->launchFx.name, static_cast<int>(40 * sc), ScreenW / 2, ScreenH / 2 - 26, WithA(tc, a), 1, 1100, true);
            }
        }
    }

    // ------------------------------------------------------------------ Zoom

    uint32_t Home::ZoomBit() const {
        const auto b = this->d->settings.S("zoom.button");
        if(b == "rstick") return plat::BtnRStick;
        if(b == "lstick") return plat::BtnLStick;
        if(b == "zr") return plat::BtnZR;
        if(b == "zl") return plat::BtnZL;
        if(b == "y") return plat::BtnY;
        if(b == "minus") return plat::BtnMinus;
        return 0;
    }

    void Home::CycleZoom() {
        auto &s = this->d->settings;
        const double mn = s.N("zoom.minScale"), mx = std::max(s.N("zoom.maxScale"), mn + 0.05);
        const int steps = std::max(1, static_cast<int>(s.N("zoom.steps")));
        const double cur = this->zoomTarget > 0 ? this->zoomTarget : s.N("zoom.levelScale");
        double next = mn;
        for(int k = 0; k <= steps; k++) {
            const double lv = mn + (mx - mn) * k / steps;
            if(lv > cur + 0.01) { next = lv; break; }
        }
        this->zoomTarget = next;
        this->Sfx("toggle");
        this->Toast("Zoom " + std::to_string(static_cast<int>(std::lround(next * 100))) + " %");
    }

    void Home::UpdateZoom(double dt) {
        auto &s = this->d->settings;
        if(s.S("zoom.mode") == "hold") this->zoomTarget = this->zoomHeld ? s.N("zoom.maxScale") : s.N("zoom.minScale");
        if(this->zoomTarget < 0) { this->zoomTarget = s.N("zoom.levelScale"); return; }
        const double cur = s.N("zoom.levelScale");
        if(std::fabs(cur - this->zoomTarget) < 0.004) return;
        double nv = cur + (this->zoomTarget - cur) * std::min(1.0, dt * 10);
        if(std::fabs(nv - this->zoomTarget) < 0.01) nv = this->zoomTarget;
        s.Set("zoom.levelScale", json(std::round(nv * 1000) / 1000));
        this->layoutDirty = true;
        if(s.S("zoom.mode") == "cycle" && nv == this->zoomTarget) this->MarkDirty();
    }

}

namespace nexus {

    // ------------------------------------------------------------------ Ansichtsmodus: Hülle groß ansehen und drehen

    uint32_t Home::InspectBit() const {
        const auto b = this->d->settings.S("zoom.inspectButton");
        if(b == "lstick") return plat::BtnLStick;
        if(b == "rstick") return plat::BtnRStick;
        if(b == "zr") return plat::BtnZR;
        if(b == "zl") return plat::BtnZL;
        return 0;
    }

    void Home::OpenInspect() {
        if(this->list.empty() || this->focus < 0 || this->focus >= static_cast<int>(this->list.size())) return;
        const auto &e = this->list[this->focus];
        if(e.folder || JStr(this->Tile(e.id), "caseStyle", "none") == "none") {
            this->Toast("Nur für Kacheln als 3D-Hülle (Kachel bearbeiten → Hülle).");
            return;
        }
        this->insp = Inspect{};
        this->insp.on = true;
        this->insp.id = e.id;
        auto ds = this->disp.find(e.id);
        this->insp.yaw = ds != this->disp.end() && ds->second.caseInit ? ds->second.caseA : 25;
        this->insp.from = this->haveLastFocus ? this->lastFocusXf : Xf{};
        this->Sfx("open");
    }

    void Home::InspectInput(const plat::Input &in, double dt) {
        using namespace plat;
        auto &s = this->insp;
        if(s.closing) return;
        dt = std::clamp(dt, 0.0, 0.1);
        const uint32_t ib = this->InspectBit();
        if((in.down & BtnB) || (ib && (in.down & ib)) || (in.down & BtnPlus)) { s.closing = true; this->Sfx("back"); return; }
        bool manual = false;
        // Steuerkreuz/Stick: drehen (mit Schwung)
        const double ty = (in.held & BtnLeft) ? -220.0 : (in.held & BtnRight) ? 220.0 : 0.0;
        const double tp = (in.held & BtnUp) ? -150.0 : (in.held & BtnDown) ? 150.0 : 0.0;
        if(ty != 0) { s.vy += (ty - s.vy) * std::min(1.0, dt * 6); manual = true; }
        if(tp != 0) { s.vp += (tp - s.vp) * std::min(1.0, dt * 6); manual = true; }
        // Touch: ziehen = drehen, loslassen = weiterdrehen und auslaufen
        if(in.touching) {
            if(s.drag) {
                const double dx = in.touchX - s.lx, dy = in.touchY - s.ly;
                s.yaw += dx * 0.45;
                s.pitch = std::clamp(s.pitch + dy * 0.35, -75.0, 75.0);
                if(dt > 0) { s.vy = s.vy * 0.5 + (dx * 0.45 / dt) * 0.5; s.vp = s.vp * 0.5 + (dy * 0.35 / dt) * 0.5; }
            }
            s.drag = true; s.lx = in.touchX; s.ly = in.touchY;
            manual = true;
        }
        else s.drag = false;
        if(manual) { s.flipping = false; s.idle = 0; }
        else s.idle += dt;
        // A: umdrehen (Vorder-/Rückseite)
        if(in.down & BtnA) {
            s.flipping = true;
            // vorn sichtbar -> Rückseite, sonst wieder Vorderseite (immer in Drehrichtung weiter)
            const double r = std::remainder(s.yaw, 360.0), base = s.yaw - r;
            s.flipTo = std::fabs(r) < 90 ? base + 180 : base + (r > 0 ? 360 : 0) + 25;
            s.flipV = s.vy;
            s.vp = 0;
            this->Sfx("select");
        }
        // Y: zurück in die Ausgangslage
        if(in.down & BtnY) {
            s.flipping = true;
            s.flipTo = s.yaw - std::remainder(s.yaw, 360.0) + 25;
            s.pitch = s.pitch * 0.5;
            s.zoom = 1;
            this->Sfx("move");
        }
        // Zoom: ZR/R näher, ZL/L weiter weg
        if(in.held & (BtnZR | BtnR)) s.zoom = std::min(1.7, s.zoom * (1 + dt * 1.2));
        if(in.held & (BtnZL | BtnL)) s.zoom = std::max(0.55, s.zoom / (1 + dt * 1.2));
        if(s.flipping) {
            // Feder zum Ziel
            const double k = 60, damp = 2 * std::sqrt(k);
            s.flipV += (k * (s.flipTo - s.yaw) - damp * s.flipV) * dt;
            s.yaw += s.flipV * dt;
            s.pitch *= std::exp(-dt * 4);
            if(std::fabs(s.flipTo - s.yaw) < 0.2 && std::fabs(s.flipV) < 1) { s.yaw = s.flipTo; s.flipping = false; s.vy = 0; }
            return;
        }
        if(!s.drag) {
            s.yaw += s.vy * dt;
            s.pitch = std::clamp(s.pitch + s.vp * dt, -75.0, 75.0);
            if(ty == 0) s.vy *= std::exp(-dt * 2.2);    // Schwung läuft aus
            if(tp == 0) s.vp *= std::exp(-dt * 5.0);
            // in Ruhe langsam waagerecht ausrichten
            if(s.idle > 1.5 && tp == 0) s.pitch *= std::exp(-dt * 1.5);
        }
    }

    void Home::DrawInspect(double dt) {
        auto &s = this->insp;
        const bool live = this->d->settings.B("anim.enabled") && !this->d->settings.B("anim.reduce");
        s.t = live ? std::clamp(s.t + (s.closing ? -dt : dt) / 0.32, 0.0, 1.0) : (s.closing ? 0.0 : 1.0);
        if(s.closing && s.t <= 0) { s.on = false; return; }
        const double e = s.t * s.t * (3 - 2 * s.t);
        const Entry *ent = nullptr;
        for(const auto &x: this->list) if(x.id == s.id) { ent = &x; break; }
        if(!ent) { s.on = false; return; }
        const json t = this->Tile(ent->id);
        // Abdunkeln
        this->g->Rect(0, 0, ScreenW, ScreenH, Col{ 4, 6, 10, static_cast<uint8_t>(200 * e) });
        // Bilder: Cover vorn, Hintergrundbild (Hero) hinten
        const auto &a = this->Art(ent->id);
        double posX = JNum(t, "posX", 50), posY = JNum(t, "posY", 50), zoom = 1;
        SDL_Texture *tex = nullptr;
        if(!a.grid.empty()) {
            tex = this->img->Get(a.grid, 0);
            const Crop c = this->d->library.CropOf(a.grid);
            if(c.set) { posX = c.x; posY = c.y; zoom = c.zoom; }
        }
        else if(!ent->iconKey.empty()) tex = this->img->Get(ent->iconKey, 0);
        if(!tex) return;
        SDL_Texture *back = a.hero.empty() ? nullptr : this->img->Get(a.hero, 1);
        // Lage: von der Kachel in die Bildmitte
        const Xf &f = s.from;
        const double aspect = f.h > 0 ? f.w / f.h : 0.72;
        const double H = 470 * s.zoom, W = H * std::max(0.3, aspect);
        Xf xf;
        xf.cx = f.cx + (ScreenW / 2.0 - f.cx) * e;
        xf.cy = f.cy + (ScreenH / 2.0 - 10 - f.cy) * e;
        xf.w = (f.w * f.sc) + (W - f.w * f.sc) * e;
        xf.h = (f.h * f.sc) + (H - f.h * f.sc) * e;
        xf.sc = 1;
        // weicher Bodenschatten
        for(int k = 4; k >= 1; k--) {
            const double sw = xf.w * (0.75 + k * 0.12), sh = 14.0 + k * 8;
            this->g->RoundRect(xf.cx - sw / 2, xf.cy + xf.h / 2 + 18 - sh / 2, sw, sh, sh / 2, Col{ 0, 0, 0, static_cast<uint8_t>(40 * e) });
        }
        CaseInput in;
        in.style = JStr(t, "caseStyle", "dvd");
        in.fit = JBool(t, "caseFit", true);
        in.depth = JNum(t, "caseDepth", 12);
        in.angle = s.yaw;
        in.tilt = s.pitch;
        in.freeRot = true;
        in.shine = true;
        in.reflect = false;
        in.shadow = false;
        in.spineText = JBool(t, "caseSpineText", true);
        in.spineRight = JStr(t, "caseSpine", "left") == "right";
        in.align = 0;
        in.camUp = JNum(t, "caseView", 240) * (1 - e);
        in.persp = 1.1;
        in.live = live;
        in.time = this->time;
        in.title = ent->name;
        in.posX = posX; in.posY = posY; in.zoom = zoom;
        in.backTex = back;
        RenderCase(*this->g, tex, xf, in);
        // Name und Bedienhinweis
        const Col tc = WithA(Col{ 238, 242, 247, 255 }, e);
        this->g->Text(ent->name, 26, ScreenW / 2, 34, tc, 1, 900, true);
        this->g->RoundRect(ScreenW / 2 - 450, ScreenH - 58, 900, 38, 19, Col{ 0, 0, 0, static_cast<uint8_t>(170 * e) });
        this->g->Text("Steuerkreuz / Stick / Touch: drehen   ·   A: umdrehen   ·   ZL/ZR: Zoom   ·   Y: zurücksetzen   ·   B: schließen", 14, ScreenW / 2, ScreenH - 48, WithA(Col{ 238, 242, 247, 255 }, e), 1, 880);
    }

}
