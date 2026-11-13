#include "themepack.hpp"
#include "store.hpp"
#include <cctype>
#include <cstdlib>
#include <zip.h>

namespace nexus {

    namespace {

        const char *const ImagePaths[] = { "home.image", "lock.image", "profile.image", "aura.image", "bgfx.image" };

        std::string Ext(const std::string &p) {
            const auto d = p.find_last_of('.');
            if(d == std::string::npos || p.size() - d > 6) return ".jpg";
            std::string e = p.substr(d);
            for(auto &c: e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".webp" || e == ".gif") ? e : ".jpg";
        }

        std::string Slug(const std::string &name) {
            std::string o;
            for(const unsigned char c: name) {
                if(std::isalnum(c)) o += static_cast<char>(std::tolower(c));
                else if(!o.empty() && o.back() != '-') o += '-';
                if(o.size() >= 40) break;
            }
            while(!o.empty() && o.back() == '-') o.pop_back();
            return o.empty() ? "theme" : o;
        }

        bool ReadEntry(struct zip_t *z, const char *name, std::string &out, size_t max) {
            if(zip_entry_open(z, name) != 0) return false;
            void *buf = nullptr;
            size_t size = 0;
            const bool ok = zip_entry_size(z) <= max && zip_entry_read(z, &buf, &size) >= 0 && buf;
            if(ok) out.assign(static_cast<const char *>(buf), size);
            std::free(buf);
            zip_entry_close(z);
            return ok;
        }

    }

    bool ExportThemePack(const std::string &file, const std::string &name, const json &settings, std::string &err) {
        json s = ThemeSnapshot(settings);
        std::vector<std::pair<std::string, std::string>> files;   // Quelle -> Name im Paket
        for(const char *p: ImagePaths) {
            const json *v = JGet(s, p);
            if(!v || !v->is_string() || v->get<std::string>().empty()) continue;
            const std::string src = v->get<std::string>();
            if(!FileExists(src)) { JSet(s, p, json(nullptr)); continue; }
            std::string key = p;
            for(auto &c: key) if(c == '.') c = '-';
            const std::string inPack = "images/" + key + Ext(src);
            files.push_back({ src, inPack });
            JSet(s, p, json("pkg:" + inPack));
        }
        json j = json::object();
        j["nexusTheme"] = 2;
        j["name"] = SanitizeUtf8(name.substr(0, 60));
        j["settings"] = s;
        const std::string tmp = file + ".tmp";
        std::remove(tmp.c_str());
        struct zip_t *z = zip_open(tmp.c_str(), 6, 'w');
        if(!z) { err = "Datei kann nicht angelegt werden"; return false; }
        bool ok = true;
        const std::string meta = SafeDump(j, 1);
        ok = ok && zip_entry_open(z, "theme.json") == 0 && zip_entry_write(z, meta.data(), meta.size()) == 0;
        zip_entry_close(z);
        for(const auto &[src, inPack]: files) {
            if(!ok) break;
            ok = zip_entry_open(z, inPack.c_str()) == 0 && zip_entry_fwrite(z, src.c_str()) == 0;
            zip_entry_close(z);
        }
        zip_close(z);
        if(!ok) { std::remove(tmp.c_str()); err = "Schreiben fehlgeschlagen"; return false; }
        std::remove(file.c_str());
        if(std::rename(tmp.c_str(), file.c_str()) != 0) { err = "Umbenennen fehlgeschlagen"; return false; }
        return true;
    }

    bool ReadThemePackName(const std::string &file, std::string &name) {
        struct zip_t *z = zip_open(file.c_str(), 0, 'r');
        if(!z) return false;
        std::string raw;
        const bool ok = ReadEntry(z, "theme.json", raw, 2 * 1024 * 1024);
        zip_close(z);
        if(!ok) return false;
        const json j = json::parse(raw, nullptr, false);
        if(j.is_discarded() || !j.is_object() || !JGet(j, "settings")) return false;
        name = SanitizeUtf8(JStr(j, "name", "Theme").substr(0, 60));
        return true;
    }

    bool ApplyThemePack(const std::string &file, const std::string &artDir, const json &current, json &result, std::string &err) {
        struct zip_t *z = zip_open(file.c_str(), 0, 'r');
        if(!z) { err = "Paket nicht lesbar"; return false; }
        std::string raw;
        if(!ReadEntry(z, "theme.json", raw, 2 * 1024 * 1024)) { zip_close(z); err = "theme.json fehlt"; return false; }
        const json j = json::parse(raw, nullptr, false);
        const json *ts = j.is_object() ? JGet(j, "settings") : nullptr;
        if(!ts || !ts->is_object()) { zip_close(z); err = "theme.json ist ungültig"; return false; }
        json s = ThemeSnapshot(*ts);
        const std::string dir = artDir + "/themes/" + Slug(JStr(j, "name", "theme"));
        for(const char *p: ImagePaths) {
            const json *v = JGet(s, p);
            if(!v || !v->is_string()) continue;
            const std::string val = v->get<std::string>();
            if(val.rfind("pkg:images/", 0) != 0) { if(val.rfind("pkg:", 0) == 0) JSet(s, p, json(nullptr)); continue; }
            const std::string entry = val.substr(4);
            const std::string fname = entry.substr(7);
            // nur einfache Dateinamen (kein Verlassen des Zielordners)
            if(fname.empty() || fname.find('/') != std::string::npos || fname.find("..") != std::string::npos || fname.size() > 64) { JSet(s, p, json(nullptr)); continue; }
            EnsureDir(dir);
            const std::string out = dir + "/" + fname;
            bool ok = zip_entry_open(z, entry.c_str()) == 0 && zip_entry_size(z) <= 32ull * 1024 * 1024 && zip_entry_fread(z, out.c_str()) == 0;
            zip_entry_close(z);
            JSet(s, p, ok ? json(out) : json(nullptr));
        }
        zip_close(z);
        json merged = s;
        for(const char *k: { "ra", "sgdbKey", "sgdb", "library", "auto" }) {
            const json *v = JGet(current, k);
            if(v) merged[k] = *v;
        }
        if(const json *pu = JGet(current, "profile.perUser")) JSet(merged, "profile.perUser", *pu);
        result = Settings::Sanitize(merged, nullptr);
        return true;
    }

}
