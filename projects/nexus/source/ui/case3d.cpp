// NEXUS HOME: realistische 3D-Spielhüllen (Perspektive, Licht, Materialien)
#include "case3d.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace nexus {

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double ScreenW = 1280, ScreenH = 720;
    }

    //
    // Jede Hülle ist ein echter Quader mit Perspektive: eine Kamera in der Bildschirmmitte, die Vorderseite
    // liegt (ungedreht) genau auf der Kachel. Flächen werden pro Seite beleuchtet, das Cover wird über ein
    // feines Gitter perspektivisch korrekt aufgezogen. Maße und Materialien nach echten Hüllen.

    namespace {

        struct V3 { double x, y, z; };

        struct CaseModel {
            double ratio = 0.72, depth = 0.075;   // Breite/Höhe der Front, Dicke/Höhe (echte Maße in mm)
            Col shell{ 20, 20, 22, 255 };         // Kunststoff der Hülle
            Col band{ 0, 0, 0, 0 };               // Kopfleiste der Einlage (a = 0: keine)
            double bandH = 0;                     // Höhe der Kopfleiste (Anteil der Einlage)
            Col bandLine{ 0, 0, 0, 0 };           // feine Linie unter der Kopfleiste
            Col spineTop{ 0, 0, 0, 0 }, spineBody{ 20, 20, 22, 255 }, spineText{ 240, 240, 240, 255 };
            double lip = 0.022;                   // sichtbarer Kunststoffrand um die Einlage (Anteil der Breite)
            double corner = 0.035;                // Eckenradius (Anteil der Breite)
            double gloss = 0.55;                  // Stärke der Glanzlichter (Klarsichtfolie)
            bool clear = false, metal = false, cart = false, jewel = false, text = true;
        };

        CaseModel ModelFor(const std::string &st) {
            CaseModel m;
            if(st == "red" || st == "switch") {         // 105 x 170 x 11 mm, rot
                m.ratio = 105.0 / 170; m.depth = 11.0 / 170; m.shell = { 196, 14, 24, 255 };
                m.band = { 228, 0, 18, 255 }; m.bandH = 0.075; m.bandLine = { 255, 255, 255, 60 };
                m.spineTop = { 228, 0, 18, 255 }; m.spineBody = { 246, 246, 246, 255 }; m.spineText = { 30, 30, 34, 255 };
                m.lip = 0.014; m.corner = 0.05;
            }
            else if(st == "ps5") {                       // 135 x 170 x 14 mm, blau, weiße Kopfleiste
                m.ratio = 135.0 / 170; m.depth = 14.0 / 170; m.shell = { 22, 68, 150, 255 };
                m.band = { 246, 246, 248, 255 }; m.bandH = 0.08; m.bandLine = { 0, 0, 0, 40 };
                m.spineTop = { 246, 246, 248, 255 }; m.spineBody = { 20, 22, 28, 255 };
                m.lip = 0.012;
            }
            else if(st == "ps4") {                       // 135 x 170 x 14 mm, blau, blaue Kopfleiste
                m.ratio = 135.0 / 170; m.depth = 14.0 / 170; m.shell = { 24, 74, 160, 255 };
                m.band = { 0, 58, 152, 255 }; m.bandH = 0.075; m.bandLine = { 255, 255, 255, 70 };
                m.spineTop = { 0, 58, 152, 255 }; m.spineBody = { 18, 20, 26, 255 };
                m.lip = 0.012;
            }
            else if(st == "xbox") {                      // 135 x 170 x 14 mm, grün, dunkle Kopfleiste
                m.ratio = 135.0 / 170; m.depth = 14.0 / 170; m.shell = { 16, 112, 22, 255 };
                m.band = { 14, 14, 16, 255 }; m.bandH = 0.075; m.bandLine = { 16, 160, 30, 255 };
                m.spineTop = { 16, 124, 16, 255 }; m.spineBody = { 16, 16, 18, 255 };
                m.lip = 0.012;
            }
            else if(st == "bluray") {                    // 135 x 171 x 12 mm, transparent blau
                m.ratio = 135.0 / 171; m.depth = 12.0 / 171; m.shell = { 30, 92, 200, 255 };
                m.band = { 20, 84, 196, 255 }; m.bandH = 0.07; m.bandLine = { 220, 228, 240, 160 };
                m.spineTop = { 20, 84, 196, 255 }; m.spineBody = { 236, 238, 242, 255 }; m.spineText = { 24, 26, 32, 255 };
                m.lip = 0.012;
            }
            else if(st == "jewel") {                     // 142 x 125 x 10 mm, Klarsicht, schwarze Trayschale
                m.ratio = 142.0 / 125; m.depth = 10.0 / 125; m.shell = { 225, 232, 240, 70 };
                m.spineBody = { 22, 22, 26, 255 }; m.lip = 0.012; m.corner = 0.01; m.gloss = 0.9;
                m.clear = true; m.jewel = true;
            }
            else if(st == "steel") {                     // Steelbook 135 x 171 x 12 mm, Metall
                m.ratio = 135.0 / 171; m.depth = 12.0 / 171; m.shell = { 150, 156, 166, 255 };
                m.spineBody = { 158, 164, 174, 255 }; m.spineText = { 34, 36, 42, 255 };
                m.lip = 0; m.corner = 0.06; m.gloss = 0.75; m.metal = true;
            }
            else if(st == "cartridge") {                 // Modul: kompakt, dick, Etikett oben
                m.ratio = 0.86; m.depth = 0.14; m.shell = { 70, 72, 80, 255 };
                m.spineBody = { 62, 64, 72, 255 }; m.lip = 0; m.corner = 0.08; m.gloss = 0.35; m.cart = true; m.text = false;
            }
            else {                                        // dvd: 135 x 190 x 14 mm, schwarz
                m.ratio = 135.0 / 190; m.depth = 14.0 / 190; m.shell = { 16, 16, 18, 255 };
                m.spineBody = { 14, 14, 16, 255 }; m.lip = 0.014;
            }
            return m;
        }

        Col Lit(Col c, double k, double a) {
            return Col{ static_cast<uint8_t>(std::clamp(c.r * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.g * k, 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(c.b * k, 0.0, 255.0)), static_cast<uint8_t>(std::clamp(c.a * a, 0.0, 255.0)) };
        }

        std::vector<SDL_Vertex> g_V;
        std::vector<int> g_I;

    }

    void RenderCase(Gfx &gfx, SDL_Texture *tex, Xf &xf, const CaseInput &in) {
        const std::string &style = in.style, &title = in.title;
        const double op = in.op, posX = in.posX, posY = in.posY, zoom = in.zoom;
        const bool focused = in.focused;
        const CaseModel m = ModelFor(style);
        const bool live = in.live;
        // Größe: echte Proportionen in die Kachel eingepasst (oder Kachelform übernehmen)
        const double boxW = xf.w * xf.sc, boxH = xf.h * xf.sc;
        double W = boxW, H = boxH;
        if(in.fit) {
            if(boxW / std::max(1.0, boxH) > m.ratio) W = boxH * m.ratio; else H = boxW / m.ratio;
        }
        const double D = H * m.depth * std::clamp(in.depth / 12.0, 0.0, 2.5);
        // Drehung: Anordnung + Schwung + (gewählte Hülle) leichtes Wiegen in der Hand
        double yawDeg = std::clamp(in.angle + in.lean, -75.0, 75.0);
        double pitchDeg = 0;
        if(focused && live) {
            yawDeg += 2.2 * std::sin(in.time * 1.25);
            pitchDeg += 1.4 * std::sin(in.time * 0.9 + 1.0);
        }
        // Kamera etwas über der Bildmitte (wie beim Blick auf ein Regal): Oberkanten werden sichtbar
        const double F = 1050, cx0 = ScreenW / 2.0, cy0 = ScreenH / 2.0, camUp = 240;
        const V3 pos{ xf.cx - cx0, xf.cy - cy0, D / 2 + in.lift };
        // Drehung relativ zur Blickrichtung: 0° = Vorderseite schaut zur Kamera, egal wo die Hülle steht
        const double yaw = -yawDeg * kPi / 180.0 + std::atan2(pos.x, F + pos.z);
        const double pitch = pitchDeg * kPi / 180.0 - 0.35 * std::atan2(pos.y + camUp, F + pos.z);
        const double cyw = std::cos(yaw), syw = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const double rot = xf.rot * kPi / 180.0, cr = std::cos(rot), sr = std::sin(rot);
        const double sq = std::clamp(xf.squash, 0.05, 1.0);
        auto world = [&](V3 p) {
            const double x = p.x * cyw + p.z * syw, z = -p.x * syw + p.z * cyw;
            const double y = p.y * cp - z * sp, z2 = p.y * sp + z * cp;
            return V3{ x + pos.x, y + pos.y, z2 + pos.z };
        };
        auto dirW = [&](V3 n) {
            const double x = n.x * cyw + n.z * syw, z = -n.x * syw + n.z * cyw;
            return V3{ x, n.y * cp - z * sp, n.y * sp + z * cp };
        };
        auto proj = [&](V3 p) {
            const V3 w = world(p);
            const double k = F / std::max(50.0, F + w.z);
            double x = cx0 + w.x * k - xf.cx, y = cy0 + (w.y + camUp) * k - camUp - xf.cy;
            x *= sq;   // Pseudo-3D-Stauchung der Anordnung
            return SDL_FPoint{ static_cast<float>(xf.cx + x * cr - y * sr), static_cast<float>(xf.cy + x * sr + y * cr) };
        };
        // Licht von oben links vorn
        const V3 L{ -0.36, -0.68, -0.64 };
        auto shade = [&](V3 n) { const V3 w = dirW(n); return 0.5 + 0.55 * std::max(0.0, w.x * L.x + w.y * L.y + w.z * L.z); };
        auto visible = [&](V3 n, V3 c) { const V3 w = dirW(n), cw = world(c); return w.x * cw.x + w.y * (cw.y + camUp) + w.z * (cw.z + F) < 0; };
        // Gitter: pos(u,v) lokal, uv(u,v), Farbe(u,v)
        auto grid = [&](SDL_Texture *tx, int nu, int nv, const std::function<V3(double, double)> &P, const std::function<SDL_FPoint(double, double)> &UV, const std::function<Col(double, double)> &C) {
            g_V.clear(); g_I.clear();
            for(int j = 0; j <= nv; j++) for(int i = 0; i <= nu; i++) {
                const double u = static_cast<double>(i) / nu, v = static_cast<double>(j) / nv;
                const Col c = C(u, v);
                g_V.push_back(SDL_Vertex{ proj(P(u, v)), SDL_Color{ c.r, c.g, c.b, c.a }, UV ? UV(u, v) : SDL_FPoint{ 0, 0 } });
            }
            for(int j = 0; j < nv; j++) for(int i = 0; i < nu; i++) {
                const int a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d2 = c + 1;
                g_I.insert(g_I.end(), { a, b, d2, a, d2, c });
            }
            gfx.Mesh(tx, g_V, g_I);
        };
        // Rechteck auf der Vorderseite (lokal x0..x1, y0..y1) mit abgerundeten Ecken r
        auto frontRect = [&](SDL_Texture *tx, double x0, double y0, double x1, double y1, double r, double z, const std::function<SDL_FPoint(double, double)> &UV, const std::function<Col(double, double)> &C, int nu = 10, int nv = 14) {
            r = std::min({ r, (x1 - x0) / 2, (y1 - y0) / 2 });
            auto P = [&](double u, double v) {
                const double y = y0 + (y1 - y0) * v;
                double in = 0;   // Einzug an den runden Ecken
                if(r > 0) {
                    const double dy = std::min(y - y0, y1 - y);
                    if(dy < r) in = r - std::sqrt(std::max(0.0, r * r - (r - dy) * (r - dy)));
                }
                return V3{ x0 + in + (x1 - x0 - 2 * in) * u, y, z };
            };
            // u der Textur muss zur tatsächlichen Lage passen (Einzug)
            auto UV2 = [&](double u, double v) {
                const V3 p = P(u, v);
                return UV ? UV((p.x - x0) / std::max(1e-6, x1 - x0), v) : SDL_FPoint{ 0, 0 };
            };
            auto C2 = [&](double u, double v) { const V3 p = P(u, v); return C((p.x - x0) / std::max(1e-6, x1 - x0), v); };
            grid(tx, nu, nv, P, UV2, C2);
        };
        const double hw = W / 2, hh = H / 2, hd = D / 2, zf = -hd;
        // Gitterfeinheit nach Größe: kleine Hüllen brauchen weniger Dreiecke
        const int q = H > 220 ? 2 : H > 120 ? 1 : 0;
        const int artU = 6 + 2 * q, artV = 8 + 3 * q, glU = 8 + 4 * q, glV = 6 + 3 * q;
        const double opA = std::clamp(op, 0.0, 1.0);
        const double kFront = shade({ 0, 0, -1 });
        // ---------------- Schatten (weich, auf der Rückwand dahinter)
        if(in.shadow) {
            for(int k = 0; k < 4; k++) {
                const double g = 6 + k * 7.0;
                SDL_FPoint q[4] = { proj({ -hw - g * 0.4, -hh + g, hd + 30 }), proj({ hw + g * 0.6, -hh + g, hd + 30 }), proj({ hw + g * 0.6, hh + g * 1.2, hd + 30 }), proj({ -hw - g * 0.4, hh + g * 1.2, hd + 30 }) };
                const Col c{ 0, 0, 0, static_cast<uint8_t>(34 * opA) };
                const Col cs[4] = { c, c, c, c };
                gfx.Quad(q, cs);
            }
        }
        // ---------------- Seitenflächen (nur sichtbare)
        auto flat = [&](V3 n, V3 a, V3 b, V3 c, V3 d2, Col col) {
            if(!visible(n, V3{ (a.x + c.x) / 2, (a.y + c.y) / 2, (a.z + c.z) / 2 })) return;
            const double k = shade(n);
            const Col cc = Lit(col, k, opA);
            const SDL_FPoint q[4] = { proj(a), proj(b), proj(c), proj(d2) };
            const Col cs[4] = { cc, cc, Lit(col, k * 0.82, opA), Lit(col, k * 0.82, opA) };
            gfx.Quad(q, cs);
        };
        const Col shellDark = Lit(m.shell, 0.7, 1);
        // Rückseite, oben, unten, offene Seite (rechts)
        flat({ 0, 0, 1 }, { hw, -hh, hd }, { -hw, -hh, hd }, { -hw, hh, hd }, { hw, hh, hd }, shellDark);
        flat({ 0, -1, 0 }, { -hw, -hh, zf }, { hw, -hh, zf }, { hw, -hh, hd }, { -hw, -hh, hd }, m.clear ? Col{ 30, 30, 34, 255 } : m.shell);
        flat({ 0, 1, 0 }, { -hw, hh, hd }, { hw, hh, hd }, { hw, hh, zf }, { -hw, hh, zf }, m.clear ? Col{ 30, 30, 34, 255 } : shellDark);
        if(visible({ 1, 0, 0 }, { hw, 0, 0 })) {
            flat({ 1, 0, 0 }, { hw, -hh, zf }, { hw, -hh, hd }, { hw, hh, hd }, { hw, hh, zf }, m.clear ? Col{ 34, 34, 38, 255 } : m.shell);
            if(!m.metal && !m.cart) {
                // Rille der Schließkante
                const double k = shade({ 1, 0, 0 });
                const SDL_FPoint q[4] = { proj({ hw, -hh * 0.96, -hd * 0.12 }), proj({ hw, -hh * 0.96, hd * 0.12 }), proj({ hw, hh * 0.96, hd * 0.12 }), proj({ hw, hh * 0.96, -hd * 0.12 }) };
                const Col c = Lit(m.shell, k * 0.55, opA);
                const Col cs[4] = { c, c, c, c };
                gfx.Quad(q, cs);
            }
        }
        // Rücken (links) mit bedruckter Einlage und Titel
        if(visible({ -1, 0, 0 }, { -hw, 0, 0 })) {
            const double k = shade({ -1, 0, 0 });
            const double topH = m.spineTop.a ? H * m.bandH : 0;
            auto spinePart = [&](double y0, double y1, Col c) {
                grid(nullptr, 1, 4, [&](double u, double v) { return V3{ -hw, y0 + (y1 - y0) * v, zf + D * u }; }, nullptr,
                     [&](double u, double) { return Lit(c, k * (0.88 + 0.12 * std::sin(u * kPi)), opA); });
            };
            spinePart(-hh, -hh + topH, m.spineTop.a ? m.spineTop : m.spineBody);
            spinePart(-hh + topH, hh, m.spineBody);
            if(m.text && !title.empty() && in.spineText && D > 6) {
                int tw = 0, th = 0;
                SDL_Texture *tt = gfx.TextTex(title, 26, Col{ 255, 255, 255, 255 }, 0, Col{}, tw, th, true);
                if(tt && tw > 0 && th > 0) {
                    // Text läuft von oben nach unten; Höhe = Rückenbreite * 0.55
                    const double tH = D * 0.62, tL = std::min(tH * tw / th, (H - topH) * 0.82);
                    const double y0 = -hh + topH + (H - topH - tL) * 0.12;
                    const double uMax = std::min(1.0, tL / (tH * tw / th));
                    const Col tc = m.spineText;
                    grid(tt, 1, 8, [&](double u, double v) { return V3{ -hw - 0.2, y0 + tL * v, -(D * 0.62) / 2 + D * 0.62 * u }; },
                         [&](double u, double v) { return SDL_FPoint{ static_cast<float>(v * uMax), static_cast<float>(u) }; },
                         [&](double, double) { return Lit(tc, std::min(1.0, k + 0.15), opA); });
                    SDL_SetTextureColorMod(tt, 255, 255, 255);
                }
            }
            // Scharnierrillen der Hülle (vorn und hinten am Rücken)
            if(!m.metal && !m.cart) {
                for(const double gu: { 0.13, 0.87 }) {
                    grid(nullptr, 1, 1, [&](double u, double v) { return V3{ -hw - 0.25, -hh + H * v, zf + D * (gu - 0.02 + 0.04 * u) }; }, nullptr,
                         [&](double u, double) { return Col{ 0, 0, 0, static_cast<uint8_t>(opA * (u < 0.5 ? 70 : 25)) }; });
                }
            }
            // Klarsichtfolie über dem Rücken
            grid(nullptr, 1, 1, [&](double u, double v) { return V3{ -hw - 0.3, -hh + H * v, zf + D * u }; }, nullptr,
                 [&](double u, double) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 255 * m.gloss * 0.12 * (1 - u)) }; });
        }
        // ---------------- Vorderseite
        if(visible({ 0, 0, -1 }, { 0, 0, zf })) {
            const double R = W * m.corner;
            // Kunststoffkörper
            frontRect(nullptr, -hw, -hh, hw, hh, R, zf, nullptr, [&](double, double v) { return Lit(m.shell, kFront * (1.02 - 0.1 * v), opA); }, 2, 10);
            // Einlage (Cover + Kopfleiste) unter der Folie
            double ix0 = -hw + W * m.lip, ix1 = hw - W * m.lip, iy0 = -hh + W * m.lip, iy1 = hh - W * m.lip;
            if(m.cart) { ix0 = -hw + W * 0.09; ix1 = hw - W * 0.09; iy0 = -hh + H * 0.12; iy1 = hh - H * 0.26; }
            if(m.jewel) ix0 = -hw + W * 0.075;   // Scharnierleiste links
            const double bandPx = (iy1 - iy0) * m.bandH;
            const double artY0 = iy0 + (m.band.a ? bandPx : 0);
            if(m.band.a) {
                frontRect(nullptr, ix0, iy0, ix1, artY0, m.metal ? R : 0, zf - 0.1, nullptr, [&](double u, double) { return Lit(m.band, kFront * (1.0 - 0.06 * u), opA); }, 2, 2);
            }
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if(tw > 0 && th > 0) {
                double sx, sy, sw, sh;
                CoverSrc(tw, th, (ix1 - ix0) / std::max(1.0, iy1 - artY0), posX, posY, zoom, sx, sy, sw, sh);
                const double artR = m.metal ? R : m.cart ? W * 0.03 : 0;
                frontRect(tex, ix0, artY0, ix1, iy1, artR, zf - 0.1,
                          [&](double u, double v) { return SDL_FPoint{ static_cast<float>((sx + u * sw) / tw), static_cast<float>((sy + v * sh) / th) }; },
                          [&](double, double) { return Lit(Col{ 255, 255, 255, 255 }, std::min(1.0, 0.3 + kFront), opA); }, artU, artV);
            }
            if(m.band.a && m.bandLine.a) {
                const double lh = std::max(0.6, H * 0.004);
                frontRect(nullptr, ix0, artY0 - lh / 2, ix1, artY0 + lh / 2, 0, zf - 0.15, nullptr, [&](double, double) { return Lit(m.bandLine, 1, opA); }, 1, 1);
            }
            // Einlage liegt unter dem Kunststoffrand: feiner Schatten an den Kanten
            if(m.lip > 0) {
                const double e = std::max(1.0, W * 0.012);
                auto edge = [&](double x0, double y0, double x1, double y1, int dirU, int dirV) {
                    frontRect(nullptr, x0, y0, x1, y1, 0, zf - 0.2, nullptr, [&](double u, double v) {
                        const double f = dirU > 0 ? 1 - u : dirU < 0 ? u : dirV > 0 ? 1 - v : v;
                        return Col{ 0, 0, 0, static_cast<uint8_t>(opA * 90 * f) }; }, 2, 2);
                };
                edge(ix0, iy0, ix0 + e, iy1, 1, 0);
                edge(ix1 - e, iy0, ix1, iy1, -1, 0);
                edge(ix0, iy0, ix1, iy0 + e, 0, 1);
                edge(ix0, iy1 - e, ix1, iy1, 0, -1);
            }
            if(!m.metal && !m.cart && !m.jewel) {
                // Schweißkante der Klarsichtfolie: feine helle Linie knapp innerhalb des Randes
                const double e = W * 0.028, t1 = std::max(0.5, W * 0.0035);
                const Col lc{ 255, 255, 255, static_cast<uint8_t>(opA * 38) };
                auto line = [&](double x0, double y0, double x1, double y1) { frontRect(nullptr, x0, y0, x1, y1, 0, zf - 0.22, nullptr, [&](double, double) { return lc; }, 1, 1); };
                line(-hw + e, -hh + e, hw - e, -hh + e + t1);
                line(-hw + e, hh - e - t1, hw - e, hh - e);
                line(-hw + e, -hh + e, -hw + e + t1, hh - e);
                line(hw - e - t1, -hh + e, hw - e, hh - e);
            }
            if(m.jewel) {
                // Scharnierleiste mit Rippen
                frontRect(nullptr, -hw, -hh, ix0, hh, 0, zf - 0.2, nullptr, [&](double u, double) { return Lit(Col{ 30, 32, 36, 255 }, kFront * (0.8 + 0.3 * u), opA); }, 2, 2);
                for(int r = 0; r < 3; r++) {
                    const double x = -hw + (ix0 + hw) * (0.25 + r * 0.25);
                    frontRect(nullptr, x - 0.6, -hh + H * 0.02, x + 0.6, hh - H * 0.02, 0, zf - 0.25, nullptr, [&](double, double) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 60) }; }, 1, 1);
                }
            }
            if(m.cart) {
                // Griffrillen unten, Pfeil oben
                for(int r = 0; r < 5; r++) {
                    const double y = hh - H * (0.07 + r * 0.035);
                    frontRect(nullptr, -hw + W * 0.2, y - H * 0.006, hw - W * 0.2, y + H * 0.006, H * 0.006, zf - 0.2, nullptr,
                              [&](double, double v) { return Lit(m.shell, kFront * (v < 0.5 ? 0.6 : 1.25), opA); }, 2, 2);
                }
                frontRect(nullptr, -W * 0.06, -hh + H * 0.035, W * 0.06, -hh + H * 0.075, W * 0.02, zf - 0.2, nullptr,
                          [&](double, double) { return Lit(m.shell, kFront * 0.6, opA); }, 2, 3);
            }
            if(m.metal) {
                // geprägter Rahmen und gebürstetes Metall
                const double e = W * 0.03;
                frontRect(nullptr, -hw + e, -hh + e, hw - e, -hh + e + 1.2, 0, zf - 0.2, nullptr, [&](double, double) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 70) }; }, 1, 1);
                frontRect(nullptr, -hw + e, hh - e - 1.2, hw - e, hh - e, 0, zf - 0.2, nullptr, [&](double, double) { return Col{ 0, 0, 0, static_cast<uint8_t>(opA * 70) }; }, 1, 1);
            }
            // Glanz der Klarsichtfolie: wandernder Lichtstreifen (hängt von Drehung und Lage ab) + Kantenlicht
            if(in.shine) {
                const double c = 0.15 + 0.9 * std::sin(yawDeg * kPi / 180.0) + (xf.cx - cx0) / ScreenW * 0.6;
                const double gl = m.gloss * opA;
                const double angled = std::min(1.0, std::fabs(yawDeg) / 45.0);
                frontRect(nullptr, -hw, -hh, hw, hh, R, zf - 0.3, nullptr, [&](double u, double v) {
                    const double d = u + 0.45 * v - c;
                    double a = 0.32 * std::exp(-d * d / 0.008) + 0.07 * std::exp(-d * d / 0.06);   // Streifen + weicher Hof
                    a += 0.05 * std::pow((1 - v) * (1 - u), 2);    // Himmel-Reflex oben links
                    a += 0.10 * angled * std::pow(1 - u, 4);        // Fresnel an der Rückenkante
                    if(m.metal) a = a * 0.8 + 0.05 * std::sin(v * 140) * 0.5 + 0.05;   // gebürstet
                    return Col{ 255, 255, 255, static_cast<uint8_t>(std::clamp(a * gl, 0.0, 1.0) * 255) };
                }, glU, glV);
            }
            // Lichtkante oben und an der Rückenkante
            frontRect(nullptr, -hw + R, -hh, hw - R, -hh + std::max(0.8, H * 0.004), 0, zf - 0.35, nullptr,
                      [&](double, double) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 90 * m.gloss) }; }, 1, 1);
            frontRect(nullptr, -hw, -hh + R, -hw + std::max(0.8, W * 0.006), hh - R, 0, zf - 0.35, nullptr,
                      [&](double, double v) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 70 * m.gloss * (1 - v)) }; }, 1, 2);
            // Spiegelung am Boden: Vorderseite gespiegelt, ausblendend
            if(in.reflect && tw > 0 && th > 0) {
                double sx, sy, sw, sh;
                CoverSrc(tw, th, (ix1 - ix0) / std::max(1.0, iy1 - artY0), posX, posY, zoom, sx, sy, sw, sh);
                const double gap = 4;
                grid(tex, 8, 8, [&](double u, double v) { return V3{ ix0 + (ix1 - ix0) * u, hh + gap + (iy1 - artY0) * 0.45 * v, zf }; },
                     [&](double u, double v) { return SDL_FPoint{ static_cast<float>((sx + u * sw) / tw), static_cast<float>((sy + (1 - v * 0.45) * sh) / th) }; },
                     [&](double, double v) { return Col{ 255, 255, 255, static_cast<uint8_t>(opA * 90 * (1 - v) * (1 - v)) }; });
            }
        }
        // Fokusrahmen und Abzeichen folgen der projizierten Vorderseite
        const SDL_FPoint a = proj({ -hw, -hh, zf }), b = proj({ hw, -hh, zf }), c = proj({ hw, hh, zf }), d2 = proj({ -hw, hh, zf });
        const double minX = std::min({ a.x, b.x, c.x, d2.x }), maxX = std::max({ a.x, b.x, c.x, d2.x });
        const double minY = std::min({ a.y, b.y, c.y, d2.y }), maxY = std::max({ a.y, b.y, c.y, d2.y });
        if(std::fabs(xf.rot) < 0.5) {
            xf.cx = (minX + maxX) / 2; xf.cy = (minY + maxY) / 2;
            xf.w = (maxX - minX) / std::max(0.01, xf.sc); xf.h = (maxY - minY) / std::max(0.01, xf.sc);
            xf.squash = 1;
        }
        else { xf.w = W / std::max(0.01, xf.sc); xf.h = H / std::max(0.01, xf.sc); }
    }

}
