#include "anim.hpp"
#include "../core/store.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>

namespace nexus {

    void AnimBg::Clear() {
        this->cancel = true;
        if(this->worker.joinable()) this->worker.join();
        for(auto *s: this->surfaces) if(s) SDL_FreeSurface(s);
        for(auto *t: this->textures) if(t) SDL_DestroyTexture(t);
        this->surfaces.clear();
        this->textures.clear();
        this->delays.clear();
        this->path.clear();
        this->done = false;
        this->failed = false;
        this->cancel = false;
        this->total = 0;
    }

    void AnimBg::Load(const std::string &p, size_t budget) {
        this->Clear();
        this->path = p;
#if defined(SDL_IMAGE_VERSION_ATLEAST)
#if SDL_IMAGE_VERSION_ATLEAST(2, 6, 0)
        this->worker = std::thread([this, p, budget]() {
            IMG_Animation *a = IMG_LoadAnimation(p.c_str());
            if(!a || a->count <= 0) { if(a) IMG_FreeAnimation(a); this->failed = true; Log("GIF nicht lesbar: %s", p.c_str()); return; }
            const double k = std::min(1.0, 640.0 / std::max(1, a->w));
            const int w = std::max(1, static_cast<int>(a->w * k)), h = std::max(1, static_cast<int>(a->h * k));
            const size_t per = static_cast<size_t>(w) * h * 4;
            const int maxFrames = std::max(1, static_cast<int>(budget / std::max<size_t>(per, 1)));
            const int skip = std::max(1, (a->count + maxFrames - 1) / maxFrames);
            int acc = 0;
            for(int i = 0; i < a->count && !this->cancel; i++) {
                acc += a->delays ? std::max(20, a->delays[i]) : 100;
                if(i % skip != 0 && i != a->count - 1) continue;
                SDL_Surface *conv = SDL_ConvertSurfaceFormat(a->frames[i], SDL_PIXELFORMAT_RGBA32, 0);
                SDL_Surface *small = conv ? SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32) : nullptr;
                if(conv && small) { SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE); SDL_BlitScaled(conv, nullptr, small, nullptr); }
                if(conv) SDL_FreeSurface(conv);
                if(!small) continue;
                std::lock_guard<std::mutex> lk(this->mtx);
                this->surfaces.push_back(small);
                this->delays.push_back(acc);
                acc = 0;
            }
            IMG_FreeAnimation(a);
            this->done = true;
        });
        return;
#endif
#endif
        this->failed = true;
        Log("GIF-Hintergrund: von dieser SDL_image-Version nicht unterstützt");
    }

    SDL_Texture *AnimBg::Frame(SDL_Renderer *r, double time) {
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            int up = 0;
            while(this->textures.size() < this->surfaces.size() && up < 4) {
                SDL_Surface *s = this->surfaces[this->textures.size()];
                SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
                if(t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                this->textures.push_back(t);
                SDL_FreeSurface(s);
                this->surfaces[this->textures.size() - 1] = nullptr;
                up++;
            }
        }
        if(this->textures.empty()) return nullptr;
        // Bildwechsel nach den GIF-Verzögerungen (nur über die bereits hochgeladenen Bilder)
        const size_t n = this->textures.size();
        long long sum = 0;
        for(size_t i = 0; i < n; i++) sum += std::max(20, this->delays[i]);
        long long tms = static_cast<long long>(time * 1000) % std::max(1LL, sum);
        for(size_t i = 0; i < n; i++) {
            tms -= std::max(20, this->delays[i]);
            if(tms < 0) return this->textures[i];
        }
        return this->textures.back();
    }

}
