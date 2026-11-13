#pragma once
#include "library.hpp"
#include <string>

namespace nexus {

    constexpr const char RootDir[] = "sdmc:/nexus";
    constexpr const char DataFile[] = "sdmc:/nexus/nexus.json";
    constexpr const char BootFile[] = "sdmc:/nexus/boot.cnt";
    constexpr const char LogFile[] = "sdmc:/nexus/log.txt";
    constexpr const char ArtDir[] = "sdmc:/nexus/art";
    constexpr const char CacheDir[] = "sdmc:/nexus/cache";

    // Ersetzt ungültige UTF-8-Folgen durch '?'. Wichtig: nlohmann::json bricht beim Speichern
    // ungültiger Zeichenketten sonst mit einer Ausnahme ab (ohne Ausnahmen: Programmabbruch).
    std::string SanitizeUtf8(const std::string &in);
    // Sicheres Serialisieren (ersetzt ungültige Zeichen statt abzubrechen)
    std::string SafeDump(const json &j, int indent = -1);

    bool ReadFile(const std::string &path, std::string &out, size_t maxBytes = 8 * 1024 * 1024);
    // tmp schreiben -> alte Datei nach .bak -> tmp umbenennen. Laden prüft Haupt-, tmp- und bak-Datei.
    bool WriteFileAtomic(const std::string &path, const std::string &content);
    bool FileExists(const std::string &path);
    void EnsureDir(const std::string &path);

    struct NexusData {
        Settings settings;
        Library library;
    };

    // Lädt robust: Hauptdatei, sonst tmp, sonst Backup, sonst Standard. Meldet, woher geladen wurde.
    NexusData LoadData(const std::string &path, std::string &source, std::vector<std::string> &report);
    bool SaveData(const std::string &path, const NexusData &d);

    // Start-Wächter: zählt Starts; nach 2 nicht abgeschlossenen Starts -> abgesicherter Modus
    bool BootGuardStart(const std::string &path);
    void BootGuardOk(const std::string &path);

    void LogInit(const std::string &path);
    void Log(const char *fmt, ...);

}
