// Tests für den Bilddekoder (nx/decode.cpp) mit echten Dateien aus tests/images
#include "decode.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace nexus;
static int fails = 0;
#define CHECK(c) do { if(!(c)) { std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while(0)
static std::string dir;
static DecodeStatus Load(const char *n, DecodedImage &o, int edge = 1600, size_t mem = 128u << 20) {
    const DecodeStatus s = DecodeImageFile(dir + n, edge, mem, o);
    std::printf("%-16s %-10s %5dx%-5d (Original %dx%d) %s\n", n, s == DecodeStatus::Ok ? "ok" : s == DecodeStatus::Failed ? "FEHLER" : "SDL", o.w, o.h, o.srcW, o.srcH, o.error.c_str());
    return s;
}
static const uint8_t *Px(const DecodedImage &o, int x, int y) { return o.rgba.data() + (static_cast<size_t>(y) * o.w + x) * 4; }
static bool Near(const uint8_t *p, int r, int g, int b, int tol = 40) { return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol; }
int main(int argc, char **argv) {
    dir = argc > 1 ? std::string(argv[1]) + "/" : "nexus/tests/images/";
    DecodedImage o;
    // JPEG
    CHECK(Load("rgb.jpg", o) == DecodeStatus::Ok && o.w == 640 && o.h == 480 && o.format == ImgFormat::Jpeg);
    if(!o.rgba.empty()) { CHECK(Near(Px(o, 10, 10), 255, 0, 0)); CHECK(Near(Px(o, 630, 470), 255, 255, 255)); }
    CHECK(Load("prog.jpg", o) == DecodeStatus::Ok && o.w >= 1440 && o.w <= 3000 && o.srcW == 3000);
    if(!o.rgba.empty()) CHECK(Near(Px(o, o.w - 5, 5), 0, 255, 0));
    CHECK(Load("big.jpg", o) == DecodeStatus::Ok && o.w >= 1440 && o.w <= 3000 && o.srcW == 6000);
#if NEXUS_HAVE_JPEG
    CHECK(o.w == 1500 && o.h == 1000);   // DCT-Skalierung 1/4
#endif
    CHECK(Load("big.jpg", o, 256) == DecodeStatus::Ok && o.w <= 1600);
#if NEXUS_HAVE_JPEG
    CHECK(o.w == 750);
    CHECK(Load("big.jpg", o, 1600, 4u << 20) == DecodeStatus::Failed);   // Speichergrenze wird eingehalten
#endif
    CHECK(Load("cmyk.jpg", o) == DecodeStatus::Ok && o.w == 400);
    if(!o.rgba.empty()) { CHECK(Near(Px(o, 10, 10), 255, 0, 0, 70)); CHECK(Near(Px(o, 390, 290), 255, 255, 255, 70)); }
    CHECK(Load("gray.jpg", o) == DecodeStatus::Ok && o.w == 320);
    if(!o.rgba.empty()) CHECK(Px(o, 10, 10)[0] == Px(o, 10, 10)[1] && Px(o, 10, 10)[1] == Px(o, 10, 10)[2]);
#if NEXUS_HAVE_JPEG
    CHECK(Load("exif6.jpg", o) == DecodeStatus::Ok && o.w == 200 && o.h == 400);   // gedreht wie auf dem Handy
    if(!o.rgba.empty()) { CHECK(Near(Px(o, 190, 10), 255, 0, 0)); CHECK(Near(Px(o, 10, 10), 0, 0, 255)); }
#endif
    CHECK(Load("truncated.jpg", o) != DecodeStatus::NotHandled);   // darf nicht abstürzen
    // PNG
    CHECK(Load("rgba.png", o) == DecodeStatus::Ok && o.w == 256);
    if(!o.rgba.empty()) { CHECK(Px(o, 10, 10)[3] == 0); CHECK(Near(Px(o, 200, 10), 0, 255, 0, 2) && Px(o, 200, 10)[3] == 255); }
    CHECK(Load("pal.png", o) == DecodeStatus::Ok && o.w == 200 && o.h == 100);
    if(!o.rgba.empty()) CHECK(Px(o, 10, 10)[3] == 0 || Px(o, 190, 10)[3] == 0 || Px(o, 10, 90)[3] == 0 || Px(o, 190, 90)[3] == 0);
    CHECK(Load("gray16.png", o) == DecodeStatus::Ok && o.w == 64);
    if(!o.rgba.empty()) CHECK(std::abs(Px(o, 5, 5)[0] - 156) <= 2 && Px(o, 5, 5)[0] == Px(o, 5, 5)[2]);
    CHECK(Load("la.png", o) == DecodeStatus::Ok);
    if(!o.rgba.empty()) CHECK(Near(Px(o, 5, 5), 200, 200, 200, 1) && Px(o, 5, 5)[3] == 128);
    CHECK(Load("big.png", o) == DecodeStatus::Ok && o.srcW == 5000 && o.w <= 1700 && o.w >= 1440);
    if(!o.rgba.empty()) { CHECK(Near(Px(o, 10, 10), 255, 0, 0, 2)); CHECK(Near(Px(o, o.w - 10, o.h - 10), 255, 255, 255, 2)); CHECK(Near(Px(o, 10, o.h - 10), 0, 0, 255, 2)); }
    CHECK(Load("big.png", o, 256) == DecodeStatus::Ok && o.w <= 300);
    CHECK(Load("interlaced.png", o) == DecodeStatus::Ok && o.w == 300 && o.h == 200);
    if(!o.rgba.empty()) { CHECK(Near(Px(o, 10, 10), 255, 0, 0, 2)); CHECK(Near(Px(o, 290, 190), 255, 255, 255, 2)); }
    CHECK(Load("corrupt.png", o) != DecodeStatus::NotHandled);
    // WebP
#if NEXUS_HAVE_WEBP
    CHECK(Load("lossy.webp", o) == DecodeStatus::Ok && o.w == 800 && o.h == 600);
    if(!o.rgba.empty()) CHECK(Near(Px(o, 10, 10), 255, 0, 0));
    CHECK(Load("lossy.webp", o, 400) == DecodeStatus::Ok && o.w == 400 && o.h == 300);   // skaliert dekodiert
    CHECK(Load("alpha.webp", o) == DecodeStatus::Ok && o.w == 300);
#else
    CHECK(Load("lossy.webp", o) == DecodeStatus::NotHandled);
#endif
    // weitere Formate
    for(const char *n: { "img.gif", "img.bmp", "img.tga" }) {
        CHECK(Load(n, o) == DecodeStatus::Ok && o.w == 120 && o.h == 90);
        if(!o.rgba.empty()) CHECK(Near(Px(o, 5, 5), 255, 0, 0, 10));
    }
    CHECK(Load("fake.heic", o) == DecodeStatus::Failed && o.error.find("HEIC") != std::string::npos);
    CHECK(Load("fake.avif", o) == DecodeStatus::Failed && o.error.find("AVIF") != std::string::npos);
    CHECK(Load("notimage.png", o) == DecodeStatus::NotHandled);
    CHECK(Load("fehlt.png", o) == DecodeStatus::Failed);
    // EXIF-Parser direkt
    const uint8_t ex[] = { 'E','x','i','f',0,0, 'I','I',42,0, 8,0,0,0, 1,0, 0x12,0x01, 3,0, 1,0,0,0, 8,0,0,0 };
    CHECK(ExifOrientation(ex, sizeof(ex)) == 8);
    CHECK(ExifOrientation(ex, 10) == 1);
    if(fails) { std::printf("%d Bild-Tests fehlgeschlagen.\n", fails); return 1; }
    std::printf("Alle Bild-Tests bestanden.\n");
    return 0;
}
