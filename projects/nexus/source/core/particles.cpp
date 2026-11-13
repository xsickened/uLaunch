#include "particles.hpp"
#include <algorithm>
#include <cmath>

namespace nexus {

    namespace {
        constexpr double Pi = 3.14159265358979323846;

        Rgb HsvRgb(double h, double s, double v) {
            h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 60.0;
            const int i = static_cast<int>(h) % 6;
            const double f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
            double r, g, b;
            switch(i) {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }
            return Rgb{ r * 255, g * 255, b * 255 };
        }

        uint8_t U8(double v) { return static_cast<uint8_t>(std::clamp(v, 0.0, 255.0)); }
        uint8_t FadeCode(const std::string &m) { return m == "fade" ? 1 : m == "shrink" ? 2 : m == "none" ? 3 : 0; }
    }

    AuraCfg AuraFrom(const Settings &s) {
        AuraCfg c;
        c.enabled = s.B("aura.enabled");
        c.target = s.S("aura.target");
        c.shape = s.S("aura.shape");
        c.additive = s.S("aura.blend") == "add";
        c.colorMode = s.S("aura.colorMode");
        c.color = HexRgb(s.S("aura.color"));
        c.direction = s.S("aura.direction");
        c.emit = s.S("aura.emit");
        c.spread = s.N("aura.dirSpread");
        c.density = s.N("aura.density");
        c.velocity = s.N("aura.velocity");
        c.size = s.N("aura.particleSize");
        c.sizeVar = s.N("aura.sizeVar");
        c.lifetime = std::max(0.1, s.N("aura.lifetime"));
        c.opacity = s.N("aura.opacity");
        c.gravity = s.N("aura.gravity");
        c.spin = s.B("aura.spin");
        c.fadeMode = s.S("aura.fadeMode");
        c.front = s.S("aura.layer") == "front";
        return c;
    }

    BgCfg BgFrom(const Settings &s) {
        BgCfg c;
        c.type = s.S("bgfx.type");
        c.additive = s.S("bgfx.blend") == "add";
        c.colorMode = s.S("bgfx.colorMode");
        c.color = HexRgb(s.S("bgfx.color"));
        c.density = s.N("bgfx.density");
        c.speed = s.N("bgfx.speedScale");
        c.size = s.N("bgfx.sizeScale");
        c.opacity = s.N("bgfx.opacity");
        c.front = s.S("bgfx.layer") == "front";
        return c;
    }

    std::string BgShape(const BgCfg &c) {
        if(c.type == "snow") return "snow";
        if(c.type == "stars") return "spark";
        if(c.type == "bubbles") return "bubble";
        if(c.type == "rain") return "line";
        if(c.type == "custom") return "custom";
        return "glow";   // Glut, Glühwürmchen
    }

