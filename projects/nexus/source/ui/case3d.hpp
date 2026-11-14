#pragma once
#include "../nx/gfx.hpp"
#include <string>

namespace nexus {

    // Eingaben für eine 3D-Hülle (aus Kachel-Einstellungen und Animationszustand)
    struct CaseInput {
        std::string style = "dvd", title;
        bool fit = true, shine = true, reflect = false, spineText = true, shadow = true, live = true, focused = false;
        double depth = 12;          // 12 = echte Dicke
        double angle = 18;          // Drehung (Grad, + = Rücken links sichtbar)
        double lean = 0, lift = 0;  // Schwung-Neigung (Grad), nach vorn (px, negativ = näher)
        double time = 0, op = 1;
        double posX = 50, posY = 50, zoom = 1;
        // Ausrichtung
        bool spineRight = false;    // Rücken rechts statt links
        int align = 1;              // 1 = unten, 0 = Mitte, -1 = oben (wenn die Hülle kleiner als die Kachel ist)
        double tilt = 0;            // Neigung vor/zurück (Grad)
        double camUp = 240;         // Blickhöhe der Kamera über der Bildmitte (px)
        double persp = 1;           // Stärke der Perspektive
    };

    // Zeichnet die Hülle in die Kachel xf; xf wird danach auf die sichtbare Vorderseite gesetzt
    // outQuad (optional): die vier Ecken der Vorderseite auf dem Bildschirm (für die Auswahlhervorhebung)
    void RenderCase(Gfx &gfx, SDL_Texture *tex, Xf &xf, const CaseInput &in, SDL_FPoint *outQuad = nullptr);

}
