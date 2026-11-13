#pragma once
#include "thread.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexus {

    // Animierter Hintergrund aus einer GIF-Datei. Dekodiert im Hintergrund, verkleinert die Bilder
    // und begrenzt den Speicher (überzählige Bilder werden übersprungen).
    class AnimBg {
        public:
            ~AnimBg() { this->Clear(); }
            void Load(const std::string &path, size_t budgetBytes);
            void Clear();
            // Im Hauptthread: aktuelles Bild (lädt fertige Bilder schrittweise als Texturen hoch)
            SDL_Texture *Frame(SDL_Renderer *r, double time);
            const std::string &Path() const { return this->path; }
            bool Failed() const { return this->failed.load(); }

        private:
            std::string path;
            Thread worker;
            std::mutex mtx;
            std::vector<SDL_Surface *> surfaces;
            std::vector<SDL_Texture *> textures;
            std::vector<int> delays;        // vom Lade-Thread befüllt (nur unter mtx)
            std::vector<int> shown;         // Verzögerungen der hochgeladenen Bilder (nur Hauptthread)
            std::atomic<bool> done{ false }, failed{ false }, cancel{ false };
            int total = 0;
    };

}
