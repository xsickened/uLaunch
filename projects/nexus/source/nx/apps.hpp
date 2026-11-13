#pragma once
#include "thread.hpp"
#include "images.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexus {

    struct AppInfo {
        std::string id;          // 16-stellige Titel-ID (Hex, Großbuchstaben)
        uint64_t tid = 0;
        std::string name;
        std::string iconKey;     // Schlüssel für ImageCache
        bool controlLoaded = false;
    };

    // ApplicationManager: liest installierte Titel (inkl. TICO-Forwarder, die normale Titel sind)
    // und lädt Namen/Icons im Hintergrund. Schreibt nur in sdmc:/nexus/cache.
    class Apps {
        public:
            bool Start(ImageCache *images);
            void Stop();
            std::vector<AppInfo> Snapshot(int &version);
            int Version() const { return this->version.load(); }
            bool Busy() const { return this->busy.load(); }
            std::string Error() const { return this->error; }

        private:
            std::vector<AppInfo> apps;
            std::mutex mtx;
            Thread worker;
            std::atomic<int> version{ 0 };
            std::atomic<bool> busy{ false }, stop{ false };
            ImageCache *images = nullptr;
            std::string error;
            bool nsOk = false;

            void Work();
    };

    std::string TidHex(uint64_t tid);

}
