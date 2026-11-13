#pragma once
#include "settings.hpp"
#include <string>

// Theme-Pakete (.nxtheme = ZIP mit theme.json und den eigenen Bildern). Zum Teilen: Datei nach
// sdmc:/nexus/themes/ kopieren. Beim Anwenden werden die Bilder nach art/themes/<name>/ entpackt.
namespace nexus {

    bool ExportThemePack(const std::string &file, const std::string &name, const json &settings, std::string &err);
    bool ReadThemePackName(const std::string &file, std::string &name);
    bool ApplyThemePack(const std::string &file, const std::string &artDir, const json &current, json &result, std::string &err);

}
