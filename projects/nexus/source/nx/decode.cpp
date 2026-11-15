// NEXUS HOME: Bilddekoder (siehe decode.hpp)
#include "decode.hpp"
#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>

#if NEXUS_HAVE_JPEG
#include <jpeglib.h>
#endif
#if NEXUS_HAVE_PNG
#include <png.h>
#endif
#if NEXUS_HAVE_WEBP
#include <webp/decode.h>
#endif

// stb_image für GIF, BMP, PSD, HDR, PNM, TGA und als Rückfall (eigene, statische Kopie)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_LINEAR
#define STBI_NO_PIC
#include "stb_image.h"

namespace nexus {

    namespace {

        bool Starts(const uint8_t *d, size_t n, const char *m, size_t off = 0) {
            const size_t l = std::strlen(m);
            return n >= off + l && std::memcmp(d + off, m, l) == 0;
        }

        bool Fits(uint64_t w, uint64_t h, size_t memLimit) {
            return w > 0 && h > 0 && w <= 30000 && h <= 30000 && w * h * 4 <= memLimit;
        }

        // Verkleinern um ganzzahligen Faktor (Flächenmittel, Alpha-gewichtet: keine dunklen Ränder)
        void BoxReduce(DecodedImage &img, int f) {
            if(f <= 1) return;
            const int ow = std::max(1, img.w / f), oh = std::max(1, img.h / f);
            std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 4);
            for(int oy = 0; oy < oh; oy++) {
                for(int ox = 0; ox < ow; ox++) {
                    uint32_t sr = 0, sg = 0, sb = 0, sa = 0;
                    for(int y = 0; y < f; y++) {
                        const uint8_t *p = img.rgba.data() + (static_cast<size_t>(oy * f + y) * img.w + static_cast<size_t>(ox) * f) * 4;
                        for(int x = 0; x < f; x++, p += 4) { sr += p[0] * p[3]; sg += p[1] * p[3]; sb += p[2] * p[3]; sa += p[3]; }
                    }
                    uint8_t *q = out.data() + (static_cast<size_t>(oy) * ow + ox) * 4;
                    const uint32_t n = static_cast<uint32_t>(f * f);
                    if(sa > 0) { q[0] = static_cast<uint8_t>(sr / sa); q[1] = static_cast<uint8_t>(sg / sa); q[2] = static_cast<uint8_t>(sb / sa); }
                    else q[0] = q[1] = q[2] = 0;
                    q[3] = static_cast<uint8_t>(sa / n);
                }
            }
            img.rgba.swap(out);
            img.w = ow; img.h = oh;
        }

        int ReduceFactor(int w, int h, int maxEdge) {
            if(maxEdge <= 0) return 1;
            const int longest = std::max(w, h);
            // Ergebnis darf knapp (10 %) unter maxEdge liegen: deutlich schneller, kaum sichtbar
            return std::max(1, static_cast<int>(std::floor(longest / (maxEdge * 0.9))));
        }

#if NEXUS_HAVE_JPEG
        struct JpegErr {
            jpeg_error_mgr pub;
            jmp_buf jb;
            char msg[JMSG_LENGTH_MAX];
        };
        void JpegErrorExit(j_common_ptr c) {
            JpegErr *e = reinterpret_cast<JpegErr *>(c->err);
            (*c->err->format_message)(c, e->msg);
            std::longjmp(e->jb, 1);
        }
        void JpegSilent(j_common_ptr) {}

