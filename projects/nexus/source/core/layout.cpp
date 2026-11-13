#include "layout.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace nexus {

    namespace {

        constexpr double kPi = 3.14159265358979323846;

        double SizeUnits(const std::string &s) {
            if(s == "S") return 5;
            if(s == "L") return 9;
            if(s == "XL") return 11;
            return 7;
        }

        double RatioOf(const std::string &r) {
            if(r == "1:1") return 1.0;
            if(r == "2:3") return 2.0 / 3.0;
            if(r == "3:2") return 1.5;
            if(r == "4:3") return 4.0 / 3.0;
            if(r == "16:9") return 16.0 / 9.0;
            return 2.0 / 3.0;
        }

        LRect Base(double x, double y, double w, double h) {
            LRect r;
            r.x = x; r.y = y; r.w = w; r.h = h;
            return r;
        }

        std::vector<LRect> PackGrid(const Settings &s, const std::vector<LItem> &list, double H, double gap) {
            // Belegungsraster (Spalten x Zeilen in Einheiten), First-Fit, Lücken bis 12 Einheiten zurück füllen
            std::map<int, std::vector<char>> occ;
            const int HH = static_cast<int>(std::ceil(H + gap));
            auto cell = [&](int x, int y) -> char & {
                auto &col = occ[x];
                if(static_cast<int>(col.size()) < HH + 16) col.resize(HH + 16, 0);
                return col[y];
            };
            auto isFree = [&](int x, int y, int w, int h) {
                for(int i = x; i < x + w; i++) for(int j = y; j < y + h; j++) if(cell(i, j)) return false;
                return true;
            };
            std::vector<LRect> out;
            int minX = 0;
            for(const auto &it: list) {
                double w, h, ratio;
                TileUnits(s, it, H, w, h, ratio);
                const int W = static_cast<int>(std::ceil(w + gap)), Hh = static_cast<int>(std::ceil(h + gap));
                bool placed = false;
                for(int x = minX; x < minX + 4000 && !placed; x++) {
                    for(int y = 0; y + Hh <= std::max(HH, Hh); y++) {
                        if(isFree(x, y, W, Hh)) {
                            for(int i = x; i < x + W; i++) for(int j = y; j < y + Hh; j++) cell(i, j) = 1;
                            minX = std::max(minX, x - 12);
                            out.push_back(Base(x * U, y * U, w * U, h * U));
                            placed = true;
                            break;
                        }
                    }
                }
                if(!placed) out.push_back(Base(0, 0, w * U, h * U));
            }
            return out;
        }

        double Fade(double sideOpacity, int k, double per) {
            if(k == 0) return 1.0;
            return std::max(0.05, sideOpacity - (std::abs(k) - 1) * per);
        }

    }

    bool IsVertical(const Settings &s) {
        const auto m = s.S("layout.mode");
        const auto d = s.S("layout.arcDir");
        return m == "vgrid" || m == "list" || (m == "arc" && (d == "left" || d == "right"));
    }

    LArea ComputeArea(const Settings &s, const LayoutEnv &env) {
        if(IsVertical(s)) {
            const auto mode = s.S("layout.mode");
            const auto side = mode == "arc" ? s.S("layout.arcDir") : s.S("layout.side");
            double x0 = 60, x1 = 1220;
            if(side == "left") { x0 = 60; x1 = 640; }
            else if(side == "right") { x0 = 640; x1 = 1220; }
            return { x0, x1, env.topH + (env.crumb ? 26 : 16), ScreenH - env.botH, true, side };
        }
        const double h = s.N("layout.height") * U;
        const double tb = env.topH + (env.crumb ? 26 : 8), bb = ScreenH - env.botH;
        const auto pos = s.S("layout.position");
        const double y0 = pos == "top" ? tb : pos == "center" ? tb + (bb - tb - h) / 2 : bb - h;
        return { 60, 1220, y0, y0 + h, false, "" };
    }

    void TileUnits(const Settings &s, const LItem &it, double maxH, double &w, double &h, double &ratio) {
        const bool uniform = s.B("layout.uniform");
        const auto size = uniform ? s.S("tileDefaults.size") : it.size;
        const auto rk = uniform ? s.S("tileDefaults.ratio") : it.ratio;
        h = std::clamp(size == "custom" ? it.customH : SizeUnits(size), 3.0, std::max(3.0, maxH));
        ratio = rk == "custom" ? std::clamp(it.customRatio, 0.4, 2.5) : RatioOf(rk);
        w = std::max(2.0, std::round(h * ratio));
    }

    LResult ComputeLayout(const Settings &s, const LayoutEnv &env, const std::vector<LItem> &list, int f, double &scroll) {
        LResult res;
        const LArea A = ComputeArea(s, env);
        const auto mode = s.S("layout.mode");
        const double gapU = s.N("layout.gap");
        const double sideScale = s.N("layout.sideScale"), sideOp = s.N("layout.sideOpacity");
        const int n = static_cast<int>(list.size());
        f = std::clamp(f, 0, std::max(0, n - 1));
        enum { NoScroll, ScrollX, ScrollY } sc = NoScroll;

        if(mode == "rows") {
            const int rc = static_cast<int>(s.N("layout.rowCount"));
            const double gapPx = gapU * U;
            const double laneH = std::max(60.0, ((A.y1 - A.y0) - gapPx * (rc - 1)) / rc);
            std::vector<double> cur(rc, 0);
            for(int i = 0; i < n; i++) {
                const int lane = i % rc;
                double w, h, ratio;
                TileUnits(s, list[i], 99, w, h, ratio);
                const double pw = std::max(40.0, laneH * ratio);
                res.items.push_back(Base(cur[lane], lane * (laneH + gapPx), pw, laneH));
                cur[lane] += pw + gapPx;
            }
            sc = ScrollX;
        }
        else if(mode == "honeycomb") {
            const int rows = std::max(1, static_cast<int>(s.N("layout.height") / 4));
            const double h = (A.y1 - A.y0) / (0.75 * (rows - 1) + 1), w = h * 0.866, g = gapU * 6;
            for(int i = 0; i < n; i++) {
                const int c = i / rows, r = i % rows;
                auto it = Base(c * (w + g) + (r % 2 ? (w + g) / 2 : 0), r * h * 0.75, w, h);
                it.hex = true;
                res.items.push_back(it);
            }
            sc = ScrollX;
        }
        else if(mode == "vgrid") {
            const int cols = static_cast<int>(s.N("layout.columns"));
            const double gapPx = gapU * U;
            const double cw = ((A.x1 - A.x0) - gapPx * (cols - 1)) / cols;
            std::vector<double> colH(cols, 0);
            for(int i = 0; i < n; i++) {
                double w, h, ratio;
                TileUnits(s, list[i], 99, w, h, ratio);
                const double ph = cw / ratio;
                int c = 0;
                for(int k = 1; k < cols; k++) if(colH[k] < colH[c] - 1) c = k;
                res.items.push_back(Base(c * (cw + gapPx), colH[c], cw, ph));
                colH[c] += ph + gapPx;
            }
            sc = ScrollY;
        }
        else if(mode == "list") {
            const double h = 64 * s.N("icons.size"), gap = 8 + gapU * 6;
            for(int i = 0; i < n; i++) {
                auto it = Base(0, i * (h + gap), A.x1 - A.x0, h);
                it.li = true;
                res.items.push_back(it);
            }
            res.nav = Nav::V;
            sc = ScrollY;
        }
        else if(mode == "carousel" || mode == "wave") {
            const bool wave = mode == "wave";
            const double gapPx = std::max(8.0, gapU * U), H = A.y1 - A.y0;
            const double amp = s.N("layout.waveAmp"), freq = s.N("layout.waveFreq");
            std::vector<double> dw(n), dh(n);
            for(int i = 0; i < n; i++) {
                double w, h, ratio;
                TileUnits(s, list[i], s.N("layout.height"), w, h, ratio);
                const double k = std::min(1.0, H / (h * U));
                dw[i] = w * U * k; dh[i] = h * U * k;
            }
            res.items.assign(n, LRect());
            const double cx = (A.x0 + A.x1) / 2;
            auto place = [&](int i, double centerX) {
                const int k = i - f;
                auto &it = res.items[i];
                it.w = dw[i]; it.h = dh[i];
                it.x = centerX - dw[i] / 2;
                it.y = wave ? A.y0 + (H - dh[i]) / 2 - amp * std::sin(k * freq) : A.y1 - dh[i];
                it.sc = k == 0 ? 1 : sideScale;
                it.op = std::abs(k) > 8 ? 0 : Fade(sideOp, k, 0.12);
                it.z = 100 - std::abs(k);
            };
            if(n > 0) {
                place(f, cx);
                double x = cx + dw[f] / 2;
                for(int i = f + 1; i < n; i++) { const double w = dw[i] * sideScale; place(i, x + gapPx + w / 2); x += gapPx + w; }
                x = cx - dw[f] / 2;
                for(int i = f - 1; i >= 0; i--) { const double w = dw[i] * sideScale; place(i, x - gapPx - w / 2); x -= gapPx + w; }
            }
            res.nav = Nav::H;
        }
        else if(mode == "arc") {
            const double R = s.N("layout.arcRadius"), sp = s.N("layout.arcSpread") * kPi / 180.0;
            const auto dir = s.S("layout.arcDir");
            const bool side = dir == "left" || dir == "right";
            const bool wrap = s.B("layout.wrap");
            for(int i = 0; i < n; i++) {
                int k = i - f;
                if(wrap && n > 2) { if(k > n / 2) k -= n; if(k < -n / 2) k += n; }
                double w, h, ratio;
                TileUnits(s, list[i], side ? 7 : s.N("layout.height"), w, h, ratio);
                const double W = w * U, Hh = h * U, th = k * sp;
                double cx, cy, rot;
                if(dir == "up") { const double ay = (A.y0 + A.y1) / 2; cx = 640 + R * std::sin(th); cy = ay + R - R * std::cos(th); rot = th; }
                else if(dir == "down") { const double ay = (A.y0 + A.y1) / 2; cx = 640 + R * std::sin(th); cy = ay - R + R * std::cos(th); rot = -th; }
                else if(dir == "left") { const double ax = A.x0 + 40 + W / 2; cx = ax - R + R * std::cos(th); cy = 360 + R * std::sin(th); rot = th; }
                else { const double ax = A.x1 - 40 - W / 2; cx = ax + R - R * std::cos(th); cy = 360 + R * std::sin(th); rot = -th; }
                auto it = Base(cx - W / 2, cy - Hh / 2, W, Hh);
                it.rot = s.B("layout.rotateTiles") ? rot * 180.0 / kPi : 0;
                it.sc = k == 0 ? 1 : sideScale;
                it.op = std::abs(th) > 1.7 ? 0 : Fade(sideOp, k, 0.1);
                it.z = 100 - std::abs(k);
                res.items.push_back(it);
            }
            res.nav = side ? Nav::V : Nav::H;
        }
        else if(mode == "ring") {
            const double R = s.N("layout.ringRadius"), tilt = s.N("layout.tilt");
            const double step = 2 * kPi / std::max(n, 6), cy = (A.y0 + A.y1) / 2;
            for(int i = 0; i < n; i++) {
                int k = i - f;
                if(k > n / 2) k -= n;
                if(k < -n / 2) k += n;
                const double th = k * step, d = std::cos(th);
                double w, h, ratio;
                TileUnits(s, list[i], s.N("layout.height"), w, h, ratio);
                const double W = w * U * 0.8, Hh = h * U * 0.8;
                auto it = Base(640 + R * std::sin(th) - W / 2, cy - Hh / 2 - (1 - d) * 30, W, Hh);
                it.sc = k == 0 ? 1 : (0.45 + 0.55 * (d + 1) / 2) * sideScale;
                it.ry = -th * 180.0 / kPi * (tilt / 75.0);
                it.z = static_cast<int>(std::lround(100 + d * 100));
                it.op = d < -0.3 ? 0 : (k == 0 ? 1 : std::clamp(sideOp * (0.4 + 0.6 * (d + 1) / 2), 0.05, 1.0));
                res.items.push_back(it);
            }
            res.nav = Nav::H;
            res.wrapForce = true;
        }
        else if(mode == "coverflow") {
            const double cy = (A.y0 + A.y1) / 2, tilt = s.N("layout.tilt");
            for(int i = 0; i < n; i++) {
                const int k = i - f;
                double w, h, ratio;
                TileUnits(s, list[i], s.N("layout.height"), w, h, ratio);
                const double W = w * U, Hh = h * U, sg = k > 0 ? 1 : k < 0 ? -1 : 0;
                const double cx = 640 + (k == 0 ? 0 : sg * (W * 0.62 + (std::abs(k) - 1) * W * 0.28));
                auto it = Base(cx - W / 2, cy - Hh / 2, W, Hh);
                it.ry = -sg * tilt;
                it.sc = k == 0 ? 1 : sideScale;
                it.z = 100 - std::abs(k);
                it.op = std::abs(k) > 7 ? 0 : (k == 0 ? 1 : sideOp);
                res.items.push_back(it);
            }
            res.nav = Nav::H;
        }
        else if(mode == "stack") {
            const double o = s.N("layout.stackOffset"), cy = (A.y0 + A.y1) / 2;
            for(int i = 0; i < n; i++) {
                const int k = i - f;
                double w, h, ratio;
                TileUnits(s, list[i], s.N("layout.height"), w, h, ratio);
                const double W = w * U, Hh = h * U;
                auto it = Base(640 - W / 2 + (k < 0 ? -520 : k * o), cy - Hh / 2 - (k > 0 ? k * o * 0.5 : 0), W, Hh);
                it.sc = k <= 0 ? 1 : 1 - k * 0.06;
                it.op = k < 0 ? 0 : k > 5 ? 0 : (k == 0 ? 1 : std::max(0.05, sideOp - (k - 1) * 0.15));
                it.z = 100 - std::abs(k);
                it.rot = k < 0 ? -12 : 0;
                res.items.push_back(it);
            }
            res.nav = Nav::H;
        }
        else { // grid (Standard und Rückfall)
            res.items = PackGrid(s, list, s.N("layout.height"), gapU);
            sc = ScrollX;
        }

        if(sc != NoScroll) {
            const bool horiz = sc == ScrollX;
            double extent = 0;
            for(const auto &r: res.items) extent = std::max(extent, horiz ? r.x + r.w : r.y + r.h);
            const double view = horiz ? A.x1 - A.x0 : A.y1 - A.y0;
            double off = 0;
            if(s.S("layout.align") == "center" && horiz && extent < view && mode != "list") off = (view - extent) / 2;
            if(f < static_cast<int>(res.items.size())) {
                const auto &r = res.items[f];
                const double a = horiz ? r.x : r.y, b = horiz ? r.x + r.w : r.y + r.h;
                if(s.S("layout.focusMode") == "center") scroll = (a + b) / 2 - view / 2;
                else {
                    if(a - scroll < 0) scroll = a - (f == 0 ? 0 : 20);
                    if(b - scroll > view) scroll = b - view + 20;
                }
                scroll = std::clamp(scroll, 0.0, std::max(0.0, extent - view));
            }
            for(auto &r: res.items) {
                if(horiz) { r.x += A.x0 + off - scroll; r.y += A.y0; }
                else { r.x += A.x0; r.y += A.y0 - scroll; }
                if(horiz && (r.x + r.w < -200 || r.x > ScreenW + 200)) r.op = 0;
                if(!horiz && (r.y + r.h < 0 || r.y > ScreenH)) r.op = 0;
            }
        }
        return res;
    }

    int Navigate(const LResult &r, const Settings &s, int focus, int dx, int dy) {
        const int n = static_cast<int>(r.items.size());
        if(n == 0) return 0;
        if(r.nav != Nav::Spatial) {
            const int along = r.nav == Nav::H ? dx : dy;
            if(!along) return focus;
            int t = focus + along;
            const bool wrap = r.wrapForce || s.B("layout.wrap");
            if(t < 0) t = wrap ? n - 1 : 0;
            if(t >= n) t = wrap ? 0 : n - 1;
            return t;
        }
        const auto &c = r.items[focus];
        const double cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        int best = -1;
        double bs = 1e18;
        for(int i = 0; i < n; i++) {
            if(i == focus) continue;
            const auto &o = r.items[i];
            const double ox = o.x + o.w / 2 - cx, oy = o.y + o.h / 2 - cy;
            const double prim = dx ? ox * dx : oy * dy;
            if(prim <= 4) continue;
            const double ov = dx ? std::min(c.y + c.h, o.y + o.h) - std::max(c.y, o.y) : std::min(c.x + c.w, o.x + o.w) - std::max(c.x, o.x);
            const double score = prim + std::abs(dx ? oy : ox) * 2 - (ov > 0 ? 200 : 0);
            if(score < bs) { bs = score; best = i; }
        }
        return best >= 0 ? best : focus;
    }

}
