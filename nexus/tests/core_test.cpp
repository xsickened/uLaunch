// Host-Tests für den plattformunabhängigen Kern (Schema, Bibliothek, Speichern, Anordnungen).
// Bauen: g++ -std=gnu++20 -O1 -I<json>/single_include -I../../projects/nexus/source/core core_test.cpp ../../projects/nexus/source/core/*.cpp
#include "settings.hpp"
#include "library.hpp"
#include "store.hpp"
#include "layout.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>

using namespace nexus;

static int g_Fail = 0;
#define CHECK(c) do { if(!(c)) { std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #c); g_Fail++; } } while(0)

static json RandomJunk(std::mt19937 &r) {
    switch(r() % 12) {
        case 0: return json(nullptr);
        case 1: return json(true);
        case 2: return json(-1e12);
        case 3: return json(1e12);
        case 4: return json("x");
        case 5: return json("#zzzzzz");
        case 6: return json::object();
        case 7: return json::array({ 1, 2 });
        case 8: return json(0.5);
        case 9: return json("#ffffff");
        case 10: return json("grid");
        default: return json(std::nan(""));
    }
}

static bool AllValid(const json &settings) {
    for(const auto &e: Schema()) {
        const json *v = JGet(settings, e.path);
        if(!v || !Settings::ValidValue(e, *v)) { std::printf("  ungueltig: %s\n", e.path.c_str()); return false; }
    }
    return true;
}

