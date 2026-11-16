#pragma once
// NEXUS HOME: ROM-Ordner durchsuchen und Prüfwerte (RetroAchievements) im Hintergrund berechnen
#include "thread.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace nexus {

    class RomScanner {
        public:
            struct Rom { std::string path, system, norm; uint64_t size = 0; long long mtime = 0; };
            ~RomScanner() { this->Stop(); }
            void Start(const std::vector<std::string> &dirs);   // einmalig: durchsuchen, danach Prüfwert-Aufträge
            bool Started() const { return this->started; }
            bool Ready();                                       // Durchsuchen fertig
            const std::vector<Rom> &Roms() const { return this->list; }   // erst nach Ready() benutzen
            static std::string Key(const Rom &r);               // Pfad|Größe|Zeit (für den Cache)
            void Hash(const Rom &r);                            // Prüfwert berechnen lassen
            bool TakeResult(std::string &key, std::string &md5);
            void Stop();
        private:
            Thread t;
            std::mutex mtx;
            std::condition_variable cv;
            std::deque<Rom> jobs;
            std::vector<std::pair<std::string, std::string>> done;
            std::atomic<bool> stop{ false }, scanned{ false };
            bool started = false;
            std::vector<std::string> dirs;
            std::vector<Rom> list;
            void Work();
    };

}
