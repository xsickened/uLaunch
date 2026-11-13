#pragma once
#include "thread.hpp"
#include "../core/nro.hpp"
#include "images.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace nexus {

    struct HbInfo {
        std::string id, path, name, author, version, iconKey;
    };

    // Durchsucht sdmc:/switch nach .nro-Dateien (im Hintergrund) und stellt die Icons bereit.
    class Homebrew {
        public:
            void Start(ImageCache *images);
            void Stop();
            std::vector<HbInfo> Snapshot(int &version);
            int Version() const { return this->version.load(); }
            bool Busy() const { return this->busy.load(); }

        private:
            std::vector<HbInfo> list;
            std::mutex mtx;
            Thread worker;
            std::atomic<int> version{ 0 };
            std::atomic<bool> busy{ false }, stop{ false };
            ImageCache *images = nullptr;
    };

}