        DecodeStatus DecodeJpeg(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out) {
            // alle nicht-trivialen Objekte vor setjmp anlegen
            std::vector<uint8_t> row;
            jpeg_decompress_struct cinfo;
            JpegErr err;
            std::memset(&cinfo, 0, sizeof(cinfo));
            err.msg[0] = 0;
            cinfo.err = jpeg_std_error(&err.pub);
            err.pub.error_exit = JpegErrorExit;
            err.pub.output_message = JpegSilent;
            if(setjmp(err.jb)) {
                jpeg_destroy_decompress(&cinfo);
                out.error = std::string("JPEG fehlerhaft: ") + err.msg;
                return DecodeStatus::Failed;
            }
            jpeg_create_decompress(&cinfo);
            jpeg_mem_src(&cinfo, const_cast<unsigned char *>(data), static_cast<unsigned long>(size));
            jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
            jpeg_read_header(&cinfo, TRUE);
            out.srcW = static_cast<int>(cinfo.image_width);
            out.srcH = static_cast<int>(cinfo.image_height);
            int orientation = 1;
            for(jpeg_saved_marker_ptr m = cinfo.marker_list; m; m = m->next) {
                if(m->marker == JPEG_APP0 + 1) { const int o = ExifOrientation(m->data, m->data_length); if(o > 1) orientation = o; }
            }
            // DCT-Skalierung: 1/2, 1/4, 1/8 direkt beim Dekodieren (schnell und sparsam)
            unsigned denom = 1;
            if(maxEdge > 0) {
                const unsigned longest = std::max(cinfo.image_width, cinfo.image_height);
                for(unsigned dn: { 8u, 4u, 2u }) if(longest / dn >= maxEdge * 0.9) { denom = dn; break; }
            }
            cinfo.scale_num = 1;
            cinfo.scale_denom = denom;
            const bool cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
            cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
            jpeg_calc_output_dimensions(&cinfo);
            if(!Fits(cinfo.output_width, cinfo.output_height, memLimit)) {
                jpeg_destroy_decompress(&cinfo);
                out.error = "Bild zu groß (" + std::to_string(out.srcW) + "x" + std::to_string(out.srcH) + ")";
                return DecodeStatus::Failed;
            }
            jpeg_start_decompress(&cinfo);
            const int ow = static_cast<int>(cinfo.output_width), oh = static_cast<int>(cinfo.output_height), comps = cinfo.output_components;
            out.w = ow; out.h = oh;
            out.rgba.assign(static_cast<size_t>(ow) * oh * 4, 255);
            row.resize(static_cast<size_t>(ow) * comps);
            const bool adobe = cinfo.saw_Adobe_marker;
            while(cinfo.output_scanline < cinfo.output_height) {
                JSAMPROW rp = row.data();
                const int y = static_cast<int>(cinfo.output_scanline);
                if(jpeg_read_scanlines(&cinfo, &rp, 1) != 1) break;
                uint8_t *q = out.rgba.data() + static_cast<size_t>(y) * ow * 4;
                const uint8_t *p = row.data();
                if(comps == 3) for(int x = 0; x < ow; x++, p += 3, q += 4) { q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; }
                else if(comps == 4) {
                    // Adobe speichert CMYK invertiert
                    for(int x = 0; x < ow; x++, p += 4, q += 4) {
                        const int c = adobe ? p[0] : 255 - p[0], m = adobe ? p[1] : 255 - p[1], yy = adobe ? p[2] : 255 - p[2], k = adobe ? p[3] : 255 - p[3];
                        q[0] = static_cast<uint8_t>(c * k / 255); q[1] = static_cast<uint8_t>(m * k / 255); q[2] = static_cast<uint8_t>(yy * k / 255);
                    }
                }
                else if(comps == 1) for(int x = 0; x < ow; x++, p++, q += 4) q[0] = q[1] = q[2] = p[0];
            }
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            out.format = ImgFormat::Jpeg;
            ApplyOrientation(out, orientation);
            return DecodeStatus::Ok;
        }
#endif

#if NEXUS_HAVE_PNG
        struct PngSrc { const uint8_t *d; size_t n, pos; };
        struct PngErr { char msg[160]; };
        void PngRead(png_structp p, png_bytep dst, png_size_t len) {
            PngSrc *s = static_cast<PngSrc *>(png_get_io_ptr(p));
            if(s->pos + len > s->n) png_error(p, "Datei zu kurz");
            std::memcpy(dst, s->d + s->pos, len);
            s->pos += len;
        }
        void PngError(png_structp p, png_const_charp m) {
            PngErr *e = static_cast<PngErr *>(png_get_error_ptr(p));
            if(e) std::snprintf(e->msg, sizeof(e->msg), "%s", m);
            png_longjmp(p, 1);
        }
        void PngWarn(png_structp, png_const_charp) {}

