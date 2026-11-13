#include "gfx.hpp"
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nexus {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        int HexVal(char c) {
            if(c >= '0' && c <= '9') return c - '0';
            if(c >= 'a' && c <= 'f') return c - 'a' + 10;
            if(c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        }

        SDL_Color SC(Col c) { return SDL_Color{ c.r, c.g, c.b, c.a }; }
    }

    Col ParseCol(const std::string &hex, uint8_t a) {
        Col c;
        c.a = a;
        if(hex.size() == 7 && hex[0] == '#') {
            c.r = static_cast<uint8_t>(HexVal(hex[1]) * 16 + HexVal(hex[2]));
            c.g = static_cast<uint8_t>(HexVal(hex[3]) * 16 + HexVal(hex[4]));
            c.b = static_cast<uint8_t>(HexVal(hex[5]) * 16 + HexVal(hex[6]));
        }
        return c;
    }

    Col WithA(Col c, double alpha) {
        c.a = static_cast<uint8_t>(std::clamp(c.a * alpha, 0.0, 255.0));
        return c;
    }

    Col Lerp(Col a, Col b, double t) {
        t = std::clamp(t, 0.0, 1.0);
        auto l = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); };
        return Col{ l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a) };
    }

    bool Gfx::Init(SDL_Renderer *renderer, const void *font, size_t fontSize) {
        this->r = renderer;
        this->fontData = font;
        this->fontLen = fontSize;
        SDL_SetRenderDrawBlendMode(this->r, SDL_BLENDMODE_BLEND);
        return this->Font(20, false) != nullptr;
    }

    void Gfx::Exit() {
        for(auto &[k, e]: this->textCache) SDL_DestroyTexture(e.tex);
        this->textCache.clear();
        for(auto &[k, f]: this->fonts) TTF_CloseFont(f);
        this->fonts.clear();
    }

    void Gfx::BeginFrame() {
        this->frame++;
        if(this->textCache.size() > 500) {
            for(auto it = this->textCache.begin(); it != this->textCache.end();) {
                if(this->frame - it->second.used > 120) {
                    SDL_DestroyTexture(it->second.tex);
                    it = this->textCache.erase(it);
                }
                else ++it;
            }
        }
    }

    TTF_Font *Gfx::Font(int size, bool bold) {
        size = std::clamp(size, 6, 300);
        const int key = size * 2 + (bold ? 1 : 0);
        auto it = this->fonts.find(key);
        if(it != this->fonts.end()) return it->second;
        if(!this->fontData) return nullptr;
        SDL_RWops *rw = SDL_RWFromConstMem(this->fontData, static_cast<int>(this->fontLen));
        TTF_Font *f = rw ? TTF_OpenFontRW(rw, 1, size) : nullptr;
        if(f && bold) TTF_SetFontStyle(f, TTF_STYLE_BOLD);
        this->fonts[key] = f;
        return f;
    }

    void Gfx::Fan(SDL_Texture *tex, const std::vector<SDL_FPoint> &pts, const std::vector<SDL_FPoint> *uv, SDL_FPoint center, SDL_FPoint centerUv, Col c) {
        if(pts.size() < 3) return;
        this->vbuf.clear();
        this->ibuf.clear();
        const SDL_Color col = SC(c);
        this->vbuf.push_back(SDL_Vertex{ center, col, centerUv });
        for(size_t i = 0; i < pts.size(); i++) {
            this->vbuf.push_back(SDL_Vertex{ pts[i], col, uv ? (*uv)[i] : SDL_FPoint{ 0, 0 } });
        }
        const int n = static_cast<int>(pts.size());
        for(int i = 0; i < n; i++) {
            this->ibuf.push_back(0);
            this->ibuf.push_back(1 + i);
            this->ibuf.push_back(1 + (i + 1) % n);
        }
        SDL_RenderGeometry(this->r, tex, this->vbuf.data(), static_cast<int>(this->vbuf.size()), this->ibuf.data(), static_cast<int>(this->ibuf.size()));
    }

    void Gfx::Rect(double x, double y, double w, double h, Col c) {
        this->GradV(x, y, w, h, c, c);
    }

    void Gfx::GradV(double x, double y, double w, double h, Col top, Col bottom) {
        const SDL_Color a = SC(top), b = SC(bottom);
        const float X = static_cast<float>(x), Y = static_cast<float>(y), W = static_cast<float>(w), H = static_cast<float>(h);
        SDL_Vertex v[4] = {
            { { X, Y }, a, { 0, 0 } }, { { X + W, Y }, a, { 0, 0 } },
            { { X + W, Y + H }, b, { 0, 0 } }, { { X, Y + H }, b, { 0, 0 } }
        };
        const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(this->r, nullptr, v, 4, idx, 6);
    }

    void Gfx::GradH(double x, double y, double w, double h, Col left, Col right) {
        const SDL_Color a = SC(left), b = SC(right);
        const float X = static_cast<float>(x), Y = static_cast<float>(y), W = static_cast<float>(w), H = static_cast<float>(h);
        SDL_Vertex v[4] = {
            { { X, Y }, a, { 0, 0 } }, { { X + W, Y }, b, { 0, 0 } },
            { { X + W, Y + H }, b, { 0, 0 } }, { { X, Y + H }, a, { 0, 0 } }
        };
        const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(this->r, nullptr, v, 4, idx, 6);
    }

    void Gfx::ShapePoints(const Xf &xf, double rad, bool hex, double grow, std::vector<SDL_FPoint> &pts, std::vector<SDL_FPoint> *uv, bool cover, double texW, double texH, double posX, double posY) {
        pts.clear();
        if(uv) uv->clear();
        const double w = xf.w + 2 * grow, h = xf.h + 2 * grow;
        std::vector<std::pair<double, double>> local;
        if(hex) {
            const double k = w / std::max(1.0, xf.w);
            const double hw = xf.w / 2 * k, hh = xf.h / 2 * (h / std::max(1.0, xf.h));
            local = { { 0, -hh }, { hw, -hh / 2 }, { hw, hh / 2 }, { 0, hh }, { -hw, hh / 2 }, { -hw, -hh / 2 } };
        }
        else {
            const double rr = std::clamp(rad + (rad > 0.5 ? grow : 0), 0.0, std::min(w, h) / 2);
            const double hw = w / 2, hh = h / 2;
            if(rr < 0.5) {
                local = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };
            }
            else {
                const int seg = rr > 12 ? 7 : 4;
                const double cxs[4] = { hw - rr, hw - rr, -hw + rr, -hw + rr };
                const double cys[4] = { -hh + rr, hh - rr, hh - rr, -hh + rr };
                const double start[4] = { -90, 0, 90, 180 };
                for(int c = 0; c < 4; c++) {
                    for(int i = 0; i <= seg; i++) {
                        const double a = (start[c] + 90.0 * i / seg) * kPi / 180.0;
                        local.push_back({ cxs[c] + rr * std::cos(a), cys[c] + rr * std::sin(a) });
                    }
                }
            }
        }
        // UV (Zuschnitt "cover" mit Bildposition)
        double srcX = 0, srcY = 0, srcW = texW, srcH = texH;
        if(uv && texW > 0 && texH > 0 && cover) {
            const double at = xf.w / std::max(1.0, xf.h), as = texW / texH;
            if(as > at) { srcW = texH * at; srcX = (texW - srcW) * posX / 100.0; }
            else { srcH = texW / at; srcY = (texH - srcH) * posY / 100.0; }
        }
        const double cs = std::cos(xf.rot * kPi / 180.0), sn = std::sin(xf.rot * kPi / 180.0);
        for(const auto &[lx, ly]: local) {
            const double x = lx * xf.sc * xf.squash, y = ly * xf.sc;
            pts.push_back(SDL_FPoint{ static_cast<float>(xf.cx + x * cs - y * sn), static_cast<float>(xf.cy + x * sn + y * cs) });
            if(uv) {
                const double u = (srcX + (lx / std::max(1.0, xf.w) + 0.5) * srcW) / std::max(1.0, texW);
                const double v = (srcY + (ly / std::max(1.0, xf.h) + 0.5) * srcH) / std::max(1.0, texH);
                uv->push_back(SDL_FPoint{ static_cast<float>(std::clamp(u, 0.0, 1.0)), static_cast<float>(std::clamp(v, 0.0, 1.0)) });
            }
        }
    }

    void Gfx::Shape(const Xf &xf, double rad, bool hex, Col c) {
        std::vector<SDL_FPoint> pts;
        this->ShapePoints(xf, rad, hex, 0, pts, nullptr, false, 0, 0, 50, 50);
        this->Fan(nullptr, pts, nullptr, SDL_FPoint{ static_cast<float>(xf.cx), static_cast<float>(xf.cy) }, SDL_FPoint{ 0, 0 }, c);
    }

    void Gfx::ShapeTex(SDL_Texture *tex, const Xf &xf, double rad, bool hex, double alpha, bool cover, double posX, double posY, Col tint) {
        if(!tex) return;
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        if(tw <= 0 || th <= 0) return;
        if(!cover) {
            // Einpassen: dunkler Hintergrund in Kachelform, Bild zentriert darin
            this->Shape(xf, rad, hex, Col{ 20, 24, 32, static_cast<uint8_t>(255 * alpha) });
            Xf in = xf;
            const double at = xf.w / std::max(1.0, xf.h), as = static_cast<double>(tw) / th;
            if(as > at) in.h = xf.w / as; else in.w = xf.h * as;
            if(hex) { in.w *= 0.8; in.h *= 0.8; }
            this->ShapeTex(tex, in, 0, false, alpha, true, 50, 50, tint);
            return;
        }
        std::vector<SDL_FPoint> pts, uv;
        this->ShapePoints(xf, rad, hex, 0, pts, &uv, true, tw, th, posX, posY);
        double srcX = 0, srcY = 0, srcW = tw, srcH = th;
        const double at = xf.w / std::max(1.0, xf.h), as = static_cast<double>(tw) / th;
        if(as > at) { srcW = th * at; srcX = (tw - srcW) * posX / 100.0; }
        else { srcH = tw / at; srcY = (th - srcH) * posY / 100.0; }
        const SDL_FPoint cuv{ static_cast<float>((srcX + srcW / 2) / tw), static_cast<float>((srcY + srcH / 2) / th) };
        this->Fan(tex, pts, &uv, SDL_FPoint{ static_cast<float>(xf.cx), static_cast<float>(xf.cy) }, cuv, Col{ tint.r, tint.g, tint.b, static_cast<uint8_t>(std::clamp(alpha, 0.0, 1.0) * tint.a) });
    }

    void Gfx::ShapeRing(const Xf &xf, double rad, bool hex, double t, Col c) {
        std::vector<SDL_FPoint> outer, inner;
        this->ShapePoints(xf, rad, hex, t, outer, nullptr, false, 0, 0, 50, 50);
        this->ShapePoints(xf, rad, hex, 0, inner, nullptr, false, 0, 0, 50, 50);
        if(outer.size() != inner.size() || outer.size() < 3) return;
        this->vbuf.clear();
        this->ibuf.clear();
        const SDL_Color col = SC(c);
        const int n = static_cast<int>(outer.size());
        for(int i = 0; i < n; i++) {
            this->vbuf.push_back(SDL_Vertex{ outer[i], col, { 0, 0 } });
            this->vbuf.push_back(SDL_Vertex{ inner[i], col, { 0, 0 } });
        }
        for(int i = 0; i < n; i++) {
            const int a = 2 * i, b = 2 * i + 1, c2 = 2 * ((i + 1) % n), d = 2 * ((i + 1) % n) + 1;
            this->ibuf.insert(this->ibuf.end(), { a, b, c2, b, d, c2 });
        }
        SDL_RenderGeometry(this->r, nullptr, this->vbuf.data(), static_cast<int>(this->vbuf.size()), this->ibuf.data(), static_cast<int>(this->ibuf.size()));
    }

    void Gfx::RoundRect(double x, double y, double w, double h, double rad, Col c) {
        Xf xf;
        xf.cx = x + w / 2; xf.cy = y + h / 2; xf.w = w; xf.h = h;
        this->Shape(xf, rad, false, c);
    }

    void Gfx::RoundRectOutline(double x, double y, double w, double h, double rad, double t, Col c) {
        Xf xf;
        xf.cx = x + w / 2; xf.cy = y + h / 2; xf.w = w; xf.h = h;
        this->ShapeRing(xf, rad, false, t, c);
    }

    void Gfx::Circle(double cx, double cy, double rad, Col c) {
        Xf xf;
        xf.cx = cx; xf.cy = cy; xf.w = rad * 2; xf.h = rad * 2;
        this->Shape(xf, rad, false, c);
    }

    void Gfx::Line(double x1, double y1, double x2, double y2, double t, Col c) {
        const double dx = x2 - x1, dy = y2 - y1, len = std::max(0.001, std::sqrt(dx * dx + dy * dy));
        const double nx = -dy / len * t / 2, ny = dx / len * t / 2;
        const SDL_Color col = SC(c);
        SDL_Vertex v[4] = {
            { { static_cast<float>(x1 + nx), static_cast<float>(y1 + ny) }, col, { 0, 0 } },
            { { static_cast<float>(x2 + nx), static_cast<float>(y2 + ny) }, col, { 0, 0 } },
            { { static_cast<float>(x2 - nx), static_cast<float>(y2 - ny) }, col, { 0, 0 } },
            { { static_cast<float>(x1 - nx), static_cast<float>(y1 - ny) }, col, { 0, 0 } }
        };
        const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(this->r, nullptr, v, 4, idx, 6);
    }

    void Gfx::Image(SDL_Texture *tex, double x, double y, double w, double h, double alpha, bool cover, Col tint) {
        Xf xf;
        xf.cx = x + w / 2; xf.cy = y + h / 2; xf.w = w; xf.h = h;
        if(cover) this->ShapeTex(tex, xf, 0, false, alpha, true, 50, 50, tint);
        else {
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if(tw <= 0 || th <= 0) return;
            const double s = std::min(w / tw, h / th);
            xf.w = tw * s; xf.h = th * s;
            this->ShapeTex(tex, xf, 0, false, alpha, true, 50, 50, tint);
        }
    }

    SDL_Texture *Gfx::TextTex(const std::string &s, int size, Col c, int outline, Col oc, int &w, int &h, bool bold) {
        w = h = 0;
        if(s.empty()) return nullptr;
        char key[96];
        std::snprintf(key, sizeof(key), "%d|%d|%d|%02x%02x%02x%02x|%02x%02x%02x|", size, bold ? 1 : 0, outline, c.r, c.g, c.b, c.a, oc.r, oc.g, oc.b);
        const std::string k = std::string(key) + s;
        auto it = this->textCache.find(k);
        if(it != this->textCache.end()) {
            it->second.used = this->frame;
            w = it->second.w; h = it->second.h;
            return it->second.tex;
        }
        TTF_Font *f = this->Font(size, bold);
        if(!f) return nullptr;
        SDL_Surface *surf = nullptr;
        if(outline > 0) {
            // Kontur: eigene Schriftinstanz mit Outline, Text darüber kopieren
            TTF_SetFontOutline(f, outline);
            SDL_Surface *back = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{ oc.r, oc.g, oc.b, 255 });
            TTF_SetFontOutline(f, 0);
            SDL_Surface *front = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{ c.r, c.g, c.b, 255 });
            if(back && front) {
                SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
                SDL_Rect dst{ outline, outline, front->w, front->h };
                SDL_BlitSurface(front, nullptr, back, &dst);
                surf = back;
                back = nullptr;
            }
            if(front) SDL_FreeSurface(front);
            if(back) SDL_FreeSurface(back);
        }
        else {
            surf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{ c.r, c.g, c.b, 255 });
        }
        if(!surf) return nullptr;
        SDL_Texture *tex = SDL_CreateTextureFromSurface(this->r, surf);
        w = surf->w; h = surf->h;
        SDL_FreeSurface(surf);
        if(!tex) return nullptr;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, c.a);
        this->textCache[k] = TextEntry{ tex, w, h, this->frame };
        return tex;
    }

    int Gfx::Text(const std::string &s, int size, double x, double y, Col c, int align, int maxW, bool bold) {
        int w, h;
        SDL_Texture *t = this->TextTex(s, size, Col{ c.r, c.g, c.b, 255 }, 0, Col{}, w, h, bold);
        if(!t) return 0;
        int drawW = w;
        SDL_Rect src{ 0, 0, w, h };
        if(maxW > 0 && w > maxW) { drawW = maxW; src.w = maxW; }
        double dx = x;
        if(align == 1) dx = x - drawW / 2.0;
        else if(align == 2) dx = x - drawW;
        SDL_SetTextureAlphaMod(t, c.a);
        SDL_Rect dst{ static_cast<int>(std::lround(dx)), static_cast<int>(std::lround(y)), drawW, h };
        SDL_RenderCopy(this->r, t, &src, &dst);
        return drawW;
    }

    int Gfx::TextW(const std::string &s, int size, bool bold) {
        TTF_Font *f = this->Font(size, bold);
        int w = 0, h = 0;
        if(f && !s.empty()) TTF_SizeUTF8(f, s.c_str(), &w, &h);
        return w;
    }

    void Gfx::Icon(const std::string &name, double cx, double cy, double s, Col c, bool filled) {
        const double t = std::max(1.5, s * 0.09), h = s / 2;
        const Col fill = WithA(c, 0.35);
        auto poly = [&](const std::vector<std::pair<double, double>> &p, bool close) {
            if(filled && close && p.size() >= 3) {
                std::vector<SDL_FPoint> pts;
                double mx = 0, my = 0;
                for(const auto &q: p) { pts.push_back(SDL_FPoint{ static_cast<float>(cx + q.first * h), static_cast<float>(cy + q.second * h) }); mx += q.first; my += q.second; }
                this->Fan(nullptr, pts, nullptr, SDL_FPoint{ static_cast<float>(cx + mx / p.size() * h), static_cast<float>(cy + my / p.size() * h) }, SDL_FPoint{ 0, 0 }, fill);
            }
            for(size_t i = 0; i + 1 < p.size() + (close ? 1 : 0); i++) {
                const auto &a = p[i], &b = p[(i + 1) % p.size()];
                this->Line(cx + a.first * h, cy + a.second * h, cx + b.first * h, cy + b.second * h, t, c);
            }
        };
        auto ring = [&](double rx, double ry, double rr) {
            std::vector<std::pair<double, double>> p;
            for(int i = 0; i < 20; i++) p.push_back({ rx + rr * std::cos(i * kPi / 10), ry + rr * std::sin(i * kPi / 10) });
            poly(p, true);
        };
        if(name == "start") poly({ { -0.8, -0.05 }, { 0, -0.8 }, { 0.8, -0.05 }, { 0.65, -0.05 }, { 0.65, 0.8 }, { -0.65, 0.8 }, { -0.65, -0.05 } }, true);
        else if(name == "all") {
            for(const auto &[ox, oy]: std::vector<std::pair<double, double>>{ { -0.75, -0.75 }, { 0.1, -0.75 }, { -0.75, 0.1 }, { 0.1, 0.1 } })
                poly({ { ox, oy }, { ox + 0.65, oy }, { ox + 0.65, oy + 0.65 }, { ox, oy + 0.65 } }, true);
        }
        else if(name == "fav") {
            std::vector<std::pair<double, double>> p;
            for(int i = 0; i < 10; i++) {
                const double a = -kPi / 2 + i * kPi / 5, rr = i % 2 ? 0.38 : 0.85;
                p.push_back({ rr * std::cos(a), rr * std::sin(a) + 0.05 });
            }
            poly(p, true);
        }
        else if(name == "folders") poly({ { -0.85, -0.6 }, { -0.25, -0.6 }, { -0.08, -0.4 }, { 0.85, -0.4 }, { 0.85, 0.7 }, { -0.85, 0.7 } }, true);
        else if(name == "recent") { ring(0, 0, 0.8); poly({ { 0, -0.45 }, { 0, 0 }, { 0.35, 0.2 } }, false); }
        else if(name == "homebrew") poly({ { -0.3, -0.8 }, { 0.3, -0.8 }, { 0.3, -0.2 }, { 0.8, 0.65 }, { 0.65, 0.8 }, { -0.65, 0.8 }, { -0.8, 0.65 }, { -0.3, -0.2 } }, true);
        else if(name == "settings") {
            ring(0, 0, 0.38);
            for(int i = 0; i < 8; i++) {
                const double a = i * kPi / 4;
                this->Line(cx + std::cos(a) * 0.55 * h, cy + std::sin(a) * 0.55 * h, cx + std::cos(a) * 0.9 * h, cy + std::sin(a) * 0.9 * h, t * 1.3, c);
            }
        }
        else if(name == "lock") { poly({ { -0.6, -0.1 }, { 0.6, -0.1 }, { 0.6, 0.8 }, { -0.6, 0.8 } }, true); poly({ { -0.35, -0.1 }, { -0.35, -0.5 }, { 0, -0.8 }, { 0.35, -0.5 }, { 0.35, -0.1 } }, false); }
        else ring(0, 0, 0.6);
    }

    void Gfx::Battery(double x, double y, double s, int pct, bool charging, Col c) {
        const double w = s * 1.25, h = s * 0.62;
        this->RoundRectOutline(x, y, w, h, 3, std::max(1.5, s * 0.08), c);
        this->Rect(x + w + 1, y + h * 0.3, s * 0.1, h * 0.4, c);
        const double p = std::clamp(pct < 0 ? 100 : pct, 0, 100) / 100.0;
        const Col fillc = pct >= 0 && pct <= 15 && !charging ? Col{ 255, 80, 80, c.a } : (charging ? Col{ 90, 220, 120, c.a } : c);
        this->Rect(x + 3, y + 3, std::max(1.0, (w - 6) * p), h - 6, fillc);
    }

    void Gfx::Wifi(double cx, double cy, double s, int bars, Col c) {
        for(int i = 0; i < 3; i++) {
            const double bh = s * (0.35 + 0.3 * i), bw = s * 0.22;
            const double x = cx - s * 0.45 + i * s * 0.32;
            this->Rect(x, cy + s / 2 - bh, bw, bh, i < bars ? c : WithA(c, 0.3));
        }
    }

    void Gfx::Button(const std::string &label, double cx, double cy, double s, Col bg, Col fg) {
        const int tw = this->TextW(label, static_cast<int>(s * 0.6), true);
        const double w = std::max(s, tw + s * 0.5);
        this->RoundRect(cx - w / 2, cy - s / 2, w, s, s / 2, bg);
        int w2, h2;
        SDL_Texture *t = this->TextTex(label, static_cast<int>(s * 0.6), fg, 0, Col{}, w2, h2, true);
        if(t) {
            SDL_Rect dst{ static_cast<int>(cx - w2 / 2.0), static_cast<int>(cy - h2 / 2.0), w2, h2 };
            SDL_RenderCopy(this->r, t, nullptr, &dst);
        }
    }

}
