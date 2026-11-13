#include "net.hpp"
#include "../core/store.hpp"
#include <switch.h>
#include <curl/curl.h>
#include <cstdio>
#include <unistd.h>

namespace nexus {

    namespace {

        struct Sink {
            std::string *str = nullptr;
            FILE *file = nullptr;
            size_t total = 0, max = 0;
            bool overflow = false;
        };

        size_t WriteCb(char *ptr, size_t size, size_t n, void *ud) {
            auto *s = static_cast<Sink *>(ud);
            const size_t len = size * n;
            if(s->total + len > s->max) { s->overflow = true; return 0; }
            s->total += len;
            if(s->file) return std::fwrite(ptr, 1, len, s->file) == len ? len : 0;
            s->str->append(ptr, len);
            return len;
        }

    }

    bool Net::Online() {
        NifmInternetConnectionStatus st;
        u32 strength = 0;
        NifmInternetConnectionType type;
        return R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &st)) && st == NifmInternetConnectionStatus_Connected;
    }

    bool Net::Start() {
        if(this->started) return true;
        this->running = true;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 512 * 1024);
        const int rc = pthread_create(&this->thread, &attr, &Net::Entry, this);
        pthread_attr_destroy(&attr);
        if(rc != 0) {
            this->running = false;
            Log("Netz: Thread konnte nicht gestartet werden (%d)", rc);
            return false;
        }
        this->started = true;
        return true;
    }

    void Net::Stop() {
        if(!this->started) return;
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            this->running = false;
            this->queue.clear();
        }
        this->cv.notify_all();
        pthread_join(this->thread, nullptr);
        this->started = false;
    }

    void *Net::Entry(void *self) {
        static_cast<Net *>(self)->Work();
        return nullptr;
    }

    int Net::Get(const std::string &url, const std::vector<std::string> &headers, const std::string &logName, const std::string &toFile, size_t maxBytes) {
        if(!this->started && !this->Start()) return 0;
        std::lock_guard<std::mutex> lk(this->mtx);
        if(this->queue.size() > 200) return 0;
        NetRequest rq;
        rq.id = this->nextId++;
        rq.url = url;
        rq.headers = headers;
        rq.toFile = toFile;
        rq.maxBytes = maxBytes;
        rq.logName = logName;
        this->queue.push_back(rq);
        this->cv.notify_one();
        return rq.id;
    }

    std::vector<NetResult> Net::Poll() {
        std::lock_guard<std::mutex> lk(this->mtx);
        std::vector<NetResult> out;
        out.swap(this->done);
        return out;
    }

    int Net::Pending() {
        std::lock_guard<std::mutex> lk(this->mtx);
        return static_cast<int>(this->queue.size()) + this->busy;
    }

    void Net::CancelAll() {
        std::lock_guard<std::mutex> lk(this->mtx);
        for(const auto &rq: this->queue) {
            NetResult r;
            r.id = rq.id;
            r.error = "abgebrochen";
            this->done.push_back(r);
        }
        this->queue.clear();
    }

    void Net::Work() {
        while(true) {
            NetRequest rq;
            {
                std::unique_lock<std::mutex> lk(this->mtx);
                this->cv.wait(lk, [this]() { return !this->running || !this->queue.empty(); });
                if(!this->running) break;
                rq = this->queue.front();
                this->queue.pop_front();
                this->busy = 1;
            }
            if(!this->socketsUp) {
                // kleinere Puffer als der Standard: uMenu läuft als Applet mit wenig Speicher
                SocketInitConfig cfg = *socketGetDefaultInitConfig();
                cfg.tcp_tx_buf_max_size = 0x20000;
                cfg.tcp_rx_buf_max_size = 0x20000;
                cfg.sb_efficiency = 2;
                cfg.num_bsd_sessions = 2;
                Result rc = socketInitialize(&cfg);
                if(R_FAILED(rc)) rc = socketInitializeDefault();
                if(R_SUCCEEDED(rc)) {
                    curl_global_init(CURL_GLOBAL_DEFAULT);
                    this->socketsUp = true;
                    Log("Netz: bereit");
                }
                else Log("Netz: socketInitialize fehlgeschlagen 0x%X", rc);
            }
            NetResult res;
            if(!this->socketsUp) { res.id = rq.id; res.error = "Netzwerk nicht verfügbar"; }
            else if(!Online()) { res.id = rq.id; res.error = "Keine Internetverbindung"; }
            else {
                res = this->Perform(rq);
                if(res.status == 429) {
                    Log("Netz: HTTP 429 bei %s, warte 5 s", rq.logName.c_str());
                    svcSleepThread(5'000'000'000ull);
                    res = this->Perform(rq);
                }
            }
            Log("Netz: %s -> %ld %s", rq.logName.c_str(), res.status, res.ok ? "ok" : res.error.c_str());
            std::lock_guard<std::mutex> lk(this->mtx);
            this->busy = 0;
            this->done.push_back(std::move(res));
        }
        if(this->socketsUp) {
            curl_global_cleanup();
            socketExit();
            this->socketsUp = false;
        }
    }

    NetResult Net::Perform(const NetRequest &rq) {
        NetResult res;
        res.id = rq.id;
        CURL *c = curl_easy_init();
        if(!c) { res.error = "curl nicht verfügbar"; return res; }
        Sink sink;
        sink.max = rq.maxBytes;
        std::string tmp;
        if(!rq.toFile.empty()) {
            tmp = rq.toFile + ".part";
            sink.file = std::fopen(tmp.c_str(), "wb");
            if(!sink.file) { curl_easy_cleanup(c); res.error = "Datei kann nicht geschrieben werden"; return res; }
        }
        else sink.str = &res.body;
        curl_slist *hdr = nullptr;
        for(const auto &h: rq.headers) hdr = curl_slist_append(hdr, h.c_str());
        curl_easy_setopt(c, CURLOPT_URL, rq.url.c_str());
        if(hdr) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "NEXUS-HOME/0.2 (Nintendo Switch; uLaunch)");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
        // Auf der Switch gibt es keinen CA-Speicher für curl (wie uManager)
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
        const CURLcode cc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.status);
        curl_easy_cleanup(c);
        if(hdr) curl_slist_free_all(hdr);
        if(sink.file) std::fclose(sink.file);
        if(cc != CURLE_OK) res.error = sink.overflow ? "Antwort zu groß" : curl_easy_strerror(cc);
        else if(res.status < 200 || res.status >= 300) res.error = "HTTP " + std::to_string(res.status);
        else res.ok = true;
        if(!rq.toFile.empty()) {
            if(res.ok && sink.total > 0) {
                std::remove(rq.toFile.c_str());
                if(std::rename(tmp.c_str(), rq.toFile.c_str()) != 0) { res.ok = false; res.error = "Umbenennen fehlgeschlagen"; }
            }
            else {
                if(res.ok) { res.ok = false; res.error = "leere Datei"; }
                std::remove(tmp.c_str());
            }
        }
        return res;
    }

}
