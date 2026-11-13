#pragma once
#include "thread.hpp"
#include <SDL2/SDL.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nexus {

    // Asynchroner Bildlader mit Texturcache (LRU nach Bytes).
    // Schlüssel: Dateipfad ("sdmc:/...") oder "mem:<id>" für Bilddaten im Speicher (z. B. Spiel-Icons).
    class ImageCache {
        public:
            void Start(size_t budgetBytes, int maxEdge);
            void Stop();
            // Liefert die Textur oder nullptr (dann wird das Laden angestoßen). prio: 0 = sofort, höher = später
            SDL_Texture *Get(const std::string &key, int prio = 1);
            bool Failed(const std::string &key);
            void ProvideMemory(const std::string &key, std::vector<uint8_t> &&data);
            bool HasMemory(const std::string &key);
            // Im Hauptthread: fertig dekodierte Bilder als Textur hochladen
            void Pump(SDL_Renderer *r, int maxUploads);
            void Invalidate(const std::string &key);
            void Clear();
            size_t UsedBytes() const { return this->used; }
            size_t Budget() const { return this->budget; }
            size_t TextureCount() const { return this->textures.size(); }
            size_t PendingCount() { std::lock_guard<std::mutex> lk(this->mtx); return this->queue.size() + this->decoded.size(); }
            // Budget anpassen (z. B. bei knappem Speicher) und sofort freigeben, was darüber liegt
            void SetBudget(size_t b) { this->budget = b; this->Evict(); }

            // Dauerhafter Cache auf der SD-Karte: große Bilder und PNGs werden einmal verkleinert
            // gespeichert (JPG, mit Transparenz PNG) und danach direkt daraus geladen.
            void SetDiskCache(const std::string &dir);
            // Cache-Dateien eines Quellbilds (Grundname ohne Endung; leer, wenn die Quelle fehlt)
            std::string DiskBase(const std::string &src) const;
            void DropDisk(const std::string &src);
            // Entfernt im Hintergrund alle Cache-Dateien, deren Quelle nicht in inUse steht
            void CollectGarbage(std::vector<std::string> inUse);
            // Prägende Farbe eines geladenen Bildes (kräftigster häufiger Farbton), false = noch nicht/zu grau
            bool ColorOf(const std::string &key, uint8_t &r, uint8_t &g, uint8_t &b);

        private:
            struct Tex { SDL_Texture *tex; size_t bytes; uint64_t used; };
            std::unordered_map<std::string, Tex> textures;
            std::unordered_map<std::string, std::vector<uint8_t>> memory;
            std::map<std::string, int> pending;          // angefordert, noch nicht fertig
            std::deque<std::pair<int, std::string>> queue;
            std::vector<std::pair<std::string, SDL_Surface *>> decoded;
            std::vector<std::string> failed;
            std::mutex mtx;
            std::condition_variable cv;
            Thread worker;
            bool running = false;
            size_t budget = 64 * 1024 * 1024, used = 0;
            int maxEdge = 1280;
            std::string diskDir;
            mutable std::mutex dirMtx;   // diskDir wird im Hauptthread gesetzt und im Lade-Thread gelesen
            std::string DiskDir() const { std::lock_guard<std::mutex> lk(this->dirMtx); return this->diskDir; }
            std::unordered_map<std::string, uint32_t> colors;   // 0xRRGGBB, 0xFFFFFFFF = grau
            uint64_t tick = 0;
            std::vector<std::string> gcInUse;   // Aufräumauftrag für den Lade-Thread
            bool gcWanted = false;

            void RunGarbage(const std::vector<std::string> &inUse);

            void Work();
            void Evict();
    };

}
