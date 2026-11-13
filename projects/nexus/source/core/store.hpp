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
    constexpr const char ThemesDir[] = "sdmc:/nexus/themes";

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

    // Schlüssel (SteamGridDB, RetroAchievements) liegen zusätzlich in keys.json neben nexus.json.
    // Sie überleben damit auch ein Löschen/Zurücksetzen von nexus.json. Updates fassen beides nie an.
    void MergeSecrets(const std::string &dataPath, Settings &s);
    void SaveSecrets(const std::string &dataPath, const Settings &s);
    // Tägliche Sicherung: backup/nexus-JJJJ-MM-TT.json neben nexus.json, die neuesten 10 bleiben
    void BackupDaily(const std::string &dataPath, const std::string &date);

    // Eigene Themes: themes/<name>.json (nur Darstellung, ohne Schlüssel, RetroAchievements-Konto und Bibliothek)
    struct UserTheme { std::string name, path; };
    json ThemeSnapshot(const json &settings);
    bool SaveUserTheme(const std::string &dir, const std::string &name, const json &settings, std::string *outPath);
    std::vector<UserTheme> ListUserThemes(const std::string &dir);
    // Wendet ein Theme an: Darstellung aus dem Theme, Schlüssel/Konto/Sortierung bleiben
    bool ApplyUserTheme(const std::string &path, const json &current, json &result);

    void LogInit(const std::string &path);
    void Log(const char *fmt, ...);

}
