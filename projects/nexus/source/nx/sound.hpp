#pragma once
#include <SDL2/SDL_mixer.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nexus {

    // Menü-Sounds: eingebaute Klänge werden zur Laufzeit erzeugt (keine Dateien nötig), eigene Dateien
    // in sdmc:/nexus/sounds/<ereignis>.wav|.ogg|.mp3 ersetzen sie. Musik: bgm.* (Menü) und lock.* (Sperrbildschirm).
    // Ereignisse: move, select, back, open, launch, lock, unlock, error, toggle
    class Sound {
        public:
            bool Init(bool openAudio);
            void Shutdown();
            void Configure(bool enabled, const std::string &style, double volume, bool bgm, double bgmVolume);
            void Play(const std::string &ev);
            void SetMusic(const std::string &which);   // "menu", "lock" oder "" (aus)
            void Tick(double dt);

        private:
            bool ready = false, ownAudio = false, enabled = true, bgm = true;
            std::string style = "soft", wantMusic, playingMusic;
            double volume = 0.5, bgmVolume = 0.4, musicCheck = 0;
            int freq = 48000, channels = 2;
            uint16_t format = 0;
            struct Synth { std::vector<int16_t> pcm; Mix_Chunk *chunk = nullptr; };
            std::map<std::string, Synth> synth;
            std::map<std::string, Mix_Chunk *> files;   // nullptr = keine Datei vorhanden
            Mix_Music *music = nullptr;
            Mix_Chunk *Chunk(const std::string &ev);
            void Build(const std::string &key, const std::string &ev);
            void StartMusic();
    };

}
