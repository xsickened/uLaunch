#pragma once
#include <functional>
#include <pthread.h>

namespace nexus {

    // Hintergrund-Thread mit fest gewählter Stackgröße. std::thread nimmt auf der Switch den kleinen
    // Standard-Stack von libnx; Bild-Dekoder und Dateisystemaufrufe laufen darin über (Absturz 2168-0002).
    class Thread {
        public:
            Thread() = default;
            Thread(const Thread &) = delete;
            Thread &operator=(const Thread &) = delete;
            ~Thread() { this->Join(); }

            bool Start(std::function<void()> fn, size_t stack = 1024 * 1024) {
                this->Join();
                this->fn = std::move(fn);
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setstacksize(&attr, stack);
                this->started = pthread_create(&this->t, &attr, &Thread::Entry, this) == 0;
                pthread_attr_destroy(&attr);
                return this->started;
            }
            bool Joinable() const { return this->started; }
            void Join() {
                if(!this->started) return;
                pthread_join(this->t, nullptr);
                this->started = false;
            }

        private:
            pthread_t t {};
            bool started = false;
            std::function<void()> fn;
            static void *Entry(void *p) { static_cast<Thread *>(p)->fn(); return nullptr; }
    };

}
