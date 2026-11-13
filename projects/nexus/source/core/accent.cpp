#include "accent.hpp"
#include <algorithm>
#include <cmath>

namespace nexus {

    namespace {

        constexpr double Pi = 3.14159265358979323846;

        double Frac(double x) { return x - std::floor(x); }
        // Dreieckswelle: 0 -> 0, 0.5 -> 1, 1 -> 0 (nahtlos wiederholbar)
        double Tri(double x) { return 1.0 - std::fabs(2.0 * Frac(x) - 1.0); }

        Rgb Mix(const Rgb &a, const Rgb &b, double t) {
            t = std::clamp(t, 0.0, 1.0);
            return Rgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
        }

        double Hue(const Rgb &c) {
            const double r = c.r / 255, g = c.g / 255, b = c.b / 255;
            const double mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
            if(d < 1e-9) return 0;
            double h;
            if(mx == r) h = std::fmod((g - b) / d, 6.0);
            else if(mx == g) h = (b - r) / d + 2;
            else h = (r - g) / d + 4;
            h *= 60;
            return h < 0 ? h + 360 : h;
        }

        Rgb Hsv(double h, double s, double v) {
            h = Frac(h / 360.0) * 6.0;
            const int i = static_cast<int>(h) % 6;
            const double f = h - std::floor(h);
            const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
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

    }

    Rgb HexRgb(const std::string &hex) {
        if(hex.size() != 7 || hex[0] != '#') return Rgb{ 62, 224, 200 };
        auto hx = [&](size_t i) -> double {
            int v = 0;
            for(size_t k = i; k < i + 2; k++) {
                const char c = hex[k];
                v *= 16;
                if(c >= '0' && c <= '9') v += c - '0';
                else if(c >= 'a' && c <= 'f') v += c - 'a' + 10;
                else if(c >= 'A' && c <= 'F') v += c - 'A' + 10;
            }
            return v;
        };
        return Rgb{ hx(1), hx(3), hx(5) };
    }

    AccentParams AccentFrom(const Settings &s) {
        AccentParams p;
        p.mode = s.S("theme.accentMode");
        p.anim = s.S("theme.accentAnim");
        p.c1 = HexRgb(s.S("theme.accent"));
        p.c2 = HexRgb(s.S("theme.accent2"));
        p.c3 = HexRgb(s.S("theme.accent3"));
        p.angle = s.N("theme.accentAngle");
        p.cycle = std::max(0.5, s.N("theme.accentCycle"));
        p.sat = s.N("theme.rainbowSat");
        p.repeat = s.N("theme.rainbowRepeat");
        if(p.mode != "gradient" && p.mode != "gradient3" && p.mode != "rainbow") p.mode = "solid";
        return p;
    }

    double AccentU(const AccentParams &p, double x, double y, double w, double h) {
        const double a = p.angle * Pi / 180.0, c = std::cos(a), s = std::sin(a);
        const double norm = std::max(1e-6, std::fabs(c) + std::fabs(s));
        const double u = 0.5 + ((x / std::max(1.0, w) - 0.5) * c + (y / std::max(1.0, h) - 0.5) * s) / norm;
        return std::clamp(u, 0.0, 1.0);
    }

    Rgb AccentAt(const AccentParams &p, double u, double t) {
        if(!std::isfinite(u)) u = 0;
        if(!std::isfinite(t)) t = 0;
        const bool moving = p.anim == "flow" || p.anim == "cycle";
        const double phase = moving ? t / p.cycle : 0.0;
        const double uu = p.anim == "cycle" ? 0.0 : u;
        Rgb c = p.c1;
        if(p.mode == "gradient" || p.mode == "gradient3") {
            double k;
            if(p.anim == "cycle") k = Tri(phase);
            else k = Tri((uu + phase) / 2.0);   // ohne Bewegung: u = 0 -> Farbe 1, u = 1 -> letzte Farbe
            if(p.mode == "gradient") c = Mix(p.c1, p.c2, k);
            else c = k < 0.5 ? Mix(p.c1, p.c2, k * 2) : Mix(p.c2, p.c3, (k - 0.5) * 2);
        }
        else if(p.mode == "rainbow") {
            const double h = Hue(p.c1) + 360.0 * (uu * p.repeat + phase);
            c = Hsv(h, std::clamp(p.sat, 0.0, 1.0), 1.0);
        }
        if(p.anim == "pulse") {
            const double f = 0.65 + 0.35 * (0.5 + 0.5 * std::sin(2 * Pi * t / p.cycle));
            c = Rgb{ c.r * f, c.g * f, c.b * f };
        }
        c.r = std::clamp(c.r, 0.0, 255.0); c.g = std::clamp(c.g, 0.0, 255.0); c.b = std::clamp(c.b, 0.0, 255.0);
        return c;
    }

}
