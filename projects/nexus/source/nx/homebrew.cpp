#include "homebrew.hpp"
#include "../core/store.hpp"
#include <algorithm>
#include <cctype>

namespace nexus {

    void Homebrew::Start(ImageCache *img) {
        if(this->worker.joinable()) return;
        this->images = img;
        this->busy = true;
        this->worker = std::thread([this]() {
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
            {
                std::lock_guard<std::mutex> lk(this->mtx);
                this->list = std::move(out);
            }
            Log("Homebrew: %d Einträge", static_cast<int>(this->list.size()));
            this->version++;
            this->busy = false;
        });
    }

    void Homebrew::Stop() {
        this->stop = true;
        if(this->worker.joinable()) this->worker.join();
    }

    std::vector<HbInfo> Homebrew::Snapshot(int &v) {
        std::lock_guard<std::mutex> lk(this->mtx);
        v = this->version.load();
        return this->list;
    }

}
