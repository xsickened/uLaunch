#pragma once
#include "settings.hpp"
#include <string>
#include <vector>

namespace nexus {

    constexpr double U = 24.0;          // Rastereinheit in px (1280x720)
    constexpr double ScreenW = 1280.0;
    constexpr double ScreenH = 720.0;

    struct LItem {
        std::string size;   // S M L XL custom
        std::string ratio;  // 1:1 2:3 3:2 4:3 16:9 custom
        double customH = 7;
        double customRatio = 1;
    };

    struct LRect {
        double x = 0, y = 0, w = 0, h = 0;
        double rot = 0;     // Grad
        double ry = 0;      // Drehung um Y (wird als horizontale Stauchung gezeichnet)
        double sc = 1;
        double op = 1;
        int z = 1;
        bool hex = false;
        bool li = false;
    };

    struct LArea {
        double x0, x1, y0, y1;
        bool v;
        std::string side;
    };

    enum class Nav { Spatial, H, V };

    struct LResult {
        std::vector<LRect> items;
        Nav nav = Nav::Spatial;
        bool wrapForce = false;
    };

    struct LayoutEnv {
        double topH = 60;
        double botH = 46;
        bool crumb = false;
    };

    bool IsVertical(const Settings &s);
    LArea ComputeArea(const Settings &s, const LayoutEnv &env);
    // Drehung einer 3D-Hülle (Grad; positiv = Rücken links sichtbar, negativ = rechts) je nach Anordnung
    double CaseAngle(const std::string &mode, double base, double cx, double focusCx, double screenCx, int index);
    void TileUnits(const Settings &s, const LItem &it, double maxH, double &w, double &h, double &ratio);
    // scroll wird gelesen und aktualisiert (Bildlauf für Raster/Reihen/Wabe/Vertikal/Liste)
    LResult ComputeLayout(const Settings &s, const LayoutEnv &env, const std::vector<LItem> &items, int focus, double &scroll);
    int Navigate(const LResult &r, const Settings &s, int focus, int dx, int dy);

}
