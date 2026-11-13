// Host-Tests für den plattformunabhängigen Kern (Schema, Bibliothek, Speichern, Anordnungen).
// Bauen: g++ -std=gnu++20 -O1 -I<json>/single_include -I../../projects/nexus/source/core core_test.cpp ../../projects/nexus/source/core/*.cpp
#include "settings.hpp"
#include "accent.hpp"
#include "online_parse.hpp"
#include "particles.hpp"
#include "library.hpp"
#include "store.hpp"
#include "layout.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    // 7. Akzent: Verläufe und RGB bleiben im Farbraum, Ränder stimmen
    {
        Settings st; st.data = Settings::Defaults();
        AccentParams p = AccentFrom(st);
        CHECK(p.mode == "solid");
        Rgb c = AccentAt(p, 0.3, 12.0);
        CHECK(std::lround(c.r) == 0x3e && std::lround(c.g) == 0xe0 && std::lround(c.b) == 0xc8);
        st.Set("theme.accentMode", json("gradient"));
        st.Set("theme.accentAnim", json("none"));
        p = AccentFrom(st);
        const Rgb a0 = AccentAt(p, 0, 0), a1 = AccentAt(p, 1, 0);
        CHECK(std::lround(a0.r) == 0x3e && std::lround(a1.r) == 0xe8 && std::lround(a1.b) == 0xff);
        CHECK(std::fabs(AccentU(p, 0, 0, 100, 50)) < 1e-9 && std::fabs(AccentU(p, 100, 50, 100, 50) - 1) < 1e-9);
        for(const char *m: { "gradient", "gradient3", "rainbow", "solid" }) for(const char *an: { "none", "flow", "cycle", "pulse" }) {
            st.Set("theme.accentMode", json(m)); st.Set("theme.accentAnim", json(an));
            p = AccentFrom(st);
            for(int ang = 0; ang < 360; ang += 45) {
                p.angle = ang;
                for(double t = -5; t < 40; t += 3.7) for(double u = -0.5; u <= 1.5; u += 0.25) {
                    const Rgb x = AccentAt(p, AccentU(p, u * 300, u * 200, 300, 200), t);
                    CHECK(x.r >= 0 && x.r <= 255 && x.g >= 0 && x.g <= 255 && x.b >= 0 && x.b <= 255 && std::isfinite(x.r));
                }
            }
        }
        st.Set("theme.accentMode", json("rainbow")); st.Set("theme.accentAnim", json("none")); st.Set("theme.accent", json("#ff0000"));
        p = AccentFrom(st);
        const Rgb r0 = AccentAt(p, 0, 0);
        CHECK(r0.r > 250 && r0.g < 60);
        CHECK(std::isfinite(AccentAt(p, std::nan(""), std::nan("")).r));
        // ungültige Werte -> Standard
        json bad = Settings::Defaults(); JSet(bad, "theme.accentMode", json("disco")); JSet(bad, "theme.accentCycle", json(-3));
        std::vector<std::string> rep; json fixed = Settings::Sanitize(bad, &rep);
        CHECK(*JGet(fixed, "theme.accentMode") == "solid" && JNum(*JGet(fixed, "theme"), "accentCycle", 0) == 1);
        // Sperrbildschirm zeigt kein Spielbild mehr: alter Wert "hero" wird ersetzt
        JSet(bad, "lock.bgType", json("hero"));
        fixed = Settings::Sanitize(bad, &rep);
        CHECK(*JGet(fixed, "lock.bgType") == "gradient");
    }

    // 8. SteamGridDB- und RetroAchievements-Antworten (inkl. kaputter Eingaben)
    {
        std::vector<SgdbGame> games; std::string err;
        CHECK(ParseSgdbGames(R"({"success":true,"data":[{"id":2254,"name":"Half-Life 2","types":["steam"],"verified":true},{"id":0,"name":"x"},{"name":"ohne id"}]})", games, err));
        CHECK(games.size() == 1 && games[0].id == 2254 && games[0].verified);
        CHECK(!ParseSgdbGames(R"({"success":false,"errors":["Invalid API key"]})", games, err) && err == "Invalid API key");
        CHECK(!ParseSgdbGames("<html>", games, err));
        CHECK(!ParseSgdbGames("", games, err));
        std::vector<SgdbImage> ims;
        CHECK(ParseSgdbImages(R"({"success":true,"data":[{"id":5,"url":"https://cdn2.steamgriddb.com/grid/a.png","thumb":"https://cdn2.steamgriddb.com/thumb/a.jpg","mime":"image/png","width":600,"height":900,"style":"alternate"},{"id":6,"url":"http://unsicher/x.png"},{"id":7,"url":"https://x/y z.png"}]})", ims, err));
        CHECK(ims.size() == 1 && ims[0].w == 600 && ImageExt(ims[0].mime, ims[0].url) == ".png" && ImageExt("", ims[0].thumb) == ".jpg");
        CHECK(ImageExt("", "https://a/b.WEBP?x=1") == ".webp" && ImageExt("", "https://a/b") == ".jpg");
        CHECK(UrlEncode("Zelda: Link's Awakening & Co") == "Zelda%3A%20Link%27s%20Awakening%20%26%20Co");
        CHECK(SearchTerm("  Mario\xE2\x84\xA2   Kart  8 ") == "Mario Kart 8");

        RaProfile pr;
        CHECK(ParseRaProfile(R"({"User":"Kevin","TotalPoints":12345,"TotalSoftcorePoints":"230","TotalTruePoints":31870})", pr) && pr.hardcore == 12345 && pr.softcore == 230 && pr.retro == 31870);
        CHECK(!ParseRaProfile("[]", pr));
        std::vector<RaGame> rg;
        CHECK(ParseRaCompletion(R"({"Count":2,"Total":2,"Results":[{"GameID":1,"Title":"Pokemon Red Version","ConsoleName":"Game Boy","MaxPossible":40,"NumAwarded":12,"NumAwardedHardcore":10,"MostRecentAwardedDate":"2026-01-02"},{"GameID":2,"Title":"Legend of Zelda, The: A Link to the Past","ConsoleName":"SNES","MaxPossible":"60","NumAwarded":60,"NumAwardedHardcore":60}]})", rg));
        CHECK(rg.size() == 2 && rg[1].max == 60 && rg[0].gotHard == 10);
        CHECK(MatchRaGame("Pokémon Red (USA)", rg) == 1);
        CHECK(MatchRaGame("The Legend of Zelda: A Link to the Past", rg) == 2);
        CHECK(MatchRaGame("Super Mario Odyssey", rg) == 0);
        CHECK(MatchRaGame("Po", rg) == 0);
        CHECK(NormalizeTitle("Ñoño & Friends [Hack] (EU)") == "nonoandfriends");
        std::vector<RaRecent> rr;
        CHECK(ParseRaRecent(R"([{"Date":"2026-01-01 10:00:00","HardcoreMode":1,"Title":"Alt","GameTitle":"G","GameID":1,"Points":5},{"Date":"2026-02-01 10:00:00","HardcoreMode":0,"Title":"Neu","GameTitle":"G","GameID":1,"Points":10}])", rr));
        CHECK(rr.size() == 2 && rr[0].title == "Neu" && rr[1].hardcore);
        RaGame gp; std::vector<RaAchievement> al;
        CHECK(ParseRaGameProgress(R"({"ID":14402,"Title":"Dragster","ConsoleName":"Atari 2600","NumAchievements":3,"NumAwardedToUser":1,"NumAwardedToUserHardcore":1,"Achievements":{"9":{"Title":"B","Points":5,"DisplayOrder":2},"8":{"Title":"A","Points":1,"DisplayOrder":1,"DateEarned":"x","DateEarnedHardcore":"x"}}})", gp, al));
        CHECK(gp.max == 3 && al.size() == 2 && al[0].title == "A" && al[0].hardcore && !al[1].unlocked);
        // Zufallsmüll darf nie abstürzen
        std::string junk;
        for(int i = 0; i < 3000; i++) { junk += static_cast<char>((i * 7919) % 256); if(i % 50 == 0) { ParseSgdbGames(junk, games, err); ParseRaCompletion(junk, rg); ParseRaRecent(junk, rr); ParseRaGameProgress(junk, gp, al); NormalizeTitle(junk); } }
    }

    // 9. Eigene Bildauswahl wird gespeichert, unsichere Pfade verworfen
    {
        NexusData d; d.settings.data = Settings::Defaults();
        d.library.SetArtOverride("0100000000010000", "hero", "sdmc:/nexus/art/backgrounds/a.jpg");
        d.library.SetArtOverride("0100000000010000", "logo", "sdmc:/../etc/passwd");
        d.library.SetArtOverride("0100000000010000", "evil", "sdmc:/x.jpg");
        d.library.SetRaGameId("0100000000010000", 14402);
        const auto fid = d.library.CreateFolder("Retro", 0);
        d.library.SetArtOverride(fid, "grid", "sdmc:/nexus/art/f.png");
        CHECK(SaveData("/tmp/nexus_test/art.json", d));
        std::string src; std::vector<std::string> rep2;
        NexusData e = LoadData("/tmp/nexus_test/art.json", src, rep2);
        CHECK(e.library.ArtOverride("0100000000010000", "hero") == "sdmc:/nexus/art/backgrounds/a.jpg");
        CHECK(e.library.ArtOverride("0100000000010000", "logo").empty());
        CHECK(e.library.RaGameId("0100000000010000") == 14402);
        CHECK(e.library.ArtOverride(fid, "grid") == "sdmc:/nexus/art/f.png");
        e.library.SetArtOverride("0100000000010000", "hero", "");
        CHECK(e.library.ArtOverride("0100000000010000", "hero").empty());
        e.settings.Set("home.image", json("sdmc:/Nintendo/Album/2026/11/13/x.jpg"));
        CHECK(e.settings.S("home.image") == "sdmc:/Nintendo/Album/2026/11/13/x.jpg");
    }

    // 10. Update-Sicherheit: unbekannte Werte bleiben, Schlüssel überleben, Themes, Sicherungen, Bildausschnitte
    {
        if(std::system("rm -rf /tmp/nexus_test/u && mkdir -p /tmp/nexus_test/u") != 0) std::printf("Hinweis: Testordner nicht neu angelegt\n");
        json in = Settings::Defaults();
        JSet(in, "zukunft.option", json(42));
        JSet(in, "theme.neuerWert", json("x"));
        JSet(in, "theme.accentMode", json("disco"));
        JSet(in, "theme", *JGet(in, "theme"));
        json out = Settings::Sanitize(in, nullptr);
        CHECK(JNum(*JGet(out, "zukunft"), "option", 0) == 42);
        CHECK(*JGet(out, "theme.neuerWert") == "x");
        CHECK(*JGet(out, "theme.accentMode") == "solid");
        // Wert anstelle eines Schema-Objekts wird nicht übernommen
        json in2 = Settings::Defaults(); in2["hero"] = 5;
        CHECK(JGet(Settings::Sanitize(in2, nullptr), "hero.enabled") != nullptr);

        NexusData d; d.settings.data = Settings::Defaults();
        d.settings.Set("sgdbKey", json("abcdefgh12345"));
        d.settings.Set("ra.user", json("Kevin"));
        d.settings.Set("ra.key", json("rakey1234567"));
        CHECK(SaveData("/tmp/nexus_test/u/nexus.json", d));
        std::remove("/tmp/nexus_test/u/nexus.json"); std::remove("/tmp/nexus_test/u/nexus.json.bak");
        std::string src; std::vector<std::string> rep;
        NexusData e = LoadData("/tmp/nexus_test/u/nexus.json", src, rep);
        CHECK(src == "defaults" && e.settings.S("sgdbKey") == "abcdefgh12345" && e.settings.S("ra.key") == "rakey1234567" && e.settings.S("ra.user") == "Kevin");
        // Leere Schlüssel überschreiben keys.json nie
        NexusData empty; empty.settings.data = Settings::Defaults();
        CHECK(SaveData("/tmp/nexus_test/u/nexus.json", empty));
        NexusData f = LoadData("/tmp/nexus_test/u/nexus.json", src, rep);
        CHECK(f.settings.S("sgdbKey") == "abcdefgh12345");

        // Themes: speichern, auflisten, laden ohne Schlüssel zu verlieren
        e.settings.Set("theme.accent", json("#ff0000"));
        e.settings.Set("layout.mode", json("ring"));
        std::string tp;
        CHECK(SaveUserTheme("/tmp/nexus_test/u/themes", "Mein Rot!", e.settings.data, &tp));
        std::string raw; ReadFile(tp, raw);
        CHECK(raw.find("abcdefgh12345") == std::string::npos && raw.find("rakey") == std::string::npos);
        const auto themes = ListUserThemes("/tmp/nexus_test/u/themes");
        CHECK(themes.size() == 1 && themes[0].name == "Mein Rot!");
        json cur = Settings::Defaults(); JSet(cur, "sgdbKey", json("anderer-key-123"));
        json res;
        CHECK(ApplyUserTheme(themes[0].path, cur, res));
        CHECK(*JGet(res, "theme.accent") == "#ff0000" && *JGet(res, "layout.mode") == "ring" && *JGet(res, "sgdbKey") == "anderer-key-123");
        CHECK(!ApplyUserTheme("/tmp/nexus_test/u/fehlt.json", cur, res));

        // Tägliche Sicherung, höchstens 10
        CHECK(SaveData("/tmp/nexus_test/u/nexus.json", e));
        for(int i = 1; i <= 13; i++) { char dt[16]; std::snprintf(dt, sizeof(dt), "2026-01-%02d", i); BackupDaily("/tmp/nexus_test/u/nexus.json", dt); }
        int n = 0; for(int i = 1; i <= 13; i++) { char fp[80]; std::snprintf(fp, sizeof(fp), "/tmp/nexus_test/u/backup/nexus-2026-01-%02d.json", i); if(FileExists(fp)) n++; }
        CHECK(n == 10 && FileExists("/tmp/nexus_test/u/backup/nexus-2026-01-13.json") && !FileExists("/tmp/nexus_test/u/backup/nexus-2026-01-01.json"));

        // Bildausschnitt
        Crop c; c.zoom = 1.8; c.x = 20; c.y = 90; c.set = true;
        e.library.SetCrop("sdmc:/nexus/art/backgrounds/a.jpg", c);
        Crop bad; bad.zoom = 99; bad.x = -5; bad.y = 500; bad.set = true;
        e.library.SetCrop("sdmc:/nexus/art/b.jpg", bad);
        e.library.SetCrop("/etc/x.jpg", c);
        CHECK(SaveData("/tmp/nexus_test/u/nexus.json", e));
        NexusData g = LoadData("/tmp/nexus_test/u/nexus.json", src, rep);
        const Crop c2 = g.library.CropOf("sdmc:/nexus/art/backgrounds/a.jpg");
        CHECK(c2.set && std::fabs(c2.zoom - 1.8) < 1e-9 && c2.x == 20 && c2.y == 90);
        const Crop c3 = g.library.CropOf("sdmc:/nexus/art/b.jpg");
        CHECK(c3.zoom == 4 && c3.x == 0 && c3.y == 100);
        CHECK(!g.library.CropOf("/etc/x.jpg").set);
        Crop reset; reset.set = true; g.library.SetCrop("sdmc:/nexus/art/b.jpg", reset);
        CHECK(!g.library.CropOf("sdmc:/nexus/art/b.jpg").set);
    }

    // 11. Partikel: alle Varianten bleiben endlich, begrenzt und sterben wieder
    {
        Settings st; st.data = Settings::Defaults();
        CHECK(!AuraFrom(st).enabled && BgFrom(st).type == "none");
        ParticleSystem ps;
        auto acc = [](double, double) { return Rgb{ 10, 200, 30 }; };
        std::vector<EmitRect> rects = { { 640, 360, 200, 300 }, { 100, 100, 0, 0 } };
        for(const char *dir: { "outward", "up", "down", "left", "right", "orbit", "inward", "random" })
            for(const char *em: { "edge", "corners", "inside", "around" })
                for(const char *cm: { "accent", "fixed", "rainbow", "random", "image" }) {
                    AuraCfg c; c.enabled = true; c.direction = dir; c.emit = em; c.colorMode = cm; c.density = 150; c.gravity = -200; c.spread = 180;
                    ps.SpawnAura(c, rects, 0.05, 1.0, acc);
                    ps.Update(0.05, 1.0);
                }
        CHECK(!ps.parts.empty() && ps.parts.size() <= ParticleSystem::MaxParticles);
        for(const auto &p: ps.parts) { float a, sz; ParticleSystem::Look(p, 2.0, a, sz); CHECK(std::isfinite(p.x) && a >= 0 && a <= 1 && sz > 0); }
        for(const char *t: { "snow", "stars", "bubbles", "embers", "rain", "fireflies", "custom" }) {
            BgCfg b; b.type = t; b.density = 150; b.colorMode = "rainbow";
            for(int i = 0; i < 40; i++) { ps.SpawnBg(b, 0.05, i * 0.05, 1280, 720, acc); ps.Update(0.05, i * 0.05); }
            CHECK(!BgShape(b).empty());
        }
        CHECK(ps.parts.size() <= ParticleSystem::MaxParticles);
        // ohne Nachschub ist nach einiger Zeit alles verschwunden
        for(int i = 0; i < 400; i++) ps.Update(0.1, 10 + i * 0.1);
        CHECK(ps.parts.empty());
        // Akzent-Farbe wird übernommen
        AuraCfg c; c.enabled = true; c.density = 100;
        ps.SpawnAura(c, { { 0, 0, 10, 10 } }, 0.1, 0, acc);
        CHECK(!ps.parts.empty() && ps.parts[0].g == 200);
        // dt = 0 / leere Liste: nichts passiert
        ps.Clear(); ps.SpawnAura(c, {}, 0.1, 0, acc); ps.SpawnAura(c, rects, 0, 0, acc);
        CHECK(ps.parts.empty());
    }

    // 12. Ersetzte Downloads werden freigegeben, eigene Dateien nie
    {
        Library lib;
        const std::string a = "sdmc:/nexus/art/0100000000010000/sgdb-hero-1.png", b = "sdmc:/nexus/art/0100000000010000/sgdb-hero-2.png";
        CHECK(lib.ReplaceArt("0100000000010000", "hero", a).empty());
        Crop c; c.zoom = 2; c.set = true; lib.SetCrop(a, c);
        CHECK(lib.ReplaceArt("0100000000010000", "hero", b) == a);
        CHECK(!lib.CropOf(a).set);
        CHECK(lib.ReplaceArt("0100000000010000", "hero", "sdmc:/nexus/art/backgrounds/mein.jpg") == b);
        CHECK(lib.ReplaceArt("0100000000010000", "hero", "").empty());   // eigenes Bild wird nie gelöscht
        // dieselbe Datei noch woanders in Benutzung -> nicht löschen
        lib.ReplaceArt("0100000000010000", "grid", a);
        lib.ReplaceArt("0100000000020000", "grid", a);
        CHECK(lib.ReplaceArt("0100000000010000", "grid", b).empty());
        CHECK(lib.ArtInUse(a) && lib.AllArtOverrides().size() == 2);
    }

    // 13. Eigene Anzeigenamen
    {
        NexusData d; d.settings.data = Settings::Defaults();
        d.library.SetCustomName("0100000000010000", "  Mein Lieblingsspiel  ");
        d.library.SetCustomName("fld-12345678", "x");
        d.library.SetCustomName("0100000000020000", std::string("Kaputt \xFF") + std::string(200, 'a'));
        CHECK(SaveData("/tmp/nexus_test/name.json", d));
        std::string src; std::vector<std::string> rep;
        NexusData e = LoadData("/tmp/nexus_test/name.json", src, rep);
        CHECK(e.library.CustomName("0100000000010000") == "Mein Lieblingsspiel");
        CHECK(e.library.CustomName("0100000000020000").size() <= 80 && !e.library.CustomName("0100000000020000").empty());
        e.library.SetCustomName("0100000000010000", "   ");
        CHECK(e.library.CustomName("0100000000010000").empty());
    }

    std::printf(g_Fail ? "%d Fehler\n" : "Alle Kern-Tests bestanden.\n", g_Fail);
    return g_Fail ? 1 : 0;
}