        DecodeStatus DecodePng(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out) {
            std::vector<uint8_t> row;
            std::vector<uint32_t> acc;
            std::vector<png_bytep> rows;
            PngSrc src{ data, size, 0 };
            PngErr err{};
            png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &err, PngError, PngWarn);
            if(!png) { out.error = "PNG: kein Speicher"; return DecodeStatus::Failed; }
            png_infop info = png_create_info_struct(png);
            if(!info) { png_destroy_read_struct(&png, nullptr, nullptr); out.error = "PNG: kein Speicher"; return DecodeStatus::Failed; }
            if(setjmp(png_jmpbuf(png))) {
                png_destroy_read_struct(&png, &info, nullptr);
                out.error = std::string("PNG fehlerhaft: ") + err.msg;
                return DecodeStatus::Failed;
            }
            png_set_read_fn(png, &src, PngRead);
            png_set_crc_action(png, PNG_CRC_WARN_USE, PNG_CRC_WARN_USE);   // kleine Prüfsummenfehler hinnehmen
            png_read_info(png, info);
            const int w = static_cast<int>(png_get_image_width(png, info)), h = static_cast<int>(png_get_image_height(png, info));
            const int ct = png_get_color_type(png, info), bd = png_get_bit_depth(png, info);
            out.srcW = w; out.srcH = h;
            if(w <= 0 || h <= 0 || w > 30000 || h > 30000) png_error(png, "ungültige Größe");
            png_set_expand(png);
            if(bd == 16) png_set_strip_16(png);
            if(ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
            if(!(ct & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS)) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
            const int passes = png_set_interlace_handling(png);
            png_read_update_info(png, info);
            if(png_get_rowbytes(png, info) != static_cast<size_t>(w) * 4) png_error(png, "unerwartetes Pixelformat");
            const int f = ReduceFactor(w, h, maxEdge);
            if(passes > 1 || f == 1) {
                // Zeilensprung-PNG (oder klein): ganz einlesen
                if(!Fits(w, h, memLimit)) { png_destroy_read_struct(&png, &info, nullptr); out.error = "PNG zu groß (" + std::to_string(w) + "x" + std::to_string(h) + ", Zeilensprung)"; return DecodeStatus::Failed; }
                out.w = w; out.h = h;
                out.rgba.resize(static_cast<size_t>(w) * h * 4);
                rows.resize(h);
                for(int y = 0; y < h; y++) rows[y] = out.rgba.data() + static_cast<size_t>(y) * w * 4;
                png_read_image(png, rows.data());
                png_read_end(png, nullptr);
                png_destroy_read_struct(&png, &info, nullptr);
                BoxReduce(out, f);
            }
            else {
                // Zeile für Zeile lesen und gleich verkleinern: braucht nur das Ergebnis im Speicher
                const int ow = std::max(1, w / f), oh = std::max(1, h / f);
                if(!Fits(ow, oh, memLimit)) png_error(png, "zu groß");
                out.w = ow; out.h = oh;
                out.rgba.assign(static_cast<size_t>(ow) * oh * 4, 0);
                row.resize(static_cast<size_t>(w) * 4);
                acc.assign(static_cast<size_t>(ow) * 4, 0);
                const uint32_t n = static_cast<uint32_t>(f * f);
                for(int y = 0; y < h; y++) {
                    png_read_row(png, row.data(), nullptr);
                    const int oy = y / f;
                    if(oy >= oh) continue;
                    const uint8_t *p = row.data();
                    for(int ox = 0; ox < ow; ox++) {
                        uint32_t *a = acc.data() + static_cast<size_t>(ox) * 4;
                        for(int x = 0; x < f; x++, p += 4) { a[0] += p[0] * p[3]; a[1] += p[1] * p[3]; a[2] += p[2] * p[3]; a[3] += p[3]; }
                    }
                    if(y % f == f - 1) {
                        uint8_t *q = out.rgba.data() + static_cast<size_t>(oy) * ow * 4;
                        for(int ox = 0; ox < ow; ox++, q += 4) {
                            const uint32_t *a = acc.data() + static_cast<size_t>(ox) * 4;
                            if(a[3] > 0) { q[0] = static_cast<uint8_t>(a[0] / a[3]); q[1] = static_cast<uint8_t>(a[1] / a[3]); q[2] = static_cast<uint8_t>(a[2] / a[3]); }
                            q[3] = static_cast<uint8_t>(a[3] / n);
                        }
                        std::fill(acc.begin(), acc.end(), 0);
                    }
                }
                png_read_end(png, nullptr);
                png_destroy_read_struct(&png, &info, nullptr);
            }
            out.format = ImgFormat::Png;
            return DecodeStatus::Ok;
        }
#endif

#if NEXUS_HAVE_WEBP
        DecodeStatus DecodeWebp(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out) {
            WebPDecoderConfig cfg;
            if(!WebPInitDecoderConfig(&cfg)) { out.error = "WebP: Dekoder nicht verfügbar"; return DecodeStatus::Failed; }
            if(WebPGetFeatures(data, size, &cfg.input) != VP8_STATUS_OK) { out.error = "WebP fehlerhaft"; return DecodeStatus::Failed; }
            if(cfg.input.has_animation) return DecodeStatus::NotHandled;   // animiertes WebP: SDL_image versuchen
            const int w = cfg.input.width, h = cfg.input.height;
            out.srcW = w; out.srcH = h;
            int ow = w, oh = h;
            if(maxEdge > 0 && std::max(w, h) > maxEdge) {
                const double k = static_cast<double>(maxEdge) / std::max(w, h);
                ow = std::max(1, static_cast<int>(std::lround(w * k)));
                oh = std::max(1, static_cast<int>(std::lround(h * k)));
                cfg.options.use_scaling = 1;
                cfg.options.scaled_width = ow;
                cfg.options.scaled_height = oh;
            }
            if(!Fits(ow, oh, memLimit)) { out.error = "WebP zu groß"; return DecodeStatus::Failed; }
            out.rgba.assign(static_cast<size_t>(ow) * oh * 4, 0);
            cfg.output.colorspace = MODE_RGBA;
            cfg.output.is_external_memory = 1;
            cfg.output.u.RGBA.rgba = out.rgba.data();
            cfg.output.u.RGBA.stride = ow * 4;
            cfg.output.u.RGBA.size = out.rgba.size();
            const VP8StatusCode st = WebPDecode(data, size, &cfg);
            WebPFreeDecBuffer(&cfg.output);
            if(st != VP8_STATUS_OK) { out.rgba.clear(); out.error = "WebP fehlerhaft (" + std::to_string(static_cast<int>(st)) + ")"; return DecodeStatus::Failed; }
            out.w = ow; out.h = oh;
            out.format = ImgFormat::Webp;
            return DecodeStatus::Ok;
        }
#endif

