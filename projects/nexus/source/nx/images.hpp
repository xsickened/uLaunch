#pragma once
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
            std::thread worker;
            bool running = false;
            size_t budget = 64 * 1024 * 1024, used = 0;
            int maxEdge = 1280;
            uint64_t tick = 0;

            void Work();
            void Evict();
    };

}