    void ParticleSystem::Push(const Particle &p) {
        if(this->parts.size() >= MaxParticles) return;
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.vx) || !std::isfinite(p.vy)) return;
        this->parts.push_back(p);
    }

    void ParticleSystem::SpawnAura(const AuraCfg &c, const std::vector<EmitRect> &rects, double dt, double time, const ColorFn &accent) {
        if(!c.enabled || rects.empty() || dt <= 0) { this->auraAcc = 0; return; }
        const double perRect = c.density * (rects.size() > 1 ? 0.4 : 1.0);
        this->auraAcc += perRect * rects.size() * std::min(dt, 0.1);
        int n = static_cast<int>(this->auraAcc);
        this->auraAcc -= n;
        n = std::min(n, 60);
        for(int k = 0; k < n; k++) {
            const EmitRect &R = rects[static_cast<size_t>(this->Rand(0, static_cast<double>(rects.size()) - 1e-9))];
            const double hw = std::max(1.0, R.w / 2), hh = std::max(1.0, R.h / 2);
            double px = R.cx, py = R.cy, nx = 0, ny = -1;
            if(c.emit == "corners") {
                const int q = static_cast<int>(this->Rand(0, 4 - 1e-9));
                const double sx = (q == 0 || q == 3) ? -1 : 1, sy = q < 2 ? -1 : 1;
                px = R.cx + sx * hw; py = R.cy + sy * hh;
                nx = sx * 0.7071; ny = sy * 0.7071;
            }
            else if(c.emit == "inside") {
                px = R.cx + this->Rand(-hw, hw); py = R.cy + this->Rand(-hh, hh);
                const double dx = px - R.cx, dy = py - R.cy, l = std::sqrt(dx * dx + dy * dy);
                if(l > 1e-3) { nx = dx / l; ny = dy / l; }
            }
            else if(c.emit == "around") {
                const double a = this->Rand(0, 2 * Pi);
                nx = std::cos(a); ny = std::sin(a);
                px = R.cx + (hw + 10) * nx; py = R.cy + (hh + 10) * ny;
            }
            else {
                // Rand: gleichmäßig über den Umfang
                const double per = 4 * (hw + hh);
                double t = this->Rand(0, per);
                if(t < 2 * hw) { px = R.cx - hw + t; py = R.cy - hh; nx = 0; ny = -1; }
                else if((t -= 2 * hw) < 2 * hh) { px = R.cx + hw; py = R.cy - hh + t; nx = 1; ny = 0; }
                else if((t -= 2 * hh) < 2 * hw) { px = R.cx + hw - t; py = R.cy + hh; nx = 0; ny = 1; }
                else { t -= 2 * hw; px = R.cx - hw; py = R.cy + hh - t; nx = -1; ny = 0; }
            }
            double dx = nx, dy = ny;
            if(c.direction == "up") { dx = 0; dy = -1; }
            else if(c.direction == "down") { dx = 0; dy = 1; }
            else if(c.direction == "left") { dx = -1; dy = 0; }
            else if(c.direction == "right") { dx = 1; dy = 0; }
            else if(c.direction == "orbit") { dx = -ny; dy = nx; }
            else if(c.direction == "inward") { dx = -nx; dy = -ny; }
            else if(c.direction == "random") { const double a = this->Rand(0, 2 * Pi); dx = std::cos(a); dy = std::sin(a); }
            const double sp = this->Rand(-0.5, 0.5) * c.spread * Pi / 180.0;
            const double rx = dx * std::cos(sp) - dy * std::sin(sp), ry = dx * std::sin(sp) + dy * std::cos(sp);
            Particle p;
            p.max = static_cast<float>(c.lifetime * this->Rand(0.75, 1.25));
            p.life = p.max;
            const double v = c.velocity * this->Rand(0.7, 1.3);
            if(c.direction == "inward") { px += nx * v * p.max * 0.6; py += ny * v * p.max * 0.6; }
            p.x = static_cast<float>(px); p.y = static_cast<float>(py);
            p.vx = static_cast<float>(rx * v); p.vy = static_cast<float>(ry * v);
            p.ay = static_cast<float>(c.gravity);
            p.size = static_cast<float>(std::max(1.0, c.size * (1 - c.sizeVar * this->Rand(0, 1))));
            p.rot = static_cast<float>(this->Rand(0, 2 * Pi));
            p.vr = c.spin ? static_cast<float>(this->Rand(-3, 3)) : 0.0f;
            p.opacity = static_cast<float>(c.opacity);
            p.fade = FadeCode(c.fadeMode);
            p.layer = c.front ? 1 : 0;
            Rgb col{ 255, 255, 255 };
            if(c.colorMode == "fixed") col = c.color;
            else if(c.colorMode == "rainbow") col = HsvRgb(time * 90 + this->Rand(0, 60), 0.85, 1);
            else if(c.colorMode == "random") col = HsvRgb(this->Rand(0, 360), 0.8, 1);
            else if(c.colorMode == "accent" && accent) col = accent(px, py);
            p.r = U8(col.r); p.g = U8(col.g); p.b = U8(col.b);
            this->Push(p);
        }
    }

    void ParticleSystem::SpawnBg(const BgCfg &c, double dt, double time, double w, double h, const ColorFn &accent) {
        if(c.type == "none" || dt <= 0) { this->bgAcc = 0; return; }
        this->bgAcc += c.density * std::min(dt, 0.1);
        int n = std::min(static_cast<int>(this->bgAcc), 40);
        this->bgAcc -= static_cast<int>(this->bgAcc);
        for(int k = 0; k < n; k++) {
            Particle p;
            p.layer = c.front ? 3 : 2;
            p.opacity = static_cast<float>(c.opacity);
            p.fade = 1;
            p.phase = static_cast<float>(this->Rand(0, 2 * Pi));
            const double sp = c.speed, sz = c.size;
            Rgb col{ 255, 255, 255 };
            if(c.type == "snow" || c.type == "custom") {
                p.x = static_cast<float>(this->Rand(-20, w + 20)); p.y = -20;
                p.vy = static_cast<float>(this->Rand(35, 80) * sp); p.vx = static_cast<float>(this->Rand(-10, 10) * sp);
                p.size = static_cast<float>(this->Rand(5, 14) * sz * (c.type == "custom" ? 2.2 : 1));
                p.motion = 3; p.max = static_cast<float>((h + 40) / std::max(1.0, static_cast<double>(p.vy)));
                p.vr = static_cast<float>(this->Rand(-1, 1)); p.fade = 3;
                col = Rgb{ 255, 255, 255 };
            }
            else if(c.type == "stars") {
                p.x = static_cast<float>(this->Rand(0, w)); p.y = static_cast<float>(this->Rand(0, h));
                p.size = static_cast<float>(this->Rand(6, 16) * sz); p.motion = 1; p.max = static_cast<float>(this->Rand(2, 4.5) / std::max(0.2, sp));
                col = Rgb{ 255, 244, 214 };
            }
            else if(c.type == "bubbles") {
                p.x = static_cast<float>(this->Rand(0, w)); p.y = static_cast<float>(h + 20);
                p.vy = static_cast<float>(-this->Rand(30, 70) * sp); p.size = static_cast<float>(this->Rand(10, 28) * sz);
                p.motion = 3; p.max = static_cast<float>((h + 60) / std::max(1.0, static_cast<double>(-p.vy))); p.fade = 3;
                col = Rgb{ 170, 220, 255 };
            }
            else if(c.type == "embers") {
                p.x = static_cast<float>(this->Rand(0, w)); p.y = static_cast<float>(h + 10);
                p.vy = static_cast<float>(-this->Rand(40, 110) * sp); p.vx = static_cast<float>(this->Rand(-20, 20) * sp);
                p.size = static_cast<float>(this->Rand(4, 12) * sz); p.max = static_cast<float>(this->Rand(3, 6));
                col = HsvRgb(this->Rand(8, 40), 0.9, 1);
            }
            else if(c.type == "rain") {
                p.x = static_cast<float>(this->Rand(-50, w + 50)); p.y = -30;
                p.vy = static_cast<float>(this->Rand(550, 750) * sp); p.vx = static_cast<float>(-60 * sp);
                p.size = static_cast<float>(this->Rand(16, 28) * sz); p.stretch = 1; p.max = static_cast<float>((h + 60) / std::max(1.0, static_cast<double>(p.vy))); p.fade = 3;
                col = Rgb{ 190, 210, 235 };
            }
            else { // fireflies
                p.x = static_cast<float>(this->Rand(0, w)); p.y = static_cast<float>(this->Rand(h * 0.2, h));
                p.size = static_cast<float>(this->Rand(8, 16) * sz); p.motion = 2; p.max = static_cast<float>(this->Rand(4, 8));
                col = Rgb{ 214, 255, 120 };
            }
            p.life = p.max;
            if(c.colorMode == "fixed") col = c.color;
            else if(c.colorMode == "rainbow") col = HsvRgb(time * 40 + p.x * 0.2, 0.8, 1);
            else if(c.colorMode == "accent" && accent) col = accent(p.x, p.y);
            else if(c.colorMode == "image") col = Rgb{ 255, 255, 255 };
            p.r = U8(col.r); p.g = U8(col.g); p.b = U8(col.b);
            this->Push(p);
        }
    }

    void ParticleSystem::Update(double dt, double time) {
        dt = std::clamp(dt, 0.0, 0.1);
        for(auto &p: this->parts) {
            p.life -= static_cast<float>(dt);
            if(p.motion == 2) {
                // umherschwirren
                p.vx += static_cast<float>(std::sin(time * 1.3 + p.phase * 3) * 40 * dt);
                p.vy += static_cast<float>(std::cos(time * 1.1 + p.phase * 5) * 40 * dt);
                p.vx *= 0.98f; p.vy *= 0.98f;
            }
            p.vy += static_cast<float>(p.ay * dt);
            p.x += static_cast<float>(p.vx * dt);
            p.y += static_cast<float>(p.vy * dt);
            if(p.motion == 3) p.x += static_cast<float>(std::sin(time * 1.5 + p.phase) * 18 * dt);
            p.rot += static_cast<float>(p.vr * dt);
        }
        this->parts.erase(std::remove_if(this->parts.begin(), this->parts.end(), [](const Particle &p) { return p.life <= 0 || !std::isfinite(p.x) || !std::isfinite(p.y); }), this->parts.end());
    }

    void ParticleSystem::Look(const Particle &p, double time, float &alpha, float &size) {
        const double age = std::clamp(1.0 - p.life / std::max(0.001f, p.max), 0.0, 1.0);
        double a = p.opacity * std::min(1.0, age / 0.1 + 0.05);
        double s = p.size;
        if(p.fade == 0 || p.fade == 1) a *= (1 - age);
        if(p.fade == 0 || p.fade == 2) s *= (1 - 0.7 * age);
        if(p.motion == 1) a = p.opacity * std::sin(age * Pi) * (0.6 + 0.4 * std::sin(time * 6 + p.phase));
        if(p.motion == 2) a *= 0.5 + 0.5 * std::sin(time * 3 + p.phase);
        if(p.fade == 3 && p.motion != 1) a = p.opacity * std::min(1.0, age / 0.05 + 0.05) * std::min(1.0, (1 - age) / 0.1);
        alpha = static_cast<float>(std::clamp(a, 0.0, 1.0));
        size = static_cast<float>(std::max(0.5, s));
    }

}
