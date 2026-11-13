#include "images.hpp"
#include "../core/store.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

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
            bool cacheIt = false;
            std::string base;
            if(isMem) {
                SDL_RWops *rw = SDL_RWFromConstMem(mem.data(), static_cast<int>(mem.size()));
                if(rw) s = IMG_Load_RW(rw, 1);
            }
            else if(key.rfind("mem:", 0) != 0) {
                base = this->DiskBase(key);
                if(!base.empty()) {
                    for(const char *ext: { ".jpg", ".png" }) {
                        struct stat st;
                        if(stat((base + ext).c_str(), &st) == 0 && st.st_size > 0) { s = IMG_Load((base + ext).c_str()); break; }
                    }
                }
                if(!s) {
                    s = IMG_Load(key.c_str());
                    if(s && static_cast<long long>(s->w) * s->h > 2500000) Log("Großes Bild %dx%d: %s", s->w, s->h, key.c_str());
                    if(!s) Log("Bild nicht ladbar: %s (%s)", key.c_str(), IMG_GetError());
                    // PNG/WebP und große Bilder lohnen den Cache (Dekodieren/Verkleinern ist langsam)
                    std::string low = key;
                    for(auto &ch: low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    const bool slowFmt = low.size() > 4 && (low.compare(low.size() - 4, 4, ".png") == 0 || low.compare(low.size() - 5, 5, ".webp") == 0);
                    cacheIt = s && !base.empty() && (slowFmt || s->w > this->maxEdge || s->h > this->maxEdge);
                }
            }
            if(s && s->format && s->format->format != SDL_PIXELFORMAT_RGBA32) {
                SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
                SDL_FreeSurface(s);
                s = c;
            }
            // Sehr große Bilder verkleinern (Speicherbudget). Hochwertig gefiltert (stb_image_resize2,
            // sRGB-korrekt), statt SDL_BlitScaled (nächster Nachbar, wirkt krisselig/unscharf).
            if(s && (s->w > this->maxEdge || s->h > this->maxEdge)) {
                const double k = static_cast<double>(this->maxEdge) / std::max(s->w, s->h);
                const int nw = std::max(1, static_cast<int>(std::lround(s->w * k))), nh = std::max(1, static_cast<int>(std::lround(s->h * k)));
                SDL_Surface *conv = s;   // bereits RGBA32
                SDL_Surface *small = SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_RGBA32);
                bool ok = false;
                if(conv && small) {
                    SDL_LockSurface(conv);
                    SDL_LockSurface(small);
                    ok = stbir_resize_uint8_srgb(static_cast<const unsigned char *>(conv->pixels), conv->w, conv->h, conv->pitch,
                                                 static_cast<unsigned char *>(small->pixels), nw, nh, small->pitch, STBIR_RGBA) != nullptr;
                    SDL_UnlockSurface(small);
                    SDL_UnlockSurface(conv);
                    if(!ok) {
                        SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
                        ok = SDL_BlitScaled(conv, nullptr, small, nullptr) == 0;
                    }
                }
                if(ok) {
                    SDL_FreeSurface(s);
                    s = small;
                    small = nullptr;
                }
                if(small) SDL_FreeSurface(small);
            }
            uint32_t dom = 0xFFFFFFFFu;
            if(s) {
                // prägende Farbe bestimmen (Farbton-Histogramm, gewichtet nach Sättigung und Helligkeit)
                SDL_Surface *c32 = s;   // RGBA32, keine Kopie
                if(c32) {
                    double w[12] = {}, sr[12] = {}, sg[12] = {}, sb[12] = {};
                    SDL_LockSurface(c32);
                    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(c32->w) * c32->h / 4000.0)));
                    for(int y = 0; y < c32->h; y += step) {
                        const uint8_t *row = static_cast<const uint8_t *>(c32->pixels) + y * c32->pitch;
                        for(int x = 0; x < c32->w; x += step) {
                            const uint8_t *p = row + x * 4;
                            if(p[3] < 128) continue;
                            const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
                            const double mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
                            if(mx < 0.15 || d / std::max(mx, 1e-6) < 0.2) continue;
                            double hh = mx == r ? std::fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
                            hh = hh < 0 ? hh + 6 : hh;
                            const int bin = std::min(11, static_cast<int>(hh * 2));
                            const double wt = (d / mx) * mx;
                            w[bin] += wt; sr[bin] += p[0] * wt; sg[bin] += p[1] * wt; sb[bin] += p[2] * wt;
                        }
                    }
                    SDL_UnlockSurface(c32);
                    int best = 0;
                    double total = 0;
                    for(int i = 0; i < 12; i++) { total += w[i]; if(w[i] > w[best]) best = i; }
                    if(total > 5 && w[best] > 0) {
                        double r = sr[best] / w[best] / 255, g = sg[best] / w[best] / 255, b = sb[best] / w[best] / 255;
                        // kräftiger und heller machen, damit es als Akzent taugt
                        const double mx = std::max({ r, g, b, 1e-6 }), mn = std::min({ r, g, b });
                        const double sat = (mx - mn) / mx, ns = std::max(sat, 0.55), nv = std::max(mx, 0.85);
                        auto fix = [&](double c) { const double rel = (mx - c) / std::max(mx - mn, 1e-6) * ns; return std::clamp(nv * (1 - rel), 0.0, 1.0); };
                        if(mx - mn > 1e-6) { r = fix(r); g = fix(g); b = fix(b); }
                        dom = (static_cast<uint32_t>(r * 255) << 16) | (static_cast<uint32_t>(g * 255) << 8) | static_cast<uint32_t>(b * 255);
                    }
                }
            }
            if(s && cacheIt) {
                // verkleinertes Bild speichern (tmp -> umbenennen, damit nie halbe Dateien geladen werden)
                SDL_Surface *rgba = s;
                if(rgba) {
                    bool alpha = false;
                    SDL_LockSurface(rgba);
                    const uint8_t *px = static_cast<const uint8_t *>(rgba->pixels);
                    for(int y = 0; y < rgba->h && !alpha; y++) {
                        const uint8_t *row = px + y * rgba->pitch;
                        for(int x = 0; x < rgba->w; x++) if(row[x * 4 + 3] < 250) { alpha = true; break; }
                    }
                    std::vector<uint8_t> packed;
                    if(rgba->pitch != rgba->w * 4) {
                        packed.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
                        for(int y = 0; y < rgba->h; y++) std::copy(px + y * rgba->pitch, px + y * rgba->pitch + rgba->w * 4, packed.begin() + static_cast<size_t>(y) * rgba->w * 4);
                    }
                    const uint8_t *data = packed.empty() ? px : packed.data();
                    const std::string out = base + (alpha ? ".png" : ".jpg"), tmp = out + ".tmp";
                    mkdir(this->diskDir.c_str(), 0777);
                    const int ok = alpha ? stbi_write_png(tmp.c_str(), rgba->w, rgba->h, 4, data, rgba->w * 4)
                                         : stbi_write_jpg(tmp.c_str(), rgba->w, rgba->h, 4, data, 90);
                    SDL_UnlockSurface(rgba);
                    if(ok) std::rename(tmp.c_str(), out.c_str());
                    else std::remove(tmp.c_str());
                }
            }
            std::lock_guard<std::mutex> lk(this->mtx);
            if(s) this->colors[key] = dom;
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

    std::string ImageCache::DiskBase(const std::string &src) const {
        if(this->diskDir.empty() || src.rfind("sdmc:/", 0) != 0 || src.rfind(this->diskDir, 0) == 0) return "";
        struct stat st;
        if(stat(src.c_str(), &st) != 0) return "";
        // Name aus Pfad, Änderungszeit, Größe und Zielgröße: ändert sich die Quelle, passt der Cache nicht mehr
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const std::string &v) { for(const unsigned char c: v) { h ^= c; h *= 1099511628211ull; } h ^= 0xff; h *= 1099511628211ull; };
        mix(src);
        mix(std::to_string(static_cast<long long>(st.st_mtime)));
        mix(std::to_string(static_cast<long long>(st.st_size)));
        mix(std::to_string(this->maxEdge));
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
        return this->diskDir + "/" + buf;
    }

    void ImageCache::DropDisk(const std::string &src) {
        const auto base = this->DiskBase(src);
        if(base.empty()) return;
        std::remove((base + ".jpg").c_str());
        std::remove((base + ".png").c_str());
    }

    void ImageCache::CollectGarbage(std::vector<std::string> inUse) {
        if(this->diskDir.empty()) return;
        const std::string dir = this->diskDir;
        const int edge = this->maxEdge;
        Log("Bild-Cache: Aufräumen startet (%d Bilder in Benutzung)", static_cast<int>(inUse.size()));
        std::thread([dir, edge, inUse]() {
            ImageCache probe;
            probe.diskDir = dir;
            probe.maxEdge = edge;
            std::set<std::string> keep;
            for(const auto &p: inUse) {
                const auto b = probe.DiskBase(p);
                if(!b.empty()) keep.insert(b.substr(b.find_last_of('/') + 1));
            }
            DIR *d = opendir(dir.c_str());
            if(!d) return;
            std::vector<std::string> drop;
            while(dirent *e = readdir(d)) {
                const std::string n = e->d_name;
                if(n.size() < 16 || n[0] == '.') continue;
                if(!keep.count(n.substr(0, 16))) drop.push_back(dir + "/" + n);
            }
            closedir(d);
            for(const auto &f: drop) std::remove(f.c_str());
            Log("Bild-Cache: Aufräumen fertig, %d Dateien entfernt", static_cast<int>(drop.size()));
        }).detach();
    }

    bool ImageCache::ColorOf(const std::string &key, uint8_t &r, uint8_t &g, uint8_t &b) {
        std::lock_guard<std::mutex> lk(this->mtx);
        auto it = this->colors.find(key);
        if(it == this->colors.end() || it->second == 0xFFFFFFFFu) return false;
        r = (it->second >> 16) & 0xFF; g = (it->second >> 8) & 0xFF; b = it->second & 0xFF;
        return true;
    }

}
