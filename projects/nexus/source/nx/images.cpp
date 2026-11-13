#include "images.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>

namespace nexus {

    void ImageCache::Start(size_t budgetBytes, int edge) {
        this->budget = budgetBytes;
        this->maxEdge = edge;
        this->running = true;
        this->worker = std::thread([this]() { this->Work(); });
    }

    void ImageCache::Stop() {
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            this->running = false;
        }
        this->cv.notify_all();
        if(this->worker.joinable()) this->worker.join();
        for(auto &[k, s]: this->decoded) SDL_FreeSurface(s);
        this->decoded.clear();
        this->Clear();
    }

    void ImageCache::Clear() {
        for(auto &[k, t]: this->textures) SDL_DestroyTexture(t.tex);
        this->textures.clear();
        this->used = 0;
        std::lock_guard<std::mutex> lk(this->mtx);
        this->failed.clear();
    }

    void ImageCache::Invalidate(const std::string &key) {
        auto it = this->textures.find(key);
        if(it != this->textures.end()) {
            this->used -= std::min(this->used, it->second.bytes);
            SDL_DestroyTexture(it->second.tex);
            this->textures.erase(it);
        }
        std::lock_guard<std::mutex> lk(this->mtx);
        this->failed.erase(std::remove(this->failed.begin(), this->failed.end(), key), this->failed.end());
    }

    bool ImageCache::Failed(const std::string &key) {
        std::lock_guard<std::mutex> lk(this->mtx);
        return std::find(this->failed.begin(), this->failed.end(), key) != this->failed.end();
    }

    void ImageCache::ProvideMemory(const std::string &key, std::vector<uint8_t> &&data) {
        std::lock_guard<std::mutex> lk(this->mtx);
        this->memory[key] = std::move(data);
        this->failed.erase(std::remove(this->failed.begin(), this->failed.end(), key), this->failed.end());
    }

    bool ImageCache::HasMemory(const std::string &key) {
        std::lock_guard<std::mutex> lk(this->mtx);
        return this->memory.count(key) > 0;
    }

    SDL_Texture *ImageCache::Get(const std::string &key, int prio) {
        if(key.empty()) return nullptr;
        auto it = this->textures.find(key);
        if(it != this->textures.end()) {
            it->second.used = this->tick;
            return it->second.tex;
        }
        std::lock_guard<std::mutex> lk(this->mtx);
        if(std::find(this->failed.begin(), this->failed.end(), key) != this->failed.end()) return nullptr;
        auto p = this->pending.find(key);
        if(p == this->pending.end()) {
            this->pending[key] = prio;
            this->queue.push_back({ prio, key });
            this->cv.notify_one();
        }
        else if(prio < p->second) {
            p->second = prio;
            this->queue.push_back({ prio, key });
            this->cv.notify_one();
        }
        return nullptr;
    }

    void ImageCache::Work() {
        while(true) {
            std::string key;
            std::vector<uint8_t> mem;
            bool isMem = false;
            {
                std::unique_lock<std::mutex> lk(this->mtx);
                this->cv.wait(lk, [this]() { return !this->running || !this->queue.empty(); });
                if(!this->running) return;
                // höchste Priorität zuerst (kleinste Zahl), bei Gleichstand die neueste Anforderung
                auto best = this->queue.begin();
                for(auto it = this->queue.begin(); it != this->queue.end(); ++it) {
                    if(it->first <= best->first) best = it;
                }
                key = best->second;
                this->queue.erase(std::remove_if(this->queue.begin(), this->queue.end(), [&](const std::pair<int, std::string> &q) { return q.second == key; }), this->queue.end());
                if(!this->pending.count(key)) continue;
                if(key.rfind("mem:", 0) == 0) {
                    auto m = this->memory.find(key);
                    if(m != this->memory.end()) { mem = m->second; isMem = true; }
                }
            }
            SDL_Surface *s = nullptr;
            if(isMem) {
                SDL_RWops *rw = SDL_RWFromConstMem(mem.data(), static_cast<int>(mem.size()));
                if(rw) s = IMG_Load_RW(rw, 1);
            }
            else if(key.rfind("mem:", 0) != 0) {
                s = IMG_Load(key.c_str());
            }
            // Sehr große Bilder verkleinern (Speicherbudget)
            if(s && (s->w > this->maxEdge || s->h > this->maxEdge)) {
                const double k = static_cast<double>(this->maxEdge) / std::max(s->w, s->h);
                SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
                SDL_Surface *small = conv ? SDL_CreateRGBSurfaceWithFormat(0, std::max(1, static_cast<int>(s->w * k)), std::max(1, static_cast<int>(s->h * k)), 32, SDL_PIXELFORMAT_RGBA32) : nullptr;
                if(conv && small) {
                    SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
                    SDL_BlitScaled(conv, nullptr, small, nullptr);
                    SDL_FreeSurface(s);
                    s = small;
                    small = nullptr;
                }
                if(conv) SDL_FreeSurface(conv);
                if(small) SDL_FreeSurface(small);
            }
            std::lock_guard<std::mutex> lk(this->mtx);
            if(s) this->decoded.push_back({ key, s });
            else {
                this->failed.push_back(key);
                this->pending.erase(key);
            }
        }
    }

    void ImageCache::Pump(SDL_Renderer *r, int maxUploads) {
        this->tick++; // ein Tick pro Bild (Frame)
        std::vector<std::pair<std::string, SDL_Surface *>> ready;
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            const int n = std::min(maxUploads, static_cast<int>(this->decoded.size()));
            ready.assign(this->decoded.begin(), this->decoded.begin() + n);
            this->decoded.erase(this->decoded.begin(), this->decoded.begin() + n);
            for(const auto &[k, s]: ready) this->pending.erase(k);
        }
        for(auto &[k, s]: ready) {
            SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
            const size_t bytes = static_cast<size_t>(s->w) * s->h * 4;
            SDL_FreeSurface(s);
            if(!t) {
                std::lock_guard<std::mutex> lk(this->mtx);
                this->failed.push_back(k);
                continue;
            }
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
            auto old = this->textures.find(k);
            if(old != this->textures.end()) {
                this->used -= std::min(this->used, old->second.bytes);
                SDL_DestroyTexture(old->second.tex);
            }
            this->textures[k] = Tex{ t, bytes, this->tick };
            this->used += bytes;
        }
        this->Evict();
    }

    void ImageCache::Evict() {
        while(this->used > this->budget && this->textures.size() > 4) {
            auto oldest = this->textures.begin();
            for(auto it = this->textures.begin(); it != this->textures.end(); ++it) {
                if(it->second.used < oldest->second.used) oldest = it;
            }
            // nichts entfernen, was in diesem oder dem letzten Bild benutzt wurde
            if(this->tick - oldest->second.used < 2) break;
            this->used -= std::min(this->used, oldest->second.bytes);
            SDL_DestroyTexture(oldest->second.tex);
            this->textures.erase(oldest);
        }
    }

}
