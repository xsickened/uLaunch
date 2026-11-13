#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <pthread.h>

namespace nexus {

    // Ein gemeinsamer Netzwerk-Thread für SteamGridDB und RetroAchievements (HTTPS über curl).
    // Anfragen laufen nacheinander, mit Zeitlimit, Größenlimit und einem Wiederholversuch bei HTTP 429.
    // Zugangsdaten stehen nur in der URL bzw. im Header, nie im Log.
    struct NetRequest {
        int id = 0;
        std::string url;
        std::vector<std::string> headers;
        std::string toFile;              // leer = Antwort als Text
        size_t maxBytes = 4 * 1024 * 1024;
        std::string logName;             // für das Log (ohne Schlüssel)
    };

    struct NetResult {
        int id = 0;
        long status = 0;
        bool ok = false;                 // HTTP 2xx und vollständig
        std::string body;
        std::string error;
    };

    class Net {
        public:
            ~Net() { this->Stop(); }   // Thread immer erst anhalten, dann abbauen
            bool Start();
            void Stop();
            int Get(const std::string &url, const std::vector<std::string> &headers, const std::string &logName, const std::string &toFile = "", size_t maxBytes = 4 * 1024 * 1024);
            std::vector<NetResult> Poll();
            int Pending();
            void CancelAll();
            static bool Online();

        private:
            std::mutex mtx;
            std::condition_variable cv;
            std::deque<NetRequest> queue;
            std::vector<NetResult> done;
            pthread_t thread{};
            std::atomic<bool> running{ false };
            bool started = false, socketsUp = false;
            int nextId = 1, busy = 0;

            static void *Entry(void *self);
            void Work();
            NetResult Perform(const NetRequest &rq);
    };

}
