#include "settings.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace nexus {

    const char *const SectionIds[8] = { "start", "all", "fav", "folders", "recent", "homebrew", "almost", "settings" };
    const char *const Palette[11] = { "#3ee0c8", "#ff5d73", "#ffb347", "#7c8cff", "#5fd35f", "#e86cff", "#ffffff", "#000000", "#222831", "#00a2ff", "#ffd447" };

    const json *JGet(const json &o, const std::string &path) {
        const json *cur = &o;
        size_t start = 0;
        while(true) {
            if(!cur->is_object()) {
                return nullptr;
            }
            const auto dot = path.find('.', start);
            const auto key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            const auto it = cur->find(key);
            if(it == cur->end()) {
                return nullptr;
            }
            cur = &(*it);
            if(dot == std::string::npos) {
                return cur;
            }
            start = dot + 1;
        }
    }

    void JSet(json &o, const std::string &path, const json &v) {
        json *cur = &o;
        size_t start = 0;
        while(true) {
            if(!cur->is_object()) {
                *cur = json::object();
            }
            const auto dot = path.find('.', start);
            const auto key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if(dot == std::string::npos) {
                (*cur)[key] = v;
                return;
            }
            cur = &(*cur)[key];
            start = dot + 1;
        }
    }

    std::string JStr(const json &o, const char *key, const std::string &def) {
        if(!o.is_object()) return def;
        const auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : def;
    }

    double JNum(const json &o, const char *key, double def) {
        if(!o.is_object()) return def;
        const auto it = o.find(key);
        if(it == o.end() || !it->is_number()) return def;
        const double v = it->get<double>();
        return std::isfinite(v) ? v : def;
    }

    bool JBool(const json &o, const char *key, bool def) {
        if(!o.is_object()) return def;
        const auto it = o.find(key);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }

    static bool IsHexColor(const std::string &s) {
        if(s.size() != 7 || s[0] != '#') return false;
        for(size_t i = 1; i < 7; i++) {
            if(!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    bool Settings::ValidValue(const SchemaEntry &e, const json &v) {
        switch(e.type) {
            case SType::Bool: return v.is_boolean();
            case SType::Enum: return v.is_string() && std::find(e.opts.begin(), e.opts.end(), v.get<std::string>()) != e.opts.end();
            case SType::Num: {
                if(!v.is_number()) return false;
                const double d = v.get<double>();
                return std::isfinite(d) && d >= e.min - 1e-9 && d <= e.max + 1e-9;
            }
            case SType::Color: return v.is_string() && IsHexColor(v.get<std::string>());
            case SType::Image: return v.is_null() || (v.is_string() && v.get<std::string>().size() < 512);
            case SType::Str: return v.is_string() && v.get<std::string>().size() < 200;
        }
        return false;
    }

    json Settings::Defaults() {
        json out = json::object();
        for(const auto &e: Schema()) {
            JSet(out, e.path, e.def);
        }
        json secs = json::array();
        for(const auto *id: SectionIds) {
            secs.push_back({ { "id", id }, { "visible", std::string(id) != "almost" } });
        }
        out["sections"] = secs;
        return out;
    }

    // Unbekannte Werte (z. B. von einer neueren oder älteren NEXUS-Version) bleiben erhalten,
    // damit ein Update oder ein Zurückwechseln keine Einstellungen löscht.
    static void KeepUnknown(const json &in, const std::string &prefix, json &out, int depth, int &count) {
        if(!in.is_object() || depth > 5) return;
        for(auto it = in.begin(); it != in.end(); ++it) {
            if(count >= 300) return;
            const std::string &k = it.key();
            if(k.empty() || k.size() > 64 || k.find('.') != std::string::npos) continue;
            const std::string path = prefix.empty() ? k : prefix + "." + k;
            if(path == "sections") continue;
            if(it->is_object()) { KeepUnknown(*it, path, out, depth + 1, count); continue; }
            if(Settings::Find(path)) continue;
            bool parentOfSchema = false;
            for(const auto &e: Schema()) if(e.path.rfind(path + ".", 0) == 0) { parentOfSchema = true; break; }
            if(parentOfSchema) continue;
            const bool ok = it->is_boolean() || it->is_null() || (it->is_number() && std::isfinite(it->get<double>())) || (it->is_string() && it->get<std::string>().size() <= 512);
            if(!ok) continue;
            JSet(out, path, *it);
            count++;
        }
    }

    json Settings::Sanitize(const json &in, std::vector<std::string> *report) {
        json out = json::object();
        int kept = 0;
        KeepUnknown(in, "", out, 0, kept);
        for(const auto &e: Schema()) {
            const json *p = JGet(in, e.path);
            json v = p ? *p : json();
            if(e.type == SType::Num && v.is_number()) {
                const double d = v.get<double>();
                if(std::isfinite(d)) v = std::clamp(d, e.min, e.max);
            }
            if(!ValidValue(e, v)) {
                if(p && report) report->push_back(e.path);
                v = e.def;
            }
            JSet(out, e.path, v);
        }
        // Bereiche: nur bekannte, keine Duplikate, fehlende anhängen, "settings" immer sichtbar
        json secs = json::array();
        std::vector<std::string> seen;
        const json *sp = JGet(in, "sections");
        if(sp && sp->is_array()) {
            for(const auto &x: *sp) {
                const auto id = JStr(x, "id", "");
                const bool known = std::find_if(std::begin(SectionIds), std::end(SectionIds), [&](const char *s) { return id == s; }) != std::end(SectionIds);
                if(known && std::find(seen.begin(), seen.end(), id) == seen.end()) {
                    seen.push_back(id);
                    secs.push_back({ { "id", id }, { "visible", id == "settings" ? true : JBool(x, "visible", true) } });
                }
            }
        }
        for(const auto *id: SectionIds) {
            if(std::find(seen.begin(), seen.end(), id) == seen.end()) {
                secs.push_back({ { "id", id }, { "visible", std::string(id) != "almost" } });
            }
        }
        out["sections"] = secs;
        return out;
    }

    const SchemaEntry *Settings::Find(const std::string &path) {
        for(const auto &e: Schema()) {
            if(e.path == path) return &e;
        }
        return nullptr;
    }

    std::string Settings::S(const std::string &path) const {
        const json *p = JGet(this->data, path);
        if(p && p->is_string()) return p->get<std::string>();
        const auto *e = Find(path);
        return (e && e->def.is_string()) ? e->def.get<std::string>() : std::string();
    }

    double Settings::N(const std::string &path) const {
        const json *p = JGet(this->data, path);
        if(p && p->is_number()) return p->get<double>();
        const auto *e = Find(path);
        return (e && e->def.is_number()) ? e->def.get<double>() : 0.0;
    }

    bool Settings::B(const std::string &path) const {
        const json *p = JGet(this->data, path);
        if(p && p->is_boolean()) return p->get<bool>();
        const auto *e = Find(path);
        return (e && e->def.is_boolean()) ? e->def.get<bool>() : false;
    }

    bool Settings::Set(const std::string &path, const json &v) {
        const auto *e = Find(path);
        if(!e || !ValidValue(*e, v)) return false;
        JSet(this->data, path, v);
        return true;
    }

    json Settings::StepValue(const SchemaEntry &e, const json &curIn, int dir) {
        const json cur = ValidValue(e, curIn) ? curIn : e.def;
        switch(e.type) {
            case SType::Bool: return json(!cur.get<bool>());
            case SType::Enum: {
                if(e.opts.empty()) return cur;
                auto it = std::find(e.opts.begin(), e.opts.end(), cur.get<std::string>());
                int i = it == e.opts.end() ? 0 : static_cast<int>(it - e.opts.begin());
                const int n = static_cast<int>(e.opts.size());
                i = ((i + dir) % n + n) % n;
                return json(e.opts[i]);
            }
            case SType::Num: {
                double v = cur.get<double>() + dir * e.step;
                v = std::round(v * 1000.0) / 1000.0;
                return json(std::clamp(v, e.min, e.max));
            }
            case SType::Color: {
                const auto c = cur.get<std::string>();
                int i = 0;
                for(int k = 0; k < 11; k++) {
                    if(c == Palette[k]) i = k;
                }
                i = ((i + dir) % 11 + 11) % 11;
                return json(std::string(Palette[i]));
            }
            default: return cur;
        }
    }

    void Settings::Step(const SchemaEntry &e, int dir) {
        const json *cur = JGet(this->data, e.path);
        this->Set(e.path, StepValue(e, cur ? *cur : e.def, dir));
    }

    std::string Settings::DisplayValue(const SchemaEntry &e, const json &vIn) {
        const json v = ValidValue(e, vIn) ? vIn : e.def;
        char buf[64];
        switch(e.type) {
            case SType::Bool: return v.get<bool>() ? "an" : "aus";
            case SType::Enum: {
                const auto s = v.get<std::string>();
                for(size_t i = 0; i < e.opts.size(); i++) {
                    if(e.opts[i] == s) return i < e.optLabels.size() ? e.optLabels[i] : s;
                }
                return s;
            }
            case SType::Num: {
                const double d = v.get<double>();
                if(e.unit == "%") std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(std::lround(d * 100)));
                else if(e.unit == "%raw") std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(std::lround(d)));
                else if(e.unit == "ms") std::snprintf(buf, sizeof(buf), "%d ms", static_cast<int>(std::lround(d)));
                else if(e.unit == "px") std::snprintf(buf, sizeof(buf), "%d px", static_cast<int>(std::lround(d)));
                else if(e.unit == "deg") std::snprintf(buf, sizeof(buf), "%d\u00b0", static_cast<int>(std::lround(d)));
                else if(e.unit == "unit") std::snprintf(buf, sizeof(buf), "%d px", static_cast<int>(std::lround(d * 24)));
                else if(e.unit == "x") std::snprintf(buf, sizeof(buf), "x%.2f", d);
                else if(e.unit == "h") std::snprintf(buf, sizeof(buf), "%d:00 Uhr", static_cast<int>(std::lround(d)));
                else if(e.unit == "s") std::snprintf(buf, sizeof(buf), "%d s", static_cast<int>(std::lround(d)));
                else if(e.step < 1) std::snprintf(buf, sizeof(buf), "%.2f", d);
                else std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(d)));
                return buf;
            }
            case SType::Image: return v.is_string() ? "Bild gesetzt" : "kein Bild";
            default: return v.is_string() ? v.get<std::string>() : std::string();
        }
    }

    std::string Settings::Display(const SchemaEntry &e) const {
        const json *cur = JGet(this->data, e.path);
        return DisplayValue(e, cur ? *cur : e.def);
    }

    std::vector<std::string> Settings::VisibleSections() const {
        std::vector<std::string> out;
        const json *sp = JGet(this->data, "sections");
        if(sp && sp->is_array()) {
            for(const auto &x: *sp) {
                if(JBool(x, "visible", true)) out.push_back(JStr(x, "id", ""));
            }
        }
        if(out.empty()) out.push_back("settings");
        return out;
    }

    json SanitizeTile(const json &t) {
        json out = json::object();
        if(!t.is_object()) return out;
        for(const auto &e: Schema()) {
            if(e.group != "tiles") continue;
            const auto key = e.path.substr(std::string("tileDefaults.").size());
            const auto it = t.find(key);
            if(it != t.end() && Settings::ValidValue(e, *it)) out[key] = *it;
        }
        if(JStr(t, "size", "") == "custom") out["size"] = "custom";
        if(JStr(t, "ratio", "") == "custom") out["ratio"] = "custom";
        const double ch = JNum(t, "customH", -1);
        if(ch > 0) out["customH"] = std::clamp(std::round(ch), 3.0, 16.0);
        const double cr = JNum(t, "customRatio", -1);
        if(cr > 0) out["customRatio"] = std::clamp(cr, 0.4, 2.5);
        return out;
    }

    json EffectiveTile(const Settings &s, const json &override_tile) {
        json out = json::object();
        const json *d = JGet(s.data, "tileDefaults");
        if(d && d->is_object()) out = *d;
        if(override_tile.is_object()) {
            const bool uniform = s.B("layout.uniform");
            for(auto it = override_tile.begin(); it != override_tile.end(); ++it) {
                if(uniform && (it.key() == "size" || it.key() == "ratio" || it.key() == "customH" || it.key() == "customRatio")) continue;
                out[it.key()] = it.value();
            }
        }
        return out;
    }

}
