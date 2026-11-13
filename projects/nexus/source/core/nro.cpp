#include "nro.hpp"
#include "store.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace nexus {

    namespace {

        bool ReadAt(FILE *f, uint64_t off, void *buf, size_t len) {
            return std::fseek(f, static_cast<long>(off), SEEK_SET) == 0 && std::fread(buf, 1, len, f) == len;
        }

        uint32_t U32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
        uint64_t U64(const uint8_t *p) { return U32(p) | (static_cast<uint64_t>(U32(p + 4)) << 32); }

        std::string CStr(const uint8_t *p, size_t max) {
            size_t n = 0;
            while(n < max && p[n]) n++;
            return SanitizeUtf8(std::string(reinterpret_cast<const char *>(p), n));
        }

        bool EndsNro(const std::string &n) {
            if(n.size() < 5) return false;
            std::string e = n.substr(n.size() - 4);
            for(auto &c: e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return e == ".nro";
        }

    }

    std::string HomebrewId(const std::string &path) {
        uint64_t h = 1469598103934665603ull;
        for(const unsigned char c: path) { h ^= c; h *= 1099511628211ull; }
        char buf[24];
        std::snprintf(buf, sizeof(buf), "ff%014llx", static_cast<unsigned long long>(h & 0x00FFFFFFFFFFFFFFull));
        return buf;
    }

    bool ParseNro(const std::string &path, NroMeta &out, bool wantIcon) {
        out = NroMeta{};
        out.path = path;
        out.id = HomebrewId(path);
        FILE *f = std::fopen(path.c_str(), "rb");
        if(!f) return false;
        uint8_t hdr[0x80];
        bool ok = ReadAt(f, 0, hdr, sizeof(hdr)) && std::memcmp(hdr + 0x10, "NRO0", 4) == 0;
        if(ok) {
            const uint32_t size = U32(hdr + 0x18);
            uint8_t aset[0x38];
            if(size > 0 && ReadAt(f, size, aset, sizeof(aset)) && std::memcmp(aset, "ASET", 4) == 0) {
                const uint64_t iconOff = U64(aset + 0x08), iconSize = U64(aset + 0x10);
                const uint64_t nacpOff = U64(aset + 0x18), nacpSize = U64(aset + 0x20);
                if(nacpSize >= 0x4000) {
                    std::vector<uint8_t> nacp(0x3070);
                    if(ReadAt(f, size + nacpOff, nacp.data(), nacp.size())) {
                        // Sprachen: zuerst Deutsch (4), dann Englisch (0), dann die erste vorhandene
                        for(int lang: { 4, 0, 1, 3, 5, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }) {
                            const auto n = CStr(nacp.data() + lang * 0x300, 0x200);
                            if(!n.empty()) { out.name = n; out.author = CStr(nacp.data() + lang * 0x300 + 0x200, 0x100); break; }
                        }
                        out.version = CStr(nacp.data() + 0x3060, 0x10);
                    }
                }
                if(wantIcon && iconSize > 0 && iconSize < 512 * 1024) {
                    out.icon.resize(iconSize);
                    if(!ReadAt(f, size + iconOff, out.icon.data(), out.icon.size())) out.icon.clear();
                }
            }
        }
        std::fclose(f);
        if(!ok) return false;
        if(out.name.empty()) {
            const auto s = path.find_last_of('/');
            out.name = path.substr(s == std::string::npos ? 0 : s + 1);
            if(out.name.size() > 4) out.name.resize(out.name.size() - 4);
        }
        return true;
    }

    std::vector<std::string> FindNros(const std::string &root, size_t max) {
        std::vector<std::string> out;
        auto scan = [&](const std::string &dir, int depth, auto &self) -> void {
            DIR *d = opendir(dir.c_str());
            if(!d) return;
            std::vector<std::string> subdirs;
            while(dirent *e = readdir(d)) {
                if(out.size() >= max) break;
                const std::string n = e->d_name;
                if(n.empty() || n[0] == '.') continue;
                const std::string full = dir + "/" + n;
                struct stat st;
                if(stat(full.c_str(), &st) != 0) continue;
                if(S_ISDIR(st.st_mode)) { if(depth > 0) subdirs.push_back(full); }
                else if(EndsNro(n)) out.push_back(full);
            }
            closedir(d);
            for(const auto &sd: subdirs) { if(out.size() >= max) break; self(sd, depth - 1, self); }
        };
        scan(root, 1, scan);
        std::sort(out.begin(), out.end());
        return out;
    }

}
