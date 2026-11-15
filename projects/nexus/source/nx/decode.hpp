#pragma once
// NEXUS HOME: Bilddekoder ohne SDL. Verkleinert schon beim Dekodieren (JPEG über die DCT-Skalierung von
// libjpeg-turbo, PNG zeilenweise, WebP über die Skalierung von libwebp), damit auch große Fotos schnell
// und mit wenig Speicher geladen werden. Ergebnis immer RGBA (8 Bit, nicht vormultipliziert).
#ifndef NEXUS_HAVE_JPEG
#define NEXUS_HAVE_JPEG 1
#endif
#ifndef NEXUS_HAVE_PNG
#define NEXUS_HAVE_PNG 1
#endif
#ifndef NEXUS_HAVE_WEBP
#define NEXUS_HAVE_WEBP 1
#endif
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus {

    enum class ImgFormat { Unknown, Jpeg, Png, Webp, Gif, Bmp, Psd, Hdr, Pnm, Qoi, Svg, Ico, Heic, Avif, Tiff, Jxl };

    // Format am Dateiinhalt erkennen (nicht an der Endung)
    ImgFormat SniffFormat(const uint8_t *data, size_t size);
    const char *FormatName(ImgFormat f);

    enum class DecodeStatus { Ok, NotHandled, Failed };

    struct DecodedImage {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;   // w * h * 4
        ImgFormat format = ImgFormat::Unknown;
        int srcW = 0, srcH = 0;      // Originalgröße
        std::string error;           // bei Failed: verständlicher Grund
    };

    // maxEdge: längste Seite des Ergebnisses (ungefähr, höchstens etwas darüber; der Aufrufer verkleinert genau).
    // memLimit: höchstens so viel Zwischenspeicher. NotHandled = anderes Format (z. B. QOI/SVG) -> SDL_image versuchen.
    DecodeStatus DecodeImage(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out);
    DecodeStatus DecodeImageFile(const std::string &path, int maxEdge, size_t memLimit, DecodedImage &out);

    // EXIF-Ausrichtung (1..8) anwenden (für Handyfotos)
    void ApplyOrientation(DecodedImage &img, int orientation);
    int ExifOrientation(const uint8_t *app1, size_t size);

}
