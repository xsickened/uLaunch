#pragma once
#include "settings.hpp"
#include <string>

namespace nexus {

    // Akzentfarbe mit Verläufen und RGB-Modus. Dieselbe Rechnung steht im Prototyp (accentAt), damit
    // Vorschau und Switch gleich aussehen.
    struct Rgb {
        double r = 0, g = 0, b = 0;   // 0..255
    };

    struct AccentParams {
        std::string mode = "solid";   // solid | gradient | gradient3 | rainbow
        std::string anim = "none";    // none | flow | cycle | pulse
        Rgb c1, c2, c3;
        double angle = 0;             // Grad, 0 = links -> rechts
        double cycle = 6;             // Sekunden pro Runde
        double sat = 0.85;            // Regenbogen-Sättigung
        double repeat = 1;            // Regenbogen-Wiederholungen über die Achse
    };

    Rgb HexRgb(const std::string &hex);
    AccentParams AccentFrom(const Settings &s);
    // Position eines Punkts auf der Verlaufsachse (0..1, Ecken exakt 0 bzw. 1)
    double AccentU(const AccentParams &p, double x, double y, double w, double h);
    // Farbe an Achsenposition u zur Zeit t (Sekunden)
    Rgb AccentAt(const AccentParams &p, double u, double t);

}
