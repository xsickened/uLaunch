// NEXUS HOME: Spielerkennung über den ROM-Prüfwert (siehe romhash.hpp)
#include "romhash.hpp"
#include "online_parse.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace nexus {

    // ------------------------------------------------------------------ MD5 (RFC 1321)
    namespace {
        constexpr uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
        constexpr int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
        inline uint32_t Rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
    }

    Md5::Md5() : a(0x67452301), b(0xefcdab89), c(0x98badcfe), d(0x10325476) {}

    void Md5::Block(const uint8_t *p) {
        uint32_t m[16];
        for(int i = 0; i < 16; i++) m[i] = p[i * 4] | (p[i * 4 + 1] << 8) | (p[i * 4 + 2] << 16) | (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
        uint32_t A = a, B = b, C = c, D = d;
        for(int i = 0; i < 64; i++) {
            uint32_t F;
            int g;
            if(i < 16) { F = (B & C) | (~B & D); g = i; }
            else if(i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if(i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + m[g];
            A = D; D = C; C = B;
            B = B + Rol(F, R[i]);
        }
        a += A; b += B; c += C; d += D;
    }

    void Md5::Update(const void *data, size_t len) {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        this->total += len;
        while(len > 0) {
            const size_t n = std::min(len, 64 - this->bufLen);
            std::memcpy(this->buf + this->bufLen, p, n);
            this->bufLen += n; p += n; len -= n;
            if(this->bufLen == 64) { this->Block(this->buf); this->bufLen = 0; }
        }
    }

    std::string Md5::HexDigest() {
        const uint64_t bits = this->total * 8;
        const uint8_t pad = 0x80, zero = 0;
        this->Update(&pad, 1);
        while(this->bufLen != 56) this->Update(&zero, 1);
        uint8_t len[8];
        for(int i = 0; i < 8; i++) len[i] = static_cast<uint8_t>(bits >> (8 * i));
        this->Update(len, 8);
        char out[33];
        const uint32_t v[4] = { a, b, c, d };
        for(int i = 0; i < 16; i++) std::snprintf(out + i * 2, 3, "%02x", static_cast<unsigned>((v[i / 4] >> (8 * (i % 4))) & 0xff));
        return std::string(out, 32);
    }

    std::string Md5Hex(const void *data, size_t len) { Md5 m; m.Update(data, len); return m.HexDigest(); }

    // ------------------------------------------------------------------ Systeme
    namespace {
        struct SysInfo { const char *id; int console; bool hash; };
        // Konsolen-IDs von RetroAchievements
        const SysInfo Systems[] = {
            { "md", 1, true }, { "n64", 2, true }, { "snes", 3, true }, { "gb", 4, true }, { "gba", 5, true }, { "gbc", 6, true },
            { "nes", 7, true }, { "pce", 8, true }, { "segacd", 9, false }, { "32x", 10, true }, { "sms", 11, true }, { "psx", 12, false },
            { "lynx", 13, true }, { "ngp", 14, true }, { "gg", 15, true }, { "gc", 16, false }, { "nds", 18, true }, { "wii", 19, false },
            { "ps2", 21, false }, { "a2600", 25, true }, { "arcade", 27, true }, { "vb", 28, true }, { "msx", 29, true },
            { "sg1000", 33, true }, { "coleco", 44, true }, { "a7800", 51, true }, { "ws", 53, true }, { "saturn", 39, false },
            { "dc", 40, false }, { "psp", 41, false }, { "pokemini", 24, true }, { "3ds", 62, false }, { "fds", 7, true },
        };
        const SysInfo *Find(const std::string &s) { for(const auto &x: Systems) if(s == x.id) return &x; return nullptr; }
        std::string Lower(std::string s) { for(auto &c: s) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return s; }
    }

    std::string RaSystemFor(const std::string &folderIn, const std::string &extIn) {
        static const std::map<std::string, std::string> folders = {
            { "nes", "nes" }, { "fc", "nes" }, { "famicom", "nes" }, { "fds", "fds" }, { "snes", "snes" }, { "sfc", "snes" }, { "superfamicom", "snes" },
            { "n64", "n64" }, { "gb", "gb" }, { "gameboy", "gb" }, { "gbc", "gbc" }, { "gba", "gba" }, { "nds", "nds" }, { "ds", "nds" },
            { "vb", "vb" }, { "virtualboy", "vb" }, { "md", "md" }, { "genesis", "md" }, { "megadrive", "md" }, { "sms", "sms" }, { "mastersystem", "sms" },
            { "gg", "gg" }, { "gamegear", "gg" }, { "32x", "32x" }, { "sega32x", "32x" }, { "sg1000", "sg1000" }, { "pce", "pce" }, { "tg16", "pce" },
            { "pcengine", "pce" }, { "turbografx16", "pce" }, { "lynx", "lynx" }, { "atarilynx", "lynx" }, { "atari2600", "a2600" }, { "a2600", "a2600" },
            { "atari7800", "a7800" }, { "a7800", "a7800" }, { "ngp", "ngp" }, { "ngpc", "ngp" }, { "neogeopocket", "ngp" }, { "ws", "ws" }, { "wsc", "ws" },
            { "wonderswan", "ws" }, { "coleco", "coleco" }, { "colecovision", "coleco" }, { "msx", "msx" }, { "msx2", "msx" }, { "pokemini", "pokemini" },
            { "psx", "psx" }, { "ps1", "psx" }, { "playstation", "psx" }, { "ps2", "ps2" }, { "psp", "psp" }, { "dc", "dc" }, { "dreamcast", "dc" },
            { "saturn", "saturn" }, { "segacd", "segacd" }, { "megacd", "segacd" }, { "gc", "gc" }, { "gamecube", "gc" }, { "ngc", "gc" }, { "wii", "wii" },
            { "3ds", "3ds" }, { "arcade", "arcade" }, { "mame", "arcade" }, { "fbneo", "arcade" }, { "fba", "arcade" }, { "neogeo", "arcade" }, { "cps1", "arcade" },
            { "cps2", "arcade" }, { "cps3", "arcade" },
        };
        static const std::map<std::string, std::string> exts = {
            { "nes", "nes" }, { "unf", "nes" }, { "fds", "fds" }, { "sfc", "snes" }, { "smc", "snes" }, { "z64", "n64" }, { "n64", "n64" }, { "v64", "n64" },
            { "gb", "gb" }, { "gbc", "gbc" }, { "gba", "gba" }, { "nds", "nds" }, { "vb", "vb" }, { "md", "md" }, { "gen", "md" }, { "sms", "sms" },
            { "gg", "gg" }, { "32x", "32x" }, { "sg", "sg1000" }, { "pce", "pce" }, { "lnx", "lynx" }, { "a26", "a2600" }, { "a78", "a7800" },
            { "ngp", "ngp" }, { "ngc", "ngp" }, { "ws", "ws" }, { "wsc", "ws" }, { "col", "coleco" }, { "min", "pokemini" },
        };
        const std::string f = Lower(folderIn);
        auto it = folders.find(f);
        if(it != folders.end()) return it->second;
        auto e = exts.find(Lower(extIn));
        return e != exts.end() ? e->second : "";
    }

    int RaConsoleId(const std::string &system) { const SysInfo *s = Find(system); return s ? s->console : 0; }
    bool RaHashSupported(const std::string &system) { const SysInfo *s = Find(system); return s && s->hash; }

    // ------------------------------------------------------------------ Prüfwert
    namespace {

        // Wie viele Bytes am Anfang zählen nicht mit (Kopierer-/Emulator-Header)?
        size_t HeaderSkip(const std::string &sys, const uint8_t *head, size_t headLen, uint64_t size) {
            if((sys == "nes" || sys == "fds") && headLen >= 16 && (std::memcmp(head, "NES\x1a", 4) == 0 || std::memcmp(head, "FDS\x1a", 4) == 0)) return 16;
            if((sys == "snes" || sys == "pce") && size % 8192 == 512) return 512;
            if(sys == "lynx" && headLen >= 5 && std::memcmp(head, "LYNX\0", 5) == 0) return 64;
            if(sys == "a7800" && headLen >= 10 && std::memcmp(head + 1, "ATARI7800", 9) == 0) return 128;
            return 0;
        }

        // N64: immer im „big endian“-Format (.z64) prüfen
        int N64Order(const uint8_t *h, size_t n) {
            if(n < 4) return 0;
            if(h[0] == 0x80 && h[1] == 0x37) return 0;   // z64
            if(h[0] == 0x37 && h[1] == 0x80) return 1;   // v64: 16-Bit vertauscht
            if(h[0] == 0x40 && h[1] == 0x12) return 2;   // n64: 32-Bit vertauscht
            return 0;
        }
        void N64Fix(uint8_t *p, size_t n, int order) {
            if(order == 1) for(size_t i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i + 1]);
            else if(order == 2) for(size_t i = 0; i + 3 < n; i += 4) { std::swap(p[i], p[i + 3]); std::swap(p[i + 1], p[i + 2]); }
        }

        uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

        std::string StemOf(const std::string &fileName) {
            std::string n = fileName;
            const size_t sl = n.find_last_of('/');
            if(sl != std::string::npos) n = n.substr(sl + 1);
            const size_t dot = n.find_last_of('.');
            if(dot != std::string::npos && dot > 0) n = n.substr(0, dot);
            return n;
        }

        // Nintendo DS: Header (0x160) + ARM9 + ARM7 + Symbol/Titel (0xA00)
        template<typename ReadAt>
        std::string HashNds(uint64_t size, ReadAt readAt) {
            uint8_t h[0x160];
            if(size < 0x200 || !readAt(0, h, sizeof(h))) return "";
            const uint32_t a9 = Le32(h + 0x20), s9 = Le32(h + 0x2C), a7 = Le32(h + 0x30), s7 = Le32(h + 0x3C), ic = Le32(h + 0x68);
            if(s9 > 16u << 20 || s7 > 16u << 20 || uint64_t(a9) + s9 > size || uint64_t(a7) + s7 > size) return "";
            Md5 m;
            m.Update(h, sizeof(h));
            std::vector<uint8_t> buf(64 * 1024);
            auto part = [&](uint64_t off, uint64_t len) {
                while(len > 0) {
                    const size_t n = static_cast<size_t>(std::min<uint64_t>(len, buf.size()));
                    if(!readAt(off, buf.data(), n)) return false;
                    m.Update(buf.data(), n);
                    off += n; len -= n;
                }
                return true;
            };
            if(!part(a9, s9) || !part(a7, s7)) return "";
            if(ic > 0 && uint64_t(ic) + 0xA00 <= size && !part(ic, 0xA00)) return "";
            return m.HexDigest();
        }
    }

    std::string RaHashFile(const std::string &path, const std::string &sys) {
        if(!RaHashSupported(sys)) return "";
        if(sys == "arcade") return Md5Hex(StemOf(path).data(), StemOf(path).size());   // Arcade: Dateiname zählt
        FILE *f = std::fopen(path.c_str(), "rb");
        if(!f) return "";
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if(sz <= 0 || sz > 512L * 1024 * 1024) { std::fclose(f); return ""; }
        const uint64_t size = static_cast<uint64_t>(sz);
        std::string out;
        if(sys == "nds") {
            out = HashNds(size, [f](uint64_t off, uint8_t *dst, size_t n) { return std::fseek(f, static_cast<long>(off), SEEK_SET) == 0 && std::fread(dst, 1, n, f) == n; });
            std::fclose(f);
            return out;
        }
        if(size > 128u * 1024 * 1024) { std::fclose(f); return ""; }
        std::vector<uint8_t> buf(256 * 1024);
        size_t got = std::fread(buf.data(), 1, buf.size(), f);
        const size_t skip = std::min(HeaderSkip(sys, buf.data(), got, size), got);
        const int order = sys == "n64" ? N64Order(buf.data(), got) : 0;
        Md5 m;
        bool first = true;
        while(got > 0) {
            uint8_t *p = buf.data();
            size_t n = got;
            if(first) { p += skip; n -= skip; first = false; }
            if(order) N64Fix(p, n, order);
            m.Update(p, n);
            got = std::fread(buf.data(), 1, buf.size(), f);
        }
        std::fclose(f);
        return m.HexDigest();
    }

    std::string RaHashBuffer(const uint8_t *data, size_t size, const std::string &sys, const std::string &fileName) {
        if(!RaHashSupported(sys)) return "";
        if(sys == "arcade") { const std::string s = StemOf(fileName); return Md5Hex(s.data(), s.size()); }
        if(!data || size == 0) return "";
        if(sys == "nds") return HashNds(size, [data, size](uint64_t off, uint8_t *dst, size_t n) { if(off + n > size) return false; std::memcpy(dst, data + off, n); return true; });
        const size_t skip = std::min(HeaderSkip(sys, data, size, size), size);
        const int order = sys == "n64" ? N64Order(data, size) : 0;
        if(!order) return Md5Hex(data + skip, size - skip);
        std::vector<uint8_t> tmp(data + skip, data + size);
        N64Fix(tmp.data(), tmp.size(), order);
        return Md5Hex(tmp.data(), tmp.size());
    }

    long long ParseRaGameIdResponse(const std::string &body) {
        const json j = json::parse(body, nullptr, false);
        if(!j.is_object()) return 0;
        const json *s = JGetCI(j, "Success");
        if(s && s->is_boolean() && !s->get<bool>()) return 0;
        const json *g = JGetCI(j, "GameID");
        if(!g) return 0;
        if(g->is_number()) return std::max(0LL, g->get<long long>());
        if(g->is_string()) return std::max(0LL, std::atoll(g->get<std::string>().c_str()));
        return 0;
    }

    std::string RomTitleFromFile(const std::string &fileName) {
        std::string n = StemOf(fileName);
        // Klammern weg: (USA), [!], (Rev 1) …
        std::string o;
        int depth = 0;
        for(char c: n) {
            if(c == '(' || c == '[') depth++;
            else if((c == ')' || c == ']') && depth > 0) depth--;
            else if(depth == 0) o += c == '_' ? ' ' : c;
        }
        while(!o.empty() && o.back() == ' ') o.pop_back();
        // „Legend of Zelda, The - …“ -> „The Legend of Zelda - …“
        for(const char *art: { ", The", ", A", ", An", ", Die", ", Der", ", Das", ", Le", ", La", ", Les" }) {
            const size_t p = o.find(art);
            if(p != std::string::npos) {
                const size_t end = p + std::strlen(art);
                if(end == o.size() || o[end] == ' ' || o[end] == ':') {
                    o = std::string(art + 2) + " " + o.substr(0, p) + o.substr(end);
                    break;
                }
            }
        }
        return o;
    }

}
