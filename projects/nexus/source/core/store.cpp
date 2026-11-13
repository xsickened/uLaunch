#include "store.hpp"
#include <algorithm>
#include <dirent.h>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace nexus {

    std::string SanitizeUtf8(const std::string &in) {
        std::string out;
        out.reserve(in.size());
        const auto *s = reinterpret_cast<const unsigned char *>(in.data());
        const size_t n = in.size();
        size_t i = 0;
        while(i < n) {
            const unsigned char c = s[i];
            size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
            bool ok = len > 0 && i + len <= n;
            for(size_t k = 1; ok && k < len; k++) ok = (s[i + k] & 0xC0) == 0x80;
            if(ok && len == 2 && c < 0xC2) ok = false;                                  // überlange Kodierung
            if(ok && len == 3 && c == 0xE0 && s[i + 1] < 0xA0) ok = false;
            if(ok && len == 3 && c == 0xED && s[i + 1] >= 0xA0) ok = false;              // Surrogate
            if(ok && len == 4 && (c > 0xF4 || (c == 0xF0 && s[i + 1] < 0x90) || (c == 0xF4 && s[i + 1] >= 0x90))) ok = false;
            if(ok && len == 1 && c == 0) break;                                          // C-String-Ende
            if(ok) { out.append(in, i, len); i += len; }
            else { out += '?'; i++; }
        }
        return out;
    }

    std::string SafeDump(const json &j, int indent) {
        return j.dump(indent, ' ', false, json::error_handler_t::replace);
    }

    bool ReadFile(const std::string &path, std::string &out, size_t maxBytes) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if(!f) return false;
        out.clear();
        char buf[16 * 1024];
        size_t n;
        bool ok = true;
        while((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            out.append(buf, n);
            if(out.size() > maxBytes) {
                ok = false;
                break;
            }
        }
        std::fclose(f);
        return ok;
    }

    bool FileExists(const std::string &path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    void EnsureDir(const std::string &path) {
        for(size_t i = 0; i < path.size(); i++) {
            if(path[i] == '/' && i > 0 && path[i - 1] != ':') {
                mkdir(path.substr(0, i).c_str(), 0777);
            }
        }
        mkdir(path.c_str(), 0777);
    }

    bool WriteFileAtomic(const std::string &path, const std::string &content) {
        const auto tmp = path + ".tmp";
        const auto bak = path + ".bak";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if(!f) return false;
        const bool written = std::fwrite(content.data(), 1, content.size(), f) == content.size();
        const bool flushed = std::fflush(f) == 0;
        const bool closed = std::fclose(f) == 0;
        if(!written || !flushed || !closed) {
            std::remove(tmp.c_str());
            return false;
        }
        // Gegenprobe: tmp-Datei vollständig lesbar?
        std::string check;
        if(!ReadFile(tmp, check) || check.size() != content.size()) {
            std::remove(tmp.c_str());
            return false;
        }
        if(FileExists(path)) {
            std::remove(bak.c_str());
            if(std::rename(path.c_str(), bak.c_str()) != 0) {
                std::remove(path.c_str());
            }
        }
        return std::rename(tmp.c_str(), path.c_str()) == 0;
    }

    static bool TryLoad(const std::string &path, NexusData &out, std::vector<std::string> &report) {
        std::string raw;
        if(!ReadFile(path, raw)) return false;
        const json j = json::parse(raw, nullptr, false);
        if(j.is_discarded() || !j.is_object()) return false;
        const json *s = JGet(j, "settings");
        out.settings.data = Settings::Sanitize(s ? *s : json::object(), &report);
        out.library = Library::FromJson(j, &report);
        return true;
    }

    NexusData LoadData(const std::string &path, std::string &source, std::vector<std::string> &report) {
        NexusData d;
        for(const auto &cand: { path, path + ".tmp", path + ".bak" }) {
            report.clear();
            if(TryLoad(cand, d, report)) {
                source = cand;
                MergeSecrets(path, d.settings);
                return d;
            }
        }
        source = "defaults";
        report.clear();
        d.settings.data = Settings::Defaults();
        MergeSecrets(path, d.settings);
        return d;
    }

    bool SaveData(const std::string &path, const NexusData &d) {
        json j = d.library.ToJson();
        j["schema"] = 2;
        j["settings"] = d.settings.data;
        SaveSecrets(path, d.settings);
        return WriteFileAtomic(path, SafeDump(j, 1));
    }

    static std::string Sibling(const std::string &path, const std::string &name) {
        const auto k = path.find_last_of('/');
        return (k == std::string::npos ? std::string() : path.substr(0, k + 1)) + name;
    }

    static const char *const SecretPaths[] = { "sgdbKey", "ra.user", "ra.key" };

    void MergeSecrets(const std::string &dataPath, Settings &s) {
        std::string raw;
        if(!ReadFile(Sibling(dataPath, "keys.json"), raw, 64 * 1024)) return;
        const json j = json::parse(raw, nullptr, false);
        if(j.is_discarded() || !j.is_object()) return;
        for(const char *p: SecretPaths) {
            const json *v = JGet(j, p);
            if(!v || !v->is_string() || v->get<std::string>().empty()) continue;
            if(s.S(p).empty()) s.Set(p, json(SanitizeUtf8(v->get<std::string>().substr(0, 120))));
        }
    }

    void SaveSecrets(const std::string &dataPath, const Settings &s) {
        json j = json::object();
        bool any = false;
        for(const char *p: SecretPaths) {
            const auto v = s.S(p);
            if(!v.empty()) any = true;
            JSet(j, p, json(v));
        }
        if(!any) return;   // nie mit leeren Werten überschreiben
        const std::string file = Sibling(dataPath, "keys.json");
        const std::string content = SafeDump(j, 1);
        std::string old;
        if(ReadFile(file, old, 64 * 1024) && old == content) return;
        WriteFileAtomic(file, content);
    }

    static std::vector<std::string> ListFiles(const std::string &dir, const std::string &ext) {
        std::vector<std::string> out;
        DIR *d = opendir(dir.c_str());
        if(!d) return out;
        while(dirent *e = readdir(d)) {
            const std::string n = e->d_name;
            if(n.size() > ext.size() && n[0] != '.' && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) out.push_back(n);
            if(out.size() > 500) break;
        }
        closedir(d);
        std::sort(out.begin(), out.end());
        return out;
    }

    void BackupDaily(const std::string &dataPath, const std::string &date) {
        if(!FileExists(dataPath) || date.size() != 10) return;
        const std::string dir = Sibling(dataPath, "backup");
        const std::string file = dir + "/nexus-" + date + ".json";
        if(FileExists(file)) return;
        std::string raw;
        if(!ReadFile(dataPath, raw)) return;
        const json j = json::parse(raw, nullptr, false);
        if(j.is_discarded() || !j.is_object()) return;   // nur gültige Stände sichern
        EnsureDir(dir);
        WriteFileAtomic(file, raw);
        std::remove((file + ".bak").c_str());
        auto files = ListFiles(dir, ".json");
        while(files.size() > 10) {
            std::remove((dir + "/" + files.front()).c_str());
            files.erase(files.begin());
        }
    }

    json ThemeSnapshot(const json &settings) {
        json t = settings;
        if(t.is_object()) {
            for(const char *k: { "ra", "sgdbKey", "sgdb", "library" }) t.erase(k);
        }
        return t;
    }

    static std::string Slug(const std::string &name) {
        std::string o;
        for(const unsigned char c: name) {
            if(std::isalnum(c)) o += static_cast<char>(std::tolower(c));
            else if((c == ' ' || c == '-' || c == '_') && !o.empty() && o.back() != '-') o += '-';
            if(o.size() >= 40) break;
        }
        while(!o.empty() && o.back() == '-') o.pop_back();
        return o.empty() ? "theme" : o;
    }

    bool SaveUserTheme(const std::string &dir, const std::string &name, const json &settings, std::string *outPath) {
        if(name.empty()) return false;
        EnsureDir(dir);
        const std::string path = dir + "/" + Slug(name) + ".json";
        json j = json::object();
        j["nexusTheme"] = 1;
        j["name"] = SanitizeUtf8(name.substr(0, 60));
        j["settings"] = ThemeSnapshot(settings);
        if(!WriteFileAtomic(path, SafeDump(j, 1))) return false;
        std::remove((path + ".bak").c_str());
        if(outPath) *outPath = path;
        return true;
    }

    std::vector<UserTheme> ListUserThemes(const std::string &dir) {
        std::vector<UserTheme> out;
        for(const auto &f: ListFiles(dir, ".json")) {
            std::string raw;
            if(!ReadFile(dir + "/" + f, raw, 2 * 1024 * 1024)) continue;
            const json j = json::parse(raw, nullptr, false);
            if(j.is_discarded() || !j.is_object() || !JGet(j, "settings")) continue;
            out.push_back(UserTheme{ SanitizeUtf8(JStr(j, "name", f.substr(0, f.size() - 5)).substr(0, 60)), dir + "/" + f });
        }
        return out;
    }

    bool ApplyUserTheme(const std::string &path, const json &current, json &result) {
        std::string raw;
        if(!ReadFile(path, raw, 2 * 1024 * 1024)) return false;
        const json j = json::parse(raw, nullptr, false);
        if(j.is_discarded() || !j.is_object()) return false;
        const json *ts = JGet(j, "settings");
        if(!ts || !ts->is_object()) return false;
        json merged = ThemeSnapshot(*ts);
        for(const char *k: { "ra", "sgdbKey", "sgdb", "library" }) {
            const json *v = JGet(current, k);
            if(v) merged[k] = *v;
        }
        result = Settings::Sanitize(merged, nullptr);
        return true;
    }

    bool BootGuardStart(const std::string &path) {
        std::string raw;
        int n = 0;
        if(ReadFile(path, raw, 64)) n = std::atoi(raw.c_str());
        if(n < 0 || n > 100) n = 0;
        n++;
        WriteFileAtomic(path, std::to_string(n));
        return n >= 3;
    }

    void BootGuardOk(const std::string &path) {
        WriteFileAtomic(path, "0");
    }

    static std::string g_LogPath;

    void LogInit(const std::string &path) {
        g_LogPath = path;
        struct stat st;
        if(stat(path.c_str(), &st) == 0 && st.st_size > 256 * 1024) {
            const auto old = path + ".1";
            std::remove(old.c_str());
            std::rename(path.c_str(), old.c_str());
        }
    }

    void Log(const char *fmt, ...) {
        if(g_LogPath.empty()) return;
        FILE *f = std::fopen(g_LogPath.c_str(), "ab");
        if(!f) return;
        const std::time_t t = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        std::fprintf(f, "[%s] ", ts);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(f, fmt, args);
        va_end(args);
        std::fputc('\n', f);
        std::fclose(f);
    }

}
