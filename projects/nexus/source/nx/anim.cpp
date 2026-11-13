#include "anim.hpp"
#include "../core/store.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>

namespace nexus {

    void AnimBg::Clear() {
        this->cancel = true;
        this->worker.Join();
        for(auto *s: this->surfaces) if(s) SDL_FreeSurface(s);
        for(auto *t: this->textures) if(t) SDL_DestroyTexture(t);
        this->surfaces.clear();
        this->textures.clear();
        this->delays.clear();
        this->shown.clear();
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
        this->worker.Start([this, p, budget]() {
            // IMG_LoadAnimation dekodiert alle Bilder in voller Größe auf einmal: vorher abschätzen,
            // sonst reicht ein langes GIF, um den Speicher von uMenu zu sprengen
            {
                std::string raw;
                if(!ReadFile(p, raw, 24u * 1024 * 1024) || raw.size() < 13 || raw.compare(0, 3, "GIF") != 0) {
                    this->failed = true;
                    Log("GIF nicht lesbar oder größer als 24 MB: %s", p.c_str());
                    return;
                }
                const size_t gw = static_cast<uint8_t>(raw[6]) | (static_cast<uint8_t>(raw[7]) << 8);
                const size_t gh = static_cast<uint8_t>(raw[8]) | (static_cast<uint8_t>(raw[9]) << 8);
                size_t frames = 0;
                for(size_t i = 0; i + 2 < raw.size(); i++) if(raw[i] == '\x21' && static_cast<uint8_t>(raw[i + 1]) == 0xF9 && raw[i + 2] == '\x04') frames++;
                frames = std::max<size_t>(frames, 1);
                const size_t need = gw * gh * 4 * frames;
                if(need > 80u * 1024 * 1024) {
                    this->failed = true;
                    Log("GIF zu groß zum Abspielen (%zux%zu, %zu Bilder, ~%zu MB): %s", gw, gh, frames, need >> 20, p.c_str());
                    return;
                }
            }
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
        }, 1024 * 1024);
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
                this->shown.push_back(this->textures.size() - 1 < this->delays.size() ? this->delays[this->textures.size() - 1] : 100);
                SDL_FreeSurface(s);
                this->surfaces[this->textures.size() - 1] = nullptr;
                up++;
            }
        }
        if(this->textures.empty()) return nullptr;
        // Bildwechsel nach den GIF-Verzögerungen (nur über die bereits hochgeladenen Bilder)
        const size_t n = this->textures.size();
        long long sum = 0;
        for(size_t i = 0; i < n; i++) sum += std::max(20, this->shown[i]);
        long long tms = static_cast<long long>(time * 1000) % std::max(1LL, sum);
        for(size_t i = 0; i < n; i++) {
            tms -= std::max(20, this->shown[i]);
            if(tms < 0) return this->textures[i];
        }
        return this->textures.back();
    }

}
