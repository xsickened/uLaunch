#include "homebrew.hpp"
#include "../core/store.hpp"
#include <algorithm>
#include <cctype>

namespace nexus {

    void Homebrew::Start(ImageCache *img) {
        if(this->worker.Joinable()) return;
        this->images = img;
        this->busy = true;
        this->worker.Start([this]() {
            std::vector<HbInfo> out;
            for(const auto &path: FindNros("sdmc:/switch", 300)) {
                if(this->stop) break;
                NroMeta m;
                if(!ParseNro(path, m, true)) continue;
                HbInfo h;
                h.id = m.id; h.path = m.path; h.name = m.name; h.author = m.author; h.version = m.version;
                if(!m.icon.empty() && this->images) {
                    h.iconKey = "mem:hb-" + m.id;
                    this->images->ProvideMemory(h.iconKey, std::move(m.icon));
                }
                out.push_back(h);
            }
            std::sort(out.begin(), out.end(), [](const HbInfo &a, const HbInfo &b) {
                std::string x = a.name, y = b.name;
                for(auto &c: x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for(auto &c: y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return x < y;
            });
            const int count = static_cast<int>(out.size());
            {
                std::lock_guard<std::mutex> lk(this->mtx);
                this->list = std::move(out);
            }
            Log("Homebrew: %d Einträge", count);
            this->version++;
            this->busy = false;
        }, 512 * 1024);
    }

    void Homebrew::Stop() {
        this->stop = true;
        this->worker.Join();
    }

    std::vector<HbInfo> Homebrew::Snapshot(int &v) {
        std::lock_guard<std::mutex> lk(this->mtx);
        v = this->version.load();
        return this->list;
    }

}
