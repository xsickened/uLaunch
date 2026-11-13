#include "store.hpp"
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace nexus {

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
                return d;
            }
        }
        source = "defaults";
        report.clear();
        d.settings.data = Settings::Defaults();
        return d;
    }

    bool SaveData(const std::string &path, const NexusData &d) {
        json j = d.library.ToJson();
        j["schema"] = 2;
        j["settings"] = d.settings.data;
        return WriteFileAtomic(path, j.dump(1));
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
