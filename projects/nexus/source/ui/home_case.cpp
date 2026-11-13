// NEXUS HOME: 3D-Hüllen für Kacheln, Startanimationen, Zoom der Startseite
#include "home.hpp"
#include <algorithm>
#include <cmath>

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

        Col Shade(Col c, double k, double op) {
            return Col{ static_cast<uint8_t>(std::clamp(c.r * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.g * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.b * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.a * op, 0.0, 255.0)) };
        }
    }

    // ------------------------------------------------------------------ 3D-Hüllen

    void Home::DrawCase(const std::string &style, const json &t, SDL_Texture *tex, Xf &xf, const Disp &ds, double op, double rad, bool cover, double posX, double posY, double zoom) {
        const bool flat = std::abs(xf.rot) > 0.5;   // gedrehte Anordnungen (Halbkreis): ohne Seitenfläche
        const double signedA = std::clamp(ds.caseInit ? ds.caseA : JNum(t, "caseAngle", 18), -60.0, 60.0);
        const double dir = signedA < 0 ? -1.0 : 1.0;   // +1: Rücken links, -1: Rücken rechts
        const double ang = std::fabs(signedA) * kPi / 180.0;
        const double depth = JNum(t, "caseDepth", 12) * xf.sc;
        const double H = xf.h * xf.sc;
        const double side = flat ? 0 : depth * std::sin(ang) + (depth > 0 ? depth * 0.18 : 0);
        Xf f = xf;
        f.squash = xf.squash * std::cos(ang);
        f.cx = xf.cx + dir * side / 2;
        const double fw = xf.w * xf.sc * f.squash;
        const double fl = f.cx - fw / 2, fr = f.cx + fw / 2, ft = xf.cy - H / 2, fb = xf.cy + H / 2;
        // Material je Hülle
        Col body{ 200, 210, 225, 70 }, spine{ 35, 38, 46, 255 }, band{ 0, 0, 0, 0 };
        double inset = 0.03, bandH = 0, crad = std::min(rad, 4.0);
        bool frame = false;
        if(style == "dvd") { body = { 14, 14, 16, 255 }; spine = { 24, 24, 28, 255 }; inset = 0.045; }
        else if(style == "red") { body = { 205, 22, 34, 255 }; spine = { 165, 14, 26, 255 }; inset = 0.04; bandH = 0.09; band = { 230, 30, 42, 255 }; }
        else if(style == "bluray") { body = { 22, 78, 180, 255 }; spine = { 16, 58, 140, 255 }; inset = 0.04; bandH = 0.11; band = { 30, 96, 210, 255 }; }
        else if(style == "steel") { body = { 160, 166, 176, 255 }; spine = { 112, 118, 128, 255 }; inset = 0.0; frame = true; crad = std::max(crad, 6.0); }
        else if(style == "cartridge") { body = { 62, 64, 72, 255 }; spine = { 44, 46, 52, 255 }; inset = 0.12; crad = std::max(rad, 10.0); }
        else frame = true;   // jewel
        // Seitenfläche (Rücken) mit Perspektive: hintere Kante etwas kürzer
        if(side > 0.5) {
            const double persp = H * 0.035 * std::sin(ang) * 2;
            const double edge = dir > 0 ? fl + 1 : fr - 1, outer = dir > 0 ? fl - side : fr + side;
            const SDL_FPoint p[4] = { { static_cast<float>(outer), static_cast<float>(ft + persp) }, { static_cast<float>(edge), static_cast<float>(ft) },
                                      { static_cast<float>(edge), static_cast<float>(fb) }, { static_cast<float>(outer), static_cast<float>(fb - persp) } };
            const Col cs[4] = { Shade(spine, 0.65, op), Shade(spine, 1.0, op), Shade(spine, 1.0, op), Shade(spine, 0.65, op) };
            this->g->Quad(p, cs);
        }
        // Hüllenkörper und Cover
        this->g->Shape(f, crad, false, WithA(body, op));
        Xf cv = f;
        cv.w = xf.w * (1 - 2 * inset);
        cv.h = xf.h * (1 - 2 * inset - bandH);
        cv.cy = xf.cy + (bandH * H) / 2;
        this->g->ShapeTex(tex, cv, std::max(0.0, crad - 2), false, op, cover, posX, posY, Col{}, zoom);
        if(bandH > 0) {
            Xf b = f;
            b.h = xf.h * bandH;
            b.cy = ft + H * bandH / 2;
            this->g->Shape(b, crad, false, WithA(band, op));
            Xf stripe = b;
            stripe.h = b.h * 0.18;
            stripe.cy = b.cy + b.h * xf.sc * 0.32;
            this->g->Shape(stripe, 0, false, Col{ 255, 255, 255, static_cast<uint8_t>(150 * op) });
        }
        if(style == "jewel" && !flat) {
            // Scharnier links auf der Vorderseite
            Xf hinge = f;
            hinge.w = xf.w * 0.06;
            hinge.cx = dir > 0 ? fl + xf.w * 0.06 * xf.sc * f.squash / 2 : fr - xf.w * 0.06 * xf.sc * f.squash / 2;
            this->g->Shape(hinge, 0, false, Col{ 20, 22, 26, static_cast<uint8_t>(170 * op) });
        }
        if(style == "cartridge") {
            // Griffmulde oben
            Xf notch = f;
            notch.w = xf.w * 0.5; notch.h = xf.h * 0.05;
            notch.cy = ft + H * 0.05;
            this->g->Shape(notch, 3, false, Col{ 30, 32, 36, static_cast<uint8_t>(255 * op) });
        }
        if(frame) this->g->ShapeRing(f, crad, false, 2 * xf.sc, Col{ 235, 240, 248, static_cast<uint8_t>((style == "steel" ? 200 : 120) * op) });
        // Glanz
        if(JBool(t, "caseShine", true) && !flat) {
            const SDL_FPoint p[4] = { { static_cast<float>(fl), static_cast<float>(ft) }, { static_cast<float>(fr), static_cast<float>(ft) },
                                      { static_cast<float>(fr), static_cast<float>(fb) }, { static_cast<float>(fl), static_cast<float>(fb) } };
            const Col cs[4] = { Col{ 255, 255, 255, static_cast<uint8_t>(70 * op) }, Col{ 255, 255, 255, static_cast<uint8_t>(18 * op) },
                                Col{ 255, 255, 255, 0 }, Col{ 255, 255, 255, static_cast<uint8_t>(10 * op) } };
            this->g->Quad(p, cs);
        }
        // Spiegelung am Boden
        if(JBool(t, "caseReflect", false) && !flat) {
            this->g->ImageEx(tex, fl, fb + 3, fw, H, op * 0.35, zoom, posX, posY, [](double, double v) { return std::max(0.0, 1.0 - v / 0.4); }, Col{}, true);
        }
        xf = f;   // Fokusrahmen und Abzeichen folgen der Vorderseite
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
