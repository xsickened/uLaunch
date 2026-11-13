#pragma once
#include "schema.gen.hpp"
#include <string>
#include <vector>

namespace nexus {

    // Sicheres Lesen/Schreiben in verschachtelten JSON-Objekten ("a.b.c"). Wirft nie.
    const json *JGet(const json &o, const std::string &path);
    void JSet(json &o, const std::string &path, const json &v);
    std::string JStr(const json &o, const char *key, const std::string &def);
    double JNum(const json &o, const char *key, double def);
    bool JBool(const json &o, const char *key, bool def);

    extern const char *const SectionIds[8];

    class Settings {
        public:
            json data;

            static bool ValidValue(const SchemaEntry &e, const json &v);
            // Prüft jeden Wert gegen das Schema; ungültig/fehlend -> Standard, Zahlen werden begrenzt.
            static json Sanitize(const json &in, std::vector<std::string> *report);
            static json Defaults();
            static const SchemaEntry *Find(const std::string &path);

            std::string S(const std::string &path) const;
            double N(const std::string &path) const;
            bool B(const std::string &path) const;
            // Setzt einen Wert nur, wenn er laut Schema gültig ist.
            bool Set(const std::string &path, const json &v);
            // Wert nach Schema schrittweise verändern (Enum zyklisch, Zahl um step, Bool umschalten, Farbe aus Palette)
            void Step(const SchemaEntry &e, int dir);
            std::string Display(const SchemaEntry &e) const;
            // Dieselbe Logik für einzelne Werte (z. B. Kachel-Editor mit eigenem Entwurf)
            static json StepValue(const SchemaEntry &e, const json &cur, int dir);
            static std::string DisplayValue(const SchemaEntry &e, const json &v);

            // Bereiche (Reihenfolge + Sichtbarkeit)
            std::vector<std::string> VisibleSections() const;
    };

    // Kachelwerte: Standard aus settings.tileDefaults, überlagert durch gültige Einzelwerte
    json SanitizeTile(const json &t);
    json EffectiveTile(const Settings &s, const json &override_tile);

    extern const char *const Palette[11];
}
