#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Liest Name, Autor, Version und Icon direkt aus einer Homebrew-Datei (.nro: Header + ASET-Bereich).
namespace nexus {

    struct NroMeta {
        std::string path, id, name, author, version;
        std::vector<uint8_t> icon;   // JPEG
    };

    // 16-stellige ID ("ff" + 14 Hex) aus dem Pfad
    std::string HomebrewId(const std::string &path);
    bool ParseNro(const std::string &path, NroMeta &out, bool wantIcon);
    // .nro-Dateien unter root (eine Ordnerebene tief), höchstens max Stück
    std::vector<std::string> FindNros(const std::string &root, size_t max);

}
