// NEXUS HOME: ROM-Ordner durchsuchen und Prüfwerte berechnen (siehe romscan.hpp)
#include "romscan.hpp"
#include "../core/online_parse.hpp"
#include "../core/romhash.hpp"
#include "../core/store.hpp"
#include "zip.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdlib>
#include <functional>

namespace nexus {

    namespace {
        std::string Lower(std::string s) { for(auto &c: s) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return s; }
        std::string ExtOf(const std::string &n) { const size_t d = n.find_last_of('.'); return d == std::string::npos ? "" : Lower(n.substr(d + 1)); }
        bool Skip(const std::string &ext) {
            for(const char *e: { "txt", "png", "jpg", "jpeg", "webp", "xml", "json", "srm", "sav", "state", "cfg", "ini", "db", "nfo", "dat", "cht", "pdf", "md", "ips", "bps", "ups", "auto" })
                if(ext == e) return true;
            return ext.rfind("state", 0) == 0;
        }
    }

    std::string RomScanner::Key(const Rom &r) { return r.path + "|" + std::to_string(r.size) + "|" + std::to_string(r.mtime); }

    void RomScanner::Start(const std::vector<std::string> &d) {
        if(this->started) return;
        this->started = true;
        this->dirs = d;
        this->stop = false;
        this->t.Start([this]() { this->Work(); }, 512 * 1024);
    }

    bool RomScanner::Ready() { return this->scanned.load(); }

    void RomScanner::Hash(const Rom &r) {
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            if(this->jobs.size() > 2000) return;
            this->jobs.push_back(r);
        }
        this->cv.notify_one();
    }

    bool RomScanner::TakeResult(std::string &key, std::string &md5) {
        std::lock_guard<std::mutex> lk(this->mtx);
        if(this->done.empty()) return false;
        key = std::move(this->done.back().first);
        md5 = std::move(this->done.back().second);
        this->done.pop_back();
        return true;
    }

    void RomScanner::Stop() {
        this->stop = true;
        this->cv.notify_all();
        this->t.Join();
    }

    void RomScanner::Work() {
        // 1. Ordner durchsuchen: <Ordner>/<System>/<Datei> (eine Ebene Unterordner zusätzlich)
        std::vector<Rom> found;
        std::function<void(const std::string &, const std::string &, int)> walk = [&](const std::string &dir, const std::string &sysFolder, int depth) {
            DIR *d = opendir(dir.c_str());
            if(!d) return;
            while(dirent *e = readdir(d)) {
                if(this->stop || found.size() >= 30000) break;
                const std::string n = e->d_name;
                if(n.empty() || n[0] == '.') continue;
                const std::string p = dir + "/" + n;
                struct stat st;
                if(stat(p.c_str(), &st) != 0) continue;
                if(S_ISDIR(st.st_mode)) {
                    if(depth < 2) walk(p, depth == 0 ? n : sysFolder, depth + 1);
                    continue;
                }
                const std::string ext = ExtOf(n);
                if(Skip(ext)) continue;
                const std::string sys = RaSystemFor(sysFolder, ext);
                if(sys.empty()) continue;
                Rom r;
                r.path = p; r.system = sys; r.size = static_cast<uint64_t>(st.st_size); r.mtime = static_cast<long long>(st.st_mtime);
                r.norm = NormalizeTitle(RomTitleFromFile(n));
                if(r.norm.size() >= 2) found.push_back(std::move(r));
            }
            closedir(d);
        };
        for(const auto &dir: this->dirs) { if(this->stop) return; walk(dir, "", 0); }
        this->list = std::move(found);
        Log("ROM-Erkennung: %d ROM-Dateien gefunden", static_cast<int>(this->list.size()));
        this->scanned = true;
        // 2. Prüfwerte nach Bedarf
        while(true) {
            Rom r;
            {
                std::unique_lock<std::mutex> lk(this->mtx);
                this->cv.wait(lk, [this]() { return this->stop.load() || !this->jobs.empty(); });
                if(this->stop) return;
                r = std::move(this->jobs.front());
                this->jobs.pop_front();
            }
            std::string md5;
            const std::string ext = ExtOf(r.path);
            if(ext == "zip" && r.system != "arcade") {
                // gepackte ROM: erste passende Datei entpacken (bis 64 MB)
                if(zip_t *z = zip_open(r.path.c_str(), 0, 'r')) {
                    const ssize_t total = zip_entries_total(z);
                    for(ssize_t i = 0; i < total && md5.empty(); i++) {
                        if(zip_entry_openbyindex(z, static_cast<size_t>(i)) != 0) continue;
                        const std::string en = zip_entry_name(z) ? zip_entry_name(z) : "";
                        const unsigned long long sz = zip_entry_size(z);
                        if(!zip_entry_isdir(z) && sz > 0 && sz <= 64ull * 1024 * 1024 && !Skip(ExtOf(en))) {
                            void *buf = nullptr;
                            size_t bs = 0;
                            if(zip_entry_read(z, &buf, &bs) > 0 && buf) md5 = RaHashBuffer(static_cast<const uint8_t *>(buf), bs, r.system, en);
                            std::free(buf);
                        }
                        zip_entry_close(z);
                    }
                    zip_close(z);
                }
            }
            else md5 = RaHashFile(r.path, r.system);
            std::lock_guard<std::mutex> lk(this->mtx);
            this->done.emplace_back(Key(r), md5);
        }
    }

}