int main() {
    // 1. Standardwerte sind gültig
    CHECK(AllValid(Settings::Defaults()));
    CHECK(Schema().size() >= 100);

    // 2. Fuzz: 2000 kaputte Konfigurationen -> immer gültig, "settings" immer sichtbar
    std::mt19937 r(42);
    for(int t = 0; t < 2000; t++) {
        json in = Settings::Defaults();
        for(const auto &e: Schema()) if(r() % 3 == 0) JSet(in, e.path, RandomJunk(r));
        if(r() % 4 == 0) in["sections"] = json::array({ { { "id", "settings" }, { "visible", false } }, { { "id", "x" } }, 5 });
        if(r() % 5 == 0) in = RandomJunk(r);
        std::vector<std::string> rep;
        Settings s; s.data = Settings::Sanitize(in, &rep);
        CHECK(AllValid(s.data));
        bool settingsVisible = false;
        for(const auto &id: s.VisibleSections()) if(id == "settings") settingsVisible = true;
        CHECK(settingsVisible);
        CHECK(s.data["sections"].size() == 7);
    }

    // 3. Bibliothek: jeder installierte Titel genau einmal erreichbar
    for(int t = 0; t < 500; t++) {
        std::vector<std::string> inst;
        for(int i = 0; i < 20; i++) { char b[17]; std::snprintf(b, sizeof(b), "0100%04X00000000", static_cast<unsigned>(r() % 30)); inst.push_back(b); }
        json j = json::object();
        j["apps"] = { { "0100000000000000", { { "favorite", true } } }, { "bad", 1 } };
        json fs = json::array();
        for(int k = 0; k < 3; k++) {
            json items = json::array();
            for(int i = 0; i < 6; i++) items.push_back(inst[r() % inst.size()]);
            items.push_back("ghost");
            fs.push_back({ { "id", "fld-" + std::to_string(k % 2) }, { "name", "F" }, { "items", items } });
        }
        j["folders"] = fs;
        j["rootOrder"] = json::array({ inst[0], inst[0], "fld-9", 7 });
        std::vector<std::string> rep;
        Library lib = Library::FromJson(j, &rep);
        lib.Reconcile(inst, &rep);
        std::map<std::string, int> seen;
        for(const auto &id: lib.rootOrder) seen[id]++;
        for(const auto &f: lib.folders) for(const auto &i: f.items) seen[i]++;
        for(const auto &i: inst) CHECK(seen[i] == 1);
        for(const auto &f: lib.folders) CHECK(seen[f.id] == 1);
        // Ordner löschen: Inhalt landet auf der Startseite
        if(!lib.folders.empty()) {
            const auto fid = lib.folders[0].id;
            lib.DeleteFolder(fid);
            seen.clear();
            for(const auto &id: lib.rootOrder) seen[id]++;
            for(const auto &f: lib.folders) for(const auto &i: f.items) seen[i]++;
            for(const auto &i: inst) CHECK(seen[i] == 1);
        }
    }

    // 4. Speichern/Laden inkl. beschädigter Dateien
    {
        const std::string dir = "/tmp/nexus_test";
        EnsureDir(dir);
        const std::string p = dir + "/nexus.json";
        std::remove(p.c_str()); std::remove((p + ".bak").c_str()); std::remove((p + ".tmp").c_str());
        NexusData d; d.settings.data = Settings::Defaults();
        d.settings.Set("layout.mode", "arc");
        d.library.CreateFolder("Test", 0);
        CHECK(SaveData(p, d));
        d.settings.Set("layout.mode", "ring");
        CHECK(SaveData(p, d));
        std::string src; std::vector<std::string> rep;
        auto l = LoadData(p, src, rep);
        CHECK(l.settings.S("layout.mode") == "ring");
        CHECK(l.library.folders.size() == 1);
        FILE *f = std::fopen(p.c_str(), "wb"); std::fputs("{kaputt", f); std::fclose(f);
        l = LoadData(p, src, rep);
        CHECK(src == p + ".bak");
        CHECK(l.settings.S("layout.mode") == "arc");
        f = std::fopen((p + ".bak").c_str(), "wb"); std::fputs("[]", f); std::fclose(f);
        l = LoadData(p, src, rep);
        CHECK(src == "defaults");
        CHECK(AllValid(l.settings.data));
        const std::string bp = dir + "/boot.cnt";
        std::remove(bp.c_str());
        CHECK(!BootGuardStart(bp)); CHECK(!BootGuardStart(bp)); CHECK(BootGuardStart(bp));
        BootGuardOk(bp); CHECK(!BootGuardStart(bp));
    }

    // 5. Alle Anordnungen: keine NaN, Navigation bleibt im Bereich
    {
        Settings s; s.data = Settings::Defaults();
        std::vector<LItem> items;
        const char *sizes[] = { "S", "M", "L", "XL", "custom" };
        const char *ratios[] = { "1:1", "2:3", "3:2", "4:3", "16:9", "custom" };
        for(int i = 0; i < 60; i++) items.push_back({ sizes[i % 5], ratios[i % 6], 3.0 + i % 12, 0.5 + (i % 7) * 0.3 });
        const auto *modeE = Settings::Find("layout.mode");
        for(const auto &m: modeE->opts) {
            for(const char *dir: { "up", "down", "left", "right" }) {
                s.Set("layout.mode", m); s.Set("layout.arcDir", std::string(dir));
                for(int sz: { 0, 1, 60 }) {
                    std::vector<LItem> sub(items.begin(), items.begin() + sz);
                    double scroll = 0; int focus = 0;
                    LayoutEnv env;
                    for(int step = 0; step < 200; step++) {
                        auto res = ComputeLayout(s, env, sub, focus, scroll);
                        CHECK(static_cast<int>(res.items.size()) == sz);
                        for(const auto &it: res.items) CHECK(std::isfinite(it.x) && std::isfinite(it.y) && std::isfinite(it.w) && std::isfinite(it.h) && it.w > 0 && it.h > 0);
                        const int dx = (step % 4 == 0) ? 1 : (step % 4 == 1) ? -1 : 0, dy = (step % 4 == 2) ? 1 : (step % 4 == 3) ? -1 : 0;
                        focus = Navigate(res, s, focus, step % 3 ? 1 : dx, dy);
                        CHECK(focus >= 0 && (sz == 0 || focus < sz));
                    }
                }
            }
        }
        // Raster: keine Überlappung
        s.Set("layout.mode", "grid");
        double scroll = 0; LayoutEnv env;
        auto res = ComputeLayout(s, env, items, 0, scroll);
        for(size_t i = 0; i < res.items.size(); i++) for(size_t j = i + 1; j < res.items.size(); j++) {
            const auto &a = res.items[i], &b = res.items[j];
            CHECK(!(a.x < b.x + b.w - 0.01 && b.x < a.x + a.w - 0.01 && a.y < b.y + b.h - 0.01 && b.y < a.y + a.h - 0.01));
        }
    }

    // 6. Ungültiges UTF-8 darf Speichern nicht abbrechen (Absturzursache auf der Konsole)
    {
        const std::string bad = std::string("Spiel \xC3\x28 \xFF Ende \xE2\x82", 20);
        const std::string clean = SanitizeUtf8(bad);
        json j = json::object();
        j["a"] = bad;
        const std::string dumped = SafeDump(j);
        CHECK(!dumped.empty());
        json k = json::object();
        k["a"] = clean;
        CHECK(json::parse(k.dump(), nullptr, false).is_object());
        CHECK(SanitizeUtf8("Pokémon ★ 日本") == "Pokémon ★ 日本");
        NexusData d; d.settings.data = Settings::Defaults();
        d.library.CreateFolder(bad, 0);
        CHECK(SaveData("/tmp/nexus_test/utf8.json", d));
    }

    std::printf(g_Fail ? "%d Fehler\n" : "Alle Kern-Tests bestanden.\n", g_Fail);
    return g_Fail ? 1 : 0;
}
