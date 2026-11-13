#pragma once
#include <SDL2/SDL.h>
// Nur Vorwärtsdeklaration: uMenu/Plutonium bringt eine eigene SDL_ttf-Variante mit, deren Header mit dem
// Standard-Header kollidiert. Die eigentlichen TTF-Aufrufe stehen in gfx.cpp.
struct _TTF_Font;
typedef struct _TTF_Font TTF_Font;
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus {

    struct Col {
        uint8_t r = 255, g = 255, b = 255, a = 255;
    };

    Col ParseCol(const std::string &hex, uint8_t a = 255);
    Col WithA(Col c, double alpha);
    Col Lerp(Col a, Col b, double t);

    // Transformation einer Kachel: Mittelpunkt, Größe, Drehung (Grad), horizontale Stauchung (Pseudo-3D), Skalierung
    struct Xf {
        double cx = 0, cy = 0, w = 0, h = 0, rot = 0, squash = 1, sc = 1;
    };

    class Gfx {
        public:
            SDL_Renderer *r = nullptr;

            bool Init(SDL_Renderer *renderer, const void *font, size_t fontSize);
            void Exit();
            void BeginFrame();

            void Rect(double x, double y, double w, double h, Col c);
            void GradV(double x, double y, double w, double h, Col top, Col bottom);
            void GradH(double x, double y, double w, double h, Col left, Col right);
            void RoundRect(double x, double y, double w, double h, double rad, Col c);
            void RoundRectOutline(double x, double y, double w, double h, double rad, double t, Col c);
            void Circle(double cx, double cy, double rad, Col c);
            void Line(double x1, double y1, double x2, double y2, double t, Col c);

            // Formen mit Transformation (abgerundetes Rechteck oder Sechseck)
            void Shape(const Xf &xf, double rad, bool hex, Col c);
            void ShapeTex(SDL_Texture *tex, const Xf &xf, double rad, bool hex, double alpha, bool cover, double posX, double posY, Col tint = Col{});
            void ShapeRing(const Xf &xf, double rad, bool hex, double t, Col c);

            // Bild in Rechteck (cover = zuschneiden, sonst einpassen)
            void Image(SDL_Texture *tex, double x, double y, double w, double h, double alpha, bool cover, Col tint = Col{});

            // Text (UTF-8, gecacht). align: 0 links, 1 Mitte, 2 rechts. Gibt Breite zurück.
            int Text(const std::string &s, int size, double x, double y, Col c, int align = 0, int maxW = 0, bool bold = false);
            int TextW(const std::string &s, int size, bool bold = false);
            SDL_Texture *TextTex(const std::string &s, int size, Col c, int outline, Col outlineCol, int &w, int &h, bool bold = true);

            void Icon(const std::string &name, double cx, double cy, double size, Col c, bool filled);
            void Battery(double x, double y, double size, int pct, bool charging, Col c);
            void Wifi(double cx, double cy, double size, int bars, Col c);
            void Button(const std::string &label, double cx, double cy, double size, Col bg, Col fg);

        private:
            const void *fontData = nullptr;
            size_t fontLen = 0;
            std::map<int, TTF_Font *> fonts;
            struct TextEntry { SDL_Texture *tex; int w, h; uint64_t used; };
            std::unordered_map<std::string, TextEntry> textCache;
            uint64_t frame = 0;
            std::vector<SDL_Vertex> vbuf;
            std::vector<int> ibuf;

            TTF_Font *Font(int size, bool bold);
            void ShapePoints(const Xf &xf, double rad, bool hex, double grow, std::vector<SDL_FPoint> &pts, std::vector<SDL_FPoint> *uv, bool cover, double texW, double texH, double posX, double posY);
            void Fan(SDL_Texture *tex, const std::vector<SDL_FPoint> &pts, const std::vector<SDL_FPoint> *uv, SDL_FPoint center, SDL_FPoint centerUv, Col c);
    };

}