        DecodeStatus DecodeStb(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out) {
            int w = 0, h = 0, c = 0;
            if(size > 0x7FFFFFFF || !stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &c)) return DecodeStatus::NotHandled;
            out.srcW = w; out.srcH = h;
            if(!Fits(w, h, memLimit)) { out.error = "Bild zu groß (" + std::to_string(w) + "x" + std::to_string(h) + ")"; return DecodeStatus::Failed; }
            unsigned char *px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &c, 4);
            if(!px) { const char *r = stbi_failure_reason(); out.error = std::string("Bild fehlerhaft") + (r ? std::string(": ") + r : ""); return DecodeStatus::Failed; }
            out.w = w; out.h = h;
            out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
            stbi_image_free(px);
            BoxReduce(out, ReduceFactor(w, h, maxEdge));   // grob vorverkleinern, fein macht der Aufrufer
            return DecodeStatus::Ok;
        }

        uint16_t Rd16(const uint8_t *p, bool le) { return le ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : static_cast<uint16_t>((p[0] << 8) | p[1]); }
        uint32_t Rd32(const uint8_t *p, bool le) {
            return le ? static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24))
                      : static_cast<uint32_t>((static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
        }

    }

    ImgFormat SniffFormat(const uint8_t *d, size_t n) {
        if(n < 4) return ImgFormat::Unknown;
        if(d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ImgFormat::Jpeg;
        if(d[0] == 0x89 && Starts(d, n, "PNG", 1)) return ImgFormat::Png;
        if(Starts(d, n, "RIFF") && Starts(d, n, "WEBP", 8)) return ImgFormat::Webp;
        if(Starts(d, n, "GIF8")) return ImgFormat::Gif;
        if(Starts(d, n, "BM")) return ImgFormat::Bmp;
        if(Starts(d, n, "8BPS")) return ImgFormat::Psd;
        if(Starts(d, n, "#?RADIANCE") || Starts(d, n, "#?RGBE")) return ImgFormat::Hdr;
        if(d[0] == 'P' && d[1] >= '1' && d[1] <= '7') return ImgFormat::Pnm;
        if(Starts(d, n, "qoif")) return ImgFormat::Qoi;
        if(d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0) return ImgFormat::Ico;
        if(Starts(d, n, "II*") || (Starts(d, n, "MM") && d[2] == 0 && d[3] == 42)) return ImgFormat::Tiff;
        if((d[0] == 0xFF && d[1] == 0x0A) || Starts(d, n, "JXL ", 4)) return ImgFormat::Jxl;
        if(Starts(d, n, "ftyp", 4)) {
            const size_t lim = std::min<size_t>(n, 64);
            for(size_t i = 8; i + 4 <= lim; i += 4) if(Starts(d, lim, "avif", i) || Starts(d, lim, "avis", i)) return ImgFormat::Avif;
            return ImgFormat::Heic;
        }
        // SVG: Text, der früh "<svg" enthält
        const size_t lim = std::min<size_t>(n, 1024);
        for(size_t i = 0; i + 4 <= lim; i++) if(std::memcmp(d + i, "<svg", 4) == 0) return ImgFormat::Svg;
        return ImgFormat::Unknown;
    }

    const char *FormatName(ImgFormat f) {
        switch(f) {
            case ImgFormat::Jpeg: return "JPEG"; case ImgFormat::Png: return "PNG"; case ImgFormat::Webp: return "WebP";
            case ImgFormat::Gif: return "GIF"; case ImgFormat::Bmp: return "BMP"; case ImgFormat::Psd: return "PSD";
            case ImgFormat::Hdr: return "HDR"; case ImgFormat::Pnm: return "PNM"; case ImgFormat::Qoi: return "QOI";
            case ImgFormat::Svg: return "SVG"; case ImgFormat::Ico: return "ICO"; case ImgFormat::Heic: return "HEIC";
            case ImgFormat::Avif: return "AVIF"; case ImgFormat::Tiff: return "TIFF"; case ImgFormat::Jxl: return "JPEG XL";
            default: return "unbekannt";
        }
    }

    int ExifOrientation(const uint8_t *d, size_t n) {
        if(n < 14 || std::memcmp(d, "Exif\0\0", 6) != 0) return 1;
        const uint8_t *t = d + 6;
        const size_t tn = n - 6;
        bool le;
        if(t[0] == 'I' && t[1] == 'I') le = true;
        else if(t[0] == 'M' && t[1] == 'M') le = false;
        else return 1;
        const uint32_t ifd = Rd32(t + 4, le);
        if(ifd + 2 > tn) return 1;
        const uint16_t cnt = Rd16(t + ifd, le);
        for(uint16_t i = 0; i < cnt; i++) {
            const size_t e = ifd + 2 + static_cast<size_t>(i) * 12;
            if(e + 12 > tn) break;
            if(Rd16(t + e, le) == 0x0112) {
                const int v = Rd16(t + e + 8, le);
                return v >= 1 && v <= 8 ? v : 1;
            }
        }
        return 1;
    }

    void ApplyOrientation(DecodedImage &img, int o) {
        if(o <= 1 || o > 8 || img.rgba.empty()) return;
        const int w = img.w, h = img.h;
        const bool swap = o >= 5;
        const int nw = swap ? h : w, nh = swap ? w : h;
        std::vector<uint8_t> out(img.rgba.size());
        for(int y = 0; y < h; y++) {
            for(int x = 0; x < w; x++) {
                int nx = x, ny = y;
                switch(o) {
                    case 2: nx = w - 1 - x; break;                       // gespiegelt
                    case 3: nx = w - 1 - x; ny = h - 1 - y; break;       // 180°
                    case 4: ny = h - 1 - y; break;                       // senkrecht gespiegelt
                    case 5: nx = y; ny = x; break;                       // transponiert
                    case 6: nx = h - 1 - y; ny = x; break;               // 90° im Uhrzeigersinn
                    case 7: nx = h - 1 - y; ny = w - 1 - x; break;       // quer transponiert
                    case 8: nx = y; ny = w - 1 - x; break;               // 90° gegen den Uhrzeigersinn
                }
                std::memcpy(out.data() + (static_cast<size_t>(ny) * nw + nx) * 4, img.rgba.data() + (static_cast<size_t>(y) * w + x) * 4, 4);
            }
        }
        img.rgba.swap(out);
        img.w = nw; img.h = nh;
    }

    DecodeStatus DecodeImage(const uint8_t *data, size_t size, int maxEdge, size_t memLimit, DecodedImage &out) {
        out = DecodedImage{};
        if(!data || size < 8) { out.error = "Datei leer oder zu kurz"; return DecodeStatus::Failed; }
        out.format = SniffFormat(data, size);
        switch(out.format) {
            case ImgFormat::Heic: out.error = "HEIC (iPhone-Foto) wird nicht unterstützt – bitte als JPG speichern"; return DecodeStatus::Failed;
            case ImgFormat::Avif: out.error = "AVIF wird nicht unterstützt – bitte als JPG oder PNG speichern"; return DecodeStatus::Failed;
            case ImgFormat::Tiff: out.error = "TIFF wird nicht unterstützt – bitte als JPG oder PNG speichern"; return DecodeStatus::Failed;
            case ImgFormat::Jxl: out.error = "JPEG XL wird nicht unterstützt – bitte als JPG oder PNG speichern"; return DecodeStatus::Failed;
            default: break;
        }
        DecodeStatus st = DecodeStatus::NotHandled;
#if NEXUS_HAVE_JPEG
        if(out.format == ImgFormat::Jpeg) {
            st = DecodeJpeg(data, size, maxEdge, memLimit, out);
            if(st == DecodeStatus::Ok) return st;
        }
#endif
#if NEXUS_HAVE_PNG
        if(out.format == ImgFormat::Png) {
            st = DecodePng(data, size, maxEdge, memLimit, out);
            if(st == DecodeStatus::Ok) return st;
        }
#endif
#if NEXUS_HAVE_WEBP
        if(out.format == ImgFormat::Webp) {
            return DecodeWebp(data, size, maxEdge, memLimit, out);
        }
#endif
        if(out.format == ImgFormat::Qoi || out.format == ImgFormat::Svg || out.format == ImgFormat::Ico || out.format == ImgFormat::Webp) return DecodeStatus::NotHandled;
        // GIF, BMP, PSD, HDR, PNM, TGA – und Rückfall, wenn libjpeg/libpng an einer leicht beschädigten Datei scheitern
        const std::string firstErr = out.error;
        const ImgFormat fmt = out.format;
        const DecodeStatus s2 = DecodeStb(data, size, maxEdge, memLimit, out);
        out.format = fmt;
        if(s2 == DecodeStatus::Ok) return s2;
        if(!firstErr.empty() && out.error.empty()) out.error = firstErr;
        if(s2 == DecodeStatus::NotHandled && st == DecodeStatus::Failed) return DecodeStatus::Failed;
        return s2;
    }

    DecodeStatus DecodeImageFile(const std::string &path, int maxEdge, size_t memLimit, DecodedImage &out) {
        out = DecodedImage{};
        FILE *f = std::fopen(path.c_str(), "rb");
        if(!f) { out.error = "Datei nicht gefunden"; return DecodeStatus::Failed; }
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if(n <= 0 || n > 96L * 1024 * 1024 || static_cast<size_t>(n) > memLimit) {
            std::fclose(f);
            out.error = n <= 0 ? "Datei leer" : "Datei zu groß (" + std::to_string(n >> 20) + " MB)";
            return DecodeStatus::Failed;
        }
        std::vector<uint8_t> buf(static_cast<size_t>(n));
        const size_t got = std::fread(buf.data(), 1, buf.size(), f);
        std::fclose(f);
        if(got != buf.size()) { out.error = "Datei nicht lesbar"; return DecodeStatus::Failed; }
        return DecodeImage(buf.data(), buf.size(), maxEdge, memLimit - buf.size(), out);
    }

}
