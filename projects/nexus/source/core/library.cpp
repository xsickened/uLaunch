#include "library.hpp"
#include "store.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <random>
#include <set>

namespace nexus {

    static bool IsTid(const std::string &s) {
        if(s.size() != 16) return false;
        for(const char c: s) {
            if(!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }

    static std::string RandomId() {
        static std::mt19937 rng(std::random_device{}());
        static const char *hex = "0123456789abcdef";
        std::string s = "fld-";
        for(int i = 0; i < 8; i++) s += hex[rng() & 15];
        return s;
    }

    json SanitizeArt(const json &a) {
        json o = json::object();
        if(!a.is_object()) return o;
        for(const char *k: { "grid", "hero", "logo" }) {
            const json *v = JGet(a, k);
            if(!v || !v->is_string()) continue;
            const std::string p = v->get<std::string>();
            if(p.size() < 300 && p.rfind("sdmc:/", 0) == 0 && p.find("..") == std::string::npos) o[k] = SanitizeUtf8(p);
        }
        return o;
    }

    Library Library::FromJson(const json &j, std::vector<std::string> *report) {
        Library lib;
        const json *apps = JGet(j, "apps");
        if(apps && apps->is_object()) {
            for(auto it = apps->begin(); it != apps->end(); ++it) {
                if(!IsTid(it.key()) || !it.value().is_object()) {
                    if(report) report->push_back("apps:" + it.key());
                    continue;
                }
                json a = json::object();
                a["favorite"] = JBool(it.value(), "favorite", false);
                a["launched"] = JNum(it.value(), "launched", 0);
                const double ra = JNum(it.value(), "raGameId", 0);
                if(ra >= 1 && ra < 1e7 && ra == static_cast<double>(static_cast<long long>(ra))) a["raGameId"] = static_cast<long long>(ra);
                const json *t = JGet(it.value(), "tile");
                a["tile"] = t ? SanitizeTile(*t) : json::object();
                const json *stk = JGet(it.value(), "stickers");
                if(stk && stk->is_array()) {
                    json keep = json::array();
                    for(const auto &x: *stk) {
                        if(!x.is_string()) continue;
                        const auto v = x.get<std::string>();
                        bool known = false;
                        for(const char *k: StickerIds) if(v == k) known = true;
                        if(known && std::find(keep.begin(), keep.end(), json(v)) == keep.end()) keep.push_back(v);
                    }
                    if(!keep.empty()) a["stickers"] = keep;
                }
                const std::string cn = SanitizeUtf8(JStr(it.value(), "name", "").substr(0, 80));
                if(!cn.empty()) a["name"] = cn;
                const json *art = JGet(it.value(), "art");
                if(art) { json sa = SanitizeArt(*art); if(!sa.empty()) a["art"] = sa; }
                lib.apps[it.key()] = a;
            }
        }
        std::set<std::string> owned, fids;
        const json *fs = JGet(j, "folders");
        if(fs && fs->is_array()) {
            for(const auto &f: *fs) {
                Folder fo;
                fo.id = JStr(f, "id", "");
                fo.name = SanitizeUtf8(JStr(f, "name", "").substr(0, 60));
                if(!IsFolderId(fo.id) || fo.name.empty() || fids.count(fo.id)) {
                    if(report) report->push_back("folder:" + fo.id);
                    continue;
                }
                fids.insert(fo.id);
                const auto acc = JStr(f, "accent", "#3ee0c8");
                const auto *ce = Settings::Find("theme.accent");
                fo.accent = (ce && Settings::ValidValue(*ce, json(acc))) ? acc : "#3ee0c8";
                fo.sort = JStr(f, "sort", "manual") == "name" ? "name" : "manual";
                const json *t = JGet(f, "tile");
                fo.tile = t ? SanitizeTile(*t) : json::object();
                const json *fart = JGet(f, "art");
                fo.art = fart ? SanitizeArt(*fart) : json::object();
                const json *items = JGet(f, "items");
                if(items && items->is_array()) {
                    for(const auto &i: *items) {
                        if(i.is_string() && IsTid(i.get<std::string>()) && !owned.count(i.get<std::string>())) {
                            owned.insert(i.get<std::string>());
                            fo.items.push_back(i.get<std::string>());
                        }
                    }
                }
                lib.folders.push_back(fo);
            }
        }
        const json *cr = JGet(j, "crops");
        if(cr && cr->is_object()) {
            for(auto it = cr->begin(); it != cr->end() && lib.crops.size() < 500; ++it) {
                const std::string &k = it.key();
                if(k.size() >= 300 || k.rfind("sdmc:/", 0) != 0 || !it.value().is_object()) continue;
                Crop c;
                c.zoom = JNum(it.value(), "z", 1); c.x = JNum(it.value(), "x", 50); c.y = JNum(it.value(), "y", 50);
                if(!std::isfinite(c.zoom) || !std::isfinite(c.x) || !std::isfinite(c.y)) continue;
                lib.crops[k] = { { "z", std::clamp(c.zoom, 1.0, 4.0) }, { "x", std::clamp(c.x, 0.0, 100.0) }, { "y", std::clamp(c.y, 0.0, 100.0) } };
            }
        }
        const json *ro = JGet(j, "rootOrder");
        std::set<std::string> seen;
        if(ro && ro->is_array()) {
            for(const auto &i: *ro) {
                if(!i.is_string()) continue;
                const auto id = i.get<std::string>();
                const bool ok = IsFolderId(id) ? fids.count(id) > 0 : (IsTid(id) && !owned.count(id));
                if(ok && !seen.count(id)) {
                    seen.insert(id);
                    lib.rootOrder.push_back(id);
                }
            }
        }
        for(const auto &f: lib.folders) {
            if(!seen.count(f.id)) {
                lib.rootOrder.push_back(f.id);
                seen.insert(f.id);
            }
        }
        return lib;
    }

    json Library::ToJson() const {
        json j = json::object();
        j["apps"] = this->apps;
        json fs = json::array();
        for(const auto &f: this->folders) {
            fs.push_back({ { "id", f.id }, { "name", f.name }, { "accent", f.accent }, { "items", f.items }, { "tile", f.tile }, { "sort", f.sort }, { "art", f.art } });
        }
        j["folders"] = fs;
        j["rootOrder"] = this->rootOrder;
        j["crops"] = this->crops;
        return j;
    }

    void Library::Reconcile(const std::vector<std::string> &installed, std::vector<std::string> *report) {
        this->installedCache = installed;
        const std::set<std::string> inst(installed.begin(), installed.end());
        // Titel in Ordnern: nur einmal; nicht installierte bleiben gespeichert, werden aber nicht angezeigt
        std::set<std::string> owned;
        for(auto &f: this->folders) {
            std::vector<std::string> keep;
            for(const auto &i: f.items) {
                if(!owned.count(i)) {
                    owned.insert(i);
                    keep.push_back(i);
                }
            }
            f.items = keep;
        }
        std::vector<std::string> ro;
        std::set<std::string> seen;
        for(const auto &id: this->rootOrder) {
            if(seen.count(id)) continue;
            if(IsFolderId(id) ? (this->FindFolder(id) != nullptr) : !owned.count(id)) {
                ro.push_back(id);
                seen.insert(id);
            }
        }
        for(const auto &f: this->folders) {
            if(!seen.count(f.id)) {
                ro.push_back(f.id);
                seen.insert(f.id);
            }
        }
        for(const auto &t: installed) {
            if(!seen.count(t) && !owned.count(t)) {
                ro.push_back(t);
                seen.insert(t);
                if(report) report->push_back("neu:" + t);
            }
        }
        this->rootOrder = ro;
    }

    Folder *Library::FindFolder(const std::string &id) {
        for(auto &f: this->folders) {
            if(f.id == id) return &f;
        }
        return nullptr;
    }

    Folder *Library::FolderOf(const std::string &tid) {
        for(auto &f: this->folders) {
            if(std::find(f.items.begin(), f.items.end(), tid) != f.items.end()) return &f;
        }
        return nullptr;
    }

    bool Library::IsFav(const std::string &tid) const {
        const json *a = JGet(this->apps, tid);
        return a && JBool(*a, "favorite", false);
    }

    void Library::ToggleFav(const std::string &tid) {
        if(IsFolderId(tid)) return;
        auto &a = this->apps[tid];
        if(!a.is_object()) a = json::object();
        a["favorite"] = !JBool(a, "favorite", false);
    }

    json &Library::TileOf(const std::string &id) {
        if(IsFolderId(id)) {
            if(auto *f = this->FindFolder(id)) return f->tile;
        }
        auto &a = this->apps[id];
        if(!a.is_object()) a = json::object();
        if(!a.contains("tile") || !a["tile"].is_object()) a["tile"] = json::object();
        return a["tile"];
    }

    void Library::AddToFolder(const std::string &tid, const std::string &fid) {
        if(IsFolderId(tid)) return;
        auto *f = this->FindFolder(fid);
        if(!f) return;
        for(auto &o: this->folders) {
            o.items.erase(std::remove(o.items.begin(), o.items.end(), tid), o.items.end());
        }
        this->rootOrder.erase(std::remove(this->rootOrder.begin(), this->rootOrder.end(), tid), this->rootOrder.end());
        f->items.push_back(tid);
    }

    void Library::RemoveFromFolder(const std::string &tid) {
        auto *f = this->FolderOf(tid);
        if(!f) return;
        const auto fid = f->id;
        f->items.erase(std::remove(f->items.begin(), f->items.end(), tid), f->items.end());
        auto it = std::find(this->rootOrder.begin(), this->rootOrder.end(), fid);
        this->rootOrder.insert(it == this->rootOrder.end() ? it : it + 1, tid);
    }

    void Library::DeleteFolder(const std::string &fid) {
        auto it = std::find_if(this->folders.begin(), this->folders.end(), [&](const Folder &f) { return f.id == fid; });
        if(it == this->folders.end()) return;
        const auto items = it->items;
        this->folders.erase(it);
        auto pos = std::find(this->rootOrder.begin(), this->rootOrder.end(), fid);
        if(pos != this->rootOrder.end()) {
            pos = this->rootOrder.erase(pos);
            this->rootOrder.insert(pos, items.begin(), items.end());
        }
        else {
            this->rootOrder.insert(this->rootOrder.end(), items.begin(), items.end());
        }
    }

    std::string Library::CreateFolder(const std::string &name, int insertAt) {
        Folder f;
        f.id = RandomId();
        while(this->FindFolder(f.id)) f.id = RandomId();
        f.name = SanitizeUtf8(name.substr(0, 60));
        f.accent = Palette[this->folders.size() % 11];
        this->folders.push_back(f);
        if(insertAt < 0 || insertAt > static_cast<int>(this->rootOrder.size())) insertAt = static_cast<int>(this->rootOrder.size());
        this->rootOrder.insert(this->rootOrder.begin() + insertAt, f.id);
        return f.id;
    }

    void Library::MarkLaunched(const std::string &tid, double when) {
        auto &a = this->apps[tid];
        if(!a.is_object()) a = json::object();
        a["launched"] = when;
    }

    double Library::Launched(const std::string &tid) const {
        const json *a = JGet(this->apps, tid);
        return a ? JNum(*a, "launched", 0) : 0;
    }

    std::string Library::ArtOverride(const std::string &id, const std::string &kind) {
        const json *art = nullptr;
        if(IsFolderId(id)) { if(auto *f = this->FindFolder(id)) art = &f->art; }
        else { const json *a = JGet(this->apps, id); if(a) art = JGet(*a, "art"); }
        if(!art || !art->is_object()) return "";
        return JStr(*art, kind.c_str(), "");
    }

    void Library::SetArtOverride(const std::string &id, const std::string &kind, const std::string &path) {
        if(kind != "grid" && kind != "hero" && kind != "logo") return;
        json *art = nullptr;
        if(IsFolderId(id)) { if(auto *f = this->FindFolder(id)) art = &f->art; }
        else if(IsTid(id)) {
            auto &a = this->apps[id];
            if(!a.is_object()) a = json::object();
            if(!a.contains("art") || !a["art"].is_object()) a["art"] = json::object();
            art = &a["art"];
        }
        if(!art) return;
        if(!art->is_object()) *art = json::object();
        if(path.empty()) art->erase(kind);
        else (*art)[kind] = path;
        *art = SanitizeArt(*art);
    }

    long long Library::RaGameId(const std::string &tid) const {
        const json *a = JGet(this->apps, tid);
        return a ? static_cast<long long>(JNum(*a, "raGameId", 0)) : 0;
    }

    void Library::SetRaGameId(const std::string &tid, long long gid) {
        if(!IsTid(tid)) return;
        auto &a = this->apps[tid];
        if(!a.is_object()) a = json::object();
        if(gid <= 0) a.erase("raGameId");
        else a["raGameId"] = gid;
    }

    Crop Library::CropOf(const std::string &path) const {
        Crop c;
        if(path.empty()) return c;
        auto it = this->crops.find(path);
        if(it == this->crops.end() || !it->is_object()) return c;
        c.zoom = std::clamp(JNum(*it, "z", 1), 1.0, 4.0);
        c.x = std::clamp(JNum(*it, "x", 50), 0.0, 100.0);
        c.y = std::clamp(JNum(*it, "y", 50), 0.0, 100.0);
        c.set = true;
        return c;
    }

    void Library::SetCrop(const std::string &path, const Crop &c) {
        if(path.empty() || path.size() >= 300 || path.rfind("sdmc:/", 0) != 0) return;
        if(!c.set || (std::fabs(c.zoom - 1) < 1e-6 && std::fabs(c.x - 50) < 1e-6 && std::fabs(c.y - 50) < 1e-6)) { this->crops.erase(path); return; }
        this->crops[path] = { { "z", std::clamp(c.zoom, 1.0, 4.0) }, { "x", std::clamp(c.x, 0.0, 100.0) }, { "y", std::clamp(c.y, 0.0, 100.0) } };
    }

    std::vector<std::string> Library::AllArtOverrides() const {
        std::vector<std::string> out;
        auto add = [&out](const json &art) {
            if(!art.is_object()) return;
            for(auto it = art.begin(); it != art.end(); ++it) if(it.value().is_string()) out.push_back(it.value().get<std::string>());
        };
        for(auto it = this->apps.begin(); it != this->apps.end(); ++it) { const json *a = JGet(it.value(), "art"); if(a) add(*a); }
        for(const auto &f: this->folders) add(f.art);
        return out;
    }

    bool Library::ArtInUse(const std::string &path) const {
        for(const auto &p: this->AllArtOverrides()) if(p == path) return true;
        return false;
    }

    std::string Library::ReplaceArt(const std::string &id, const std::string &kind, const std::string &path) {
        const std::string old = this->ArtOverride(id, kind);
        this->SetArtOverride(id, kind, path);
        if(old.empty() || old == path) return "";
        const auto slash = old.find_last_of('/');
        const std::string file = slash == std::string::npos ? old : old.substr(slash + 1);
        if(file.rfind("sgdb-", 0) != 0 || old.rfind("sdmc:/nexus/art/", 0) != 0) return "";
        if(this->ArtInUse(old)) return "";
        this->crops.erase(old);
        return old;
    }

    std::string Library::CustomName(const std::string &tid) const {
        const json *a = JGet(this->apps, tid);
        return a ? JStr(*a, "name", "") : std::string();
    }

    void Library::SetCustomName(const std::string &tid, const std::string &name) {
        if(!IsTid(tid)) return;
        auto &a = this->apps[tid];
        if(!a.is_object()) a = json::object();
        std::string n = SanitizeUtf8(name.substr(0, 80));
        while(!n.empty() && (n.back() == ' ')) n.pop_back();
        while(!n.empty() && (n.front() == ' ')) n.erase(n.begin());
        if(n.empty()) a.erase("name");
        else a["name"] = n;
    }

    const char *StickerLabel(const std::string &id) {
        if(id == "new") return "Neu";
        if(id == "progress") return "Angefangen";
        if(id == "done") return "Durchgespielt";
        if(id == "100") return "100 %";
        if(id == "mp") return "Multiplayer";
        if(id == "coop") return "Koop";
        if(id == "top") return "Top";
        return "?";
    }

    std::vector<std::string> Library::Stickers(const std::string &tid) const {
        std::vector<std::string> out;
        const json *a = JGet(this->apps, tid);
        const json *s = a ? JGet(*a, "stickers") : nullptr;
        if(s && s->is_array()) for(const auto &x: *s) if(x.is_string()) out.push_back(x.get<std::string>());
        return out;
    }

    void Library::ToggleSticker(const std::string &tid, const std::string &sticker) {
        bool known = false;
        for(const char *k: StickerIds) if(sticker == k) known = true;
        if(!known || !IsTid(tid)) return;
        auto &a = this->apps[tid];
        if(!a.is_object()) a = json::object();
        if(!a.contains("stickers") || !a["stickers"].is_array()) a["stickers"] = json::array();
        json &arr = a["stickers"];
        auto it = std::find(arr.begin(), arr.end(), json(sticker));
        if(it != arr.end()) arr.erase(it);
        else arr.push_back(sticker);
        if(arr.empty()) a.erase("stickers");
    }

}
