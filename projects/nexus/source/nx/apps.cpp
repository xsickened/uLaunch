#include "apps.hpp"
#include "../core/store.hpp"
#include <switch.h>
#include "nsctl.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

namespace nexus {

    std::string TidHex(uint64_t tid) {
        char b[17];
        std::snprintf(b, sizeof(b), "%016lX", static_cast<unsigned long>(tid));
        return b;
    }

    namespace {
        const std::string NamesFile = std::string(CacheDir) + "/names.json";
        const std::string IconDir = std::string(CacheDir) + "/icons";

        std::string IconPath(const std::string &id) { return IconDir + "/" + id + ".jpg"; }
    }

    bool Apps::Start(ImageCache *img) {
        this->images = img;
        this->nsOk = R_SUCCEEDED(nsInitialize());
        if(!this->nsOk) {
            this->error = "NS-Dienst nicht verfügbar";
            return false;
        }
        EnsureDir(IconDir);
        // gespeicherte Namen sofort anzeigen
        json names = json::object();
        std::string raw;
        if(ReadFile(NamesFile, raw)) {
            names = json::parse(raw, nullptr, false);
            if(names.is_discarded() || !names.is_object()) names = json::object();
        }
        // installierte Titel auflisten
        std::vector<AppInfo> list;
        NsApplicationRecord recs[30];
        s32 offset = 0;
        for(int guard = 0; guard < 100; guard++) {
            s32 count = 0;
            if(R_FAILED(nsListApplicationRecord(recs, 30, offset, &count)) || count <= 0) break;
            for(s32 i = 0; i < count; i++) {
                AppInfo a;
                a.tid = recs[i].application_id;
                a.id = TidHex(a.tid);
                a.name = JStr(names, a.id.c_str(), "");
                if(FileExists(IconPath(a.id))) a.iconKey = IconPath(a.id);
                list.push_back(a);
            }
            offset += count;
        }
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            this->apps = list;
        }
        this->version++;
        this->busy = true;
        this->worker = std::thread([this]() { this->Work(); });
        return true;
    }

    void Apps::Stop() {
        this->stop = true;
        if(this->worker.joinable()) this->worker.join();
        if(this->nsOk) nsExit();
    }

    std::vector<AppInfo> Apps::Snapshot(int &v) {
        std::lock_guard<std::mutex> lk(this->mtx);
        v = this->version.load();
        return this->apps;
    }

    void Apps::Work() {
        std::vector<AppInfo> todo;
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            todo = this->apps;
        }
        auto data = std::make_unique<NsApplicationControlData>();
        json names = json::object();
        int changed = 0;
        for(const auto &a: todo) {
            if(this->stop) break;
            u64 actual = 0;
            std::memset(data.get(), 0, sizeof(NsApplicationControlData));
            const Result rc = GetControlData(a.tid, data.get(), &actual);
            if(R_FAILED(rc) || actual < sizeof(NacpStruct)) {
                Log("ControlData %s fehlgeschlagen: 0x%X", a.id.c_str(), rc);
                continue;
            }
            std::string name;
            NacpLanguageEntry *le = nullptr;
            if(R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &le)) && le && le->name[0]) name = le->name;
            for(int i = 0; i < 16 && name.empty(); i++) {
                if(data->nacp.lang[i].name[0]) name = data->nacp.lang[i].name;
            }
            if(name.empty()) name = a.id;
            names[a.id] = name;
            std::string iconKey = a.iconKey;
            const size_t iconSize = actual - sizeof(NacpStruct);
            if(iconKey.empty() && iconSize > 0 && iconSize <= sizeof(data->icon)) {
                const auto path = IconPath(a.id);
                if(WriteFileAtomic(path, std::string(reinterpret_cast<const char *>(data->icon), iconSize))) iconKey = path;
                else {
                    iconKey = "mem:" + a.id;
                    this->images->ProvideMemory(iconKey, std::vector<uint8_t>(data->icon, data->icon + iconSize));
                }
            }
            {
                std::lock_guard<std::mutex> lk(this->mtx);
                for(auto &x: this->apps) {
                    if(x.id == a.id) {
                        x.name = name;
                        x.iconKey = iconKey;
                        x.controlLoaded = true;
                    }
                }
            }
            if(++changed % 8 == 0) this->version++;
        }
        if(!names.empty()) WriteFileAtomic(NamesFile, names.dump());
        this->version++;
        this->busy = false;
    }

}
