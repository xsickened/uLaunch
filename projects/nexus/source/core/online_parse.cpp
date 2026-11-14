#include "online_parse.hpp"
#include "store.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace nexus {

    namespace {

        json Parse(const std::string &body) {
            if(body.empty() || body.size() > 16 * 1024 * 1024) return json();
            json j = json::parse(body, nullptr, false);
            if(j.is_discarded()) return json();
            return j;
        }

        double NumOf(const json *v, double def = 0) {
            if(!v) return def;
            if(v->is_number()) return v->get<double>();
            if(v->is_string()) {
                const std::string s = v->get<std::string>();
                char *end = nullptr;
                const double d = std::strtod(s.c_str(), &end);
                if(end && end != s.c_str()) return d;
            }
            if(v->is_boolean()) return v->get<bool>() ? 1 : 0;
            return def;
        }

        std::string StrOf(const json *v, size_t maxLen = 300) {
            if(!v) return "";
            if(v->is_string()) return SanitizeUtf8(v->get<std::string>().substr(0, maxLen));
            if(v->is_number_integer()) return std::to_string(v->get<long long>());
            return "";
        }

        const json *DataArray(const json &j, std::string &err) {
            if(j.is_object()) {
                const json *ok = JGetCI(j, "success");
                const json *data = JGetCI(j, "data");
                if(ok && ok->is_boolean() && !ok->get<bool>()) {
                    const json *errs = JGetCI(j, "errors");
                    if(errs && errs->is_array() && !errs->empty() && (*errs)[0].is_string()) err = SanitizeUtf8((*errs)[0].get<std::string>().substr(0, 120));
                    else err = "SteamGridDB meldet einen Fehler.";
                    return nullptr;
                }
                if(data && data->is_array()) return data;
            }
            if(err.empty()) err = "Unerwartete Antwort von SteamGridDB.";
            return nullptr;
        }

        bool SafeUrl(const std::string &u) {
            return u.size() < 600 && u.rfind("https://", 0) == 0 && u.find_first_of(" \"\r\n") == std::string::npos;
        }

    }

    const json *JGetCI(const json &obj, const char *key) {
        if(!obj.is_object()) return nullptr;
        auto it = obj.find(key);
        if(it != obj.end()) return &*it;
        const std::string k(key);
        for(auto i = obj.begin(); i != obj.end(); ++i) {
            const std::string &n = i.key();
            if(n.size() != k.size()) continue;
            bool eq = true;
            for(size_t c = 0; c < n.size() && eq; c++) eq = std::tolower(static_cast<unsigned char>(n[c])) == std::tolower(static_cast<unsigned char>(k[c]));
            if(eq) return &*i;
        }
        return nullptr;
    }

    std::string UrlEncode(const std::string &s) {
        static const char *hex = "0123456789ABCDEF";
        std::string o;
        for(const unsigned char c: s) {
            if(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
            else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
        }
        return o;
    }

    std::string SearchTerm(const std::string &name) {
        std::string s = SanitizeUtf8(name);
        for(const char *bad: { "\xE2\x84\xA2", "\xC2\xAE", "\xC2\xA9" }) {
            size_t p;
            while((p = s.find(bad)) != std::string::npos) s.erase(p, std::char_traits<char>::length(bad));
        }
        std::string o;
        bool sp = false;
        for(const char c: s) {
            const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
            if(ws) { if(!o.empty()) sp = true; continue; }
            if(sp) { o += ' '; sp = false; }
            o += c;
        }
        return o.substr(0, 80);
    }

    std::string ImageExt(const std::string &mime, const std::string &url) {
        if(mime == "image/png") return ".png";
        if(mime == "image/webp") return ".webp";
        if(mime == "image/jpeg") return ".jpg";
        std::string u = url;
        std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto q = u.find('?');
        if(q != std::string::npos) u.resize(q);
        for(const char *e: { ".png", ".webp", ".jpg", ".jpeg" }) {
            const std::string ext(e);
            if(u.size() > ext.size() && u.compare(u.size() - ext.size(), ext.size(), ext) == 0) return ext == ".jpeg" ? ".jpg" : ext;
        }
        return ".jpg";
    }

    bool ParseSgdbGames(const std::string &body, std::vector<SgdbGame> &out, std::string &err) {
        out.clear();
        const json j = Parse(body);
        const json *data = DataArray(j, err);
        if(!data) return false;
        for(const auto &g: *data) {
            if(!g.is_object() || out.size() >= 40) continue;
            SgdbGame s;
            s.id = static_cast<long long>(NumOf(JGetCI(g, "id")));
            s.name = StrOf(JGetCI(g, "name"), 120);
            const json *v = JGetCI(g, "verified");
            s.verified = v && v->is_boolean() && v->get<bool>();
            if(s.id > 0 && !s.name.empty()) out.push_back(s);
        }
        return true;
    }

    bool ParseSgdbImages(const std::string &body, std::vector<SgdbImage> &out, std::string &err) {
        out.clear();
        const json j = Parse(body);
        const json *data = DataArray(j, err);
        if(!data) return false;
        for(const auto &g: *data) {
            if(!g.is_object() || out.size() >= 60) continue;
            SgdbImage s;
            s.id = static_cast<long long>(NumOf(JGetCI(g, "id")));
            s.url = StrOf(JGetCI(g, "url"), 600);
            s.thumb = StrOf(JGetCI(g, "thumb"), 600);
            s.mime = StrOf(JGetCI(g, "mime"), 40);
            s.style = StrOf(JGetCI(g, "style"), 40);
            s.w = static_cast<int>(NumOf(JGetCI(g, "width")));
            s.h = static_cast<int>(NumOf(JGetCI(g, "height")));
            if(!SafeUrl(s.thumb)) s.thumb = s.url;
            if(s.id > 0 && SafeUrl(s.url)) out.push_back(s);
        }
        return true;
    }

    bool ParseRaProfile(const std::string &body, RaProfile &out) {
        out = RaProfile{};
        const json j = Parse(body);
        if(!j.is_object()) return false;
        out.user = StrOf(JGetCI(j, "User"), 60);
        out.hardcore = static_cast<long long>(NumOf(JGetCI(j, "TotalPoints")));
        out.softcore = static_cast<long long>(NumOf(JGetCI(j, "TotalSoftcorePoints")));
        out.retro = static_cast<long long>(NumOf(JGetCI(j, "TotalTruePoints")));
        out.ok = JGetCI(j, "TotalPoints") != nullptr;
        return out.ok;
    }

    bool ParseRaCompletion(const std::string &body, std::vector<RaGame> &out) {
        out.clear();
        const json j = Parse(body);
        const json *arr = nullptr;
        if(j.is_array()) arr = &j;
        else if(j.is_object()) arr = JGetCI(j, "Results");
        if(!arr || !arr->is_array()) return false;
        for(const auto &g: *arr) {
            if(!g.is_object() || out.size() >= 2000) continue;
            RaGame r;
            r.id = static_cast<long long>(NumOf(JGetCI(g, "GameID")));
            r.title = StrOf(JGetCI(g, "Title"), 160);
            r.console = StrOf(JGetCI(g, "ConsoleName"), 60);
            r.max = static_cast<int>(NumOf(JGetCI(g, "MaxPossible")));
            r.got = static_cast<int>(NumOf(JGetCI(g, "NumAwarded")));
            r.gotHard = static_cast<int>(NumOf(JGetCI(g, "NumAwardedHardcore")));
            r.lastDate = StrOf(JGetCI(g, "MostRecentAwardedDate"), 40);
            r.max = std::max(0, r.max);
            r.got = std::clamp(r.got, 0, std::max(r.max, r.got));
            if(r.id > 0 && !r.title.empty()) out.push_back(r);
        }
        return true;
    }

    bool ParseRaRecent(const std::string &body, std::vector<RaRecent> &out) {
        out.clear();
        const json j = Parse(body);
        if(!j.is_array()) return false;
        for(const auto &a: j) {
            if(!a.is_object() || out.size() >= 50) continue;
            RaRecent r;
            r.title = StrOf(JGetCI(a, "Title"), 120);
            r.game = StrOf(JGetCI(a, "GameTitle"), 120);
            r.date = StrOf(JGetCI(a, "Date"), 40);
            r.gameId = static_cast<long long>(NumOf(JGetCI(a, "GameID")));
            r.points = static_cast<int>(NumOf(JGetCI(a, "Points")));
            r.hardcore = NumOf(JGetCI(a, "HardcoreMode")) != 0;
            if(!r.title.empty()) out.push_back(r);
        }
        // neueste zuerst
        std::stable_sort(out.begin(), out.end(), [](const RaRecent &a, const RaRecent &b) { return a.date > b.date; });
        return true;
    }

    bool ParseRaGameProgress(const std::string &body, RaGame &game, std::vector<RaAchievement> &list) {
        list.clear();
        const json j = Parse(body);
        if(!j.is_object()) return false;
        game.id = static_cast<long long>(NumOf(JGetCI(j, "ID"), static_cast<double>(game.id)));
        const auto t = StrOf(JGetCI(j, "Title"), 160);
        if(!t.empty()) game.title = t;
        const auto c = StrOf(JGetCI(j, "ConsoleName"), 60);
        if(!c.empty()) game.console = c;
        game.max = static_cast<int>(NumOf(JGetCI(j, "NumAchievements")));
        game.got = static_cast<int>(NumOf(JGetCI(j, "NumAwardedToUser")));
        game.gotHard = static_cast<int>(NumOf(JGetCI(j, "NumAwardedToUserHardcore")));
        const json *ach = JGetCI(j, "Achievements");
        if(ach && ach->is_object()) {
            for(auto it = ach->begin(); it != ach->end() && list.size() < 1000; ++it) {
                const json &a = it.value();
                if(!a.is_object()) continue;
                RaAchievement r;
                r.title = StrOf(JGetCI(a, "Title"), 120);
                r.desc = StrOf(JGetCI(a, "Description"), 200);
                r.points = static_cast<int>(NumOf(JGetCI(a, "Points")));
                r.order = static_cast<int>(NumOf(JGetCI(a, "DisplayOrder")));
                r.hardcore = !StrOf(JGetCI(a, "DateEarnedHardcore")).empty();
                r.unlocked = r.hardcore || !StrOf(JGetCI(a, "DateEarned")).empty();
                if(!r.title.empty()) list.push_back(r);
            }
            std::stable_sort(list.begin(), list.end(), [](const RaAchievement &a, const RaAchievement &b) { return a.order < b.order; });
        }
        return true;
    }

    static std::string NormalizeImpl(const std::string &in, bool spaced) {
        // Latin-1-Akzente (UTF-8 C3 xx) auf Grundbuchstaben abbilden
        static const char *const map = "aaaaaaaceeeeiiiidnooooo/ouuuuypy";   // C3 A0..BF (Kleinbuchstaben)
        std::string s;
        for(size_t i = 0; i < in.size(); i++) {
            const unsigned char c = static_cast<unsigned char>(in[i]);
            if(c == 0xC3 && i + 1 < in.size()) {
                unsigned char d = static_cast<unsigned char>(in[i + 1]);
                i++;
                if(d == 0x9F) { s += "ss"; continue; }
                if(d >= 0x80 && d <= 0x9E) d = static_cast<unsigned char>(d + 0x20);
                if(d >= 0xA0 && d <= 0xBF) { const char m = map[d - 0xA0]; if(std::isalpha(static_cast<unsigned char>(m))) s += m; }
                continue;
            }
            if(c >= 0x80) continue;
            s += static_cast<char>(std::tolower(c));
        }
        // Klammerzusätze entfernen
        std::string t;
        int depth = 0;
        for(const char c: s) {
            if(c == '(' || c == '[') { depth++; continue; }
            if(c == ')' || c == ']') { if(depth > 0) depth--; continue; }
            if(depth == 0) t += c;
        }
        // in Wörter zerlegen, „the“ auslassen, & -> and
        std::string out, word;
        auto flush = [&]() {
            if(!word.empty() && word != "the") { if(spaced && !out.empty()) out += ' '; out += word; }
            word.clear();
        };
        for(const char c: t) {
            if(std::isalnum(static_cast<unsigned char>(c))) word += c;
            else if(c == '&') { flush(); word = "and"; flush(); }
            else flush();
        }
        flush();
        return out;
    }

    std::string NormalizeTitle(const std::string &in) { return NormalizeImpl(in, false); }
    std::string SearchKey(const std::string &in) { return NormalizeImpl(in, true); }

    bool ParseRaConsoles(const std::string &body, std::vector<RaConsole> &out) {
        out.clear();
        const json j = Parse(body);
        if(!j.is_array()) return false;
        for(const auto &c: j) {
            if(!c.is_object() || out.size() >= 200) continue;
            const json *gs = JGetCI(c, "IsGameSystem");
            if(gs && gs->is_boolean() && !gs->get<bool>()) continue;   // Hubs, Events usw.
            RaConsole r;
            r.id = static_cast<int>(NumOf(JGetCI(c, "ID")));
            r.name = StrOf(JGetCI(c, "Name"), 60);
            if(r.id > 0 && !r.name.empty()) out.push_back(r);
        }
        return !out.empty();
    }

    bool ParseRaGameList(const std::string &body, int consoleId, std::vector<RaIndexEntry> &out) {
        const json j = Parse(body);
        if(!j.is_array()) return false;
        for(const auto &g: j) {
            if(!g.is_object() || out.size() >= 200000) continue;
            RaIndexEntry e;
            e.id = static_cast<long long>(NumOf(JGetCI(g, "ID")));
            e.title = StrOf(JGetCI(g, "Title"), 160);
            e.console = consoleId;
            e.achievements = static_cast<int>(NumOf(JGetCI(g, "NumAchievements")));
            if(e.id <= 0 || e.title.empty()) continue;
            e.norm = SearchKey(e.title);
            out.push_back(std::move(e));
        }
        return true;
    }

    std::vector<size_t> SearchRaIndex(const std::vector<RaIndexEntry> &index, const std::string &term, size_t limit) {
        const std::string q = SearchKey(term);
        std::vector<std::string> words;
        {
            std::string w;
            for(char c: q + " ") {
                if(c == ' ') { if(!w.empty()) words.push_back(w); w.clear(); }
                else w += c;
            }
        }
        std::vector<std::pair<int, size_t>> hits;
        if(words.empty()) return {};
        for(size_t i = 0; i < index.size(); i++) {
            const std::string &t = index[i].norm;
            bool all = true;
            int score = 0;
            for(const auto &w: words) {
                const size_t p = t.find(w);
                if(p == std::string::npos) { all = false; break; }
                score += (p == 0 || t[p - 1] == ' ') ? 2 : 1;   // Wortanfang zählt mehr
            }
            if(!all) continue;
            if(t == q) score += 100;
            else if(t.rfind(q, 0) == 0) score += 40;
            score -= static_cast<int>(std::min<size_t>(t.size(), 60) / 6);   // kürzere Titel zuerst
            if(index[i].achievements <= 0) score -= 20;
            hits.emplace_back(-score, i);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<size_t> out;
        for(const auto &h: hits) { if(out.size() >= limit) break; out.push_back(h.second); }
        return out;
    }

    long long MatchRaGame(const std::string &name, const std::vector<RaGame> &games) {
        const std::string n = NormalizeTitle(name);
        if(n.size() < 3) return 0;
        long long exact = 0;
        int exactCount = 0;
        for(const auto &g: games) {
            if(NormalizeTitle(g.title) == n) { exact = g.id; exactCount++; }
        }
        if(exactCount == 1) return exact;
        if(exactCount > 1) return 0;
        // eindeutiger Präfix-Treffer (z. B. „Pokemon Red“ ↔ „Pokemon Red Version“)
        if(n.size() < 6) return 0;
        long long pre = 0;
        int preCount = 0;
        for(const auto &g: games) {
            const std::string t = NormalizeTitle(g.title);
            if(t.size() < 6) continue;
            if(t.rfind(n, 0) == 0 || n.rfind(t, 0) == 0) { if(pre != g.id) { pre = g.id; preCount++; } }
        }
        return preCount == 1 ? pre : 0;
    }

}
