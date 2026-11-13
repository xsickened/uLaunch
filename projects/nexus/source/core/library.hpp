#pragma once
#include "settings.hpp"
#include <string>
#include <vector>

namespace nexus {

    // Titel-IDs werden als 16-stellige Hex-Strings gespeichert ("0100ABCD12340000"),
    // Ordner als "fld-xxxxxxxx". Löschen entfernt nie Software, nur NEXUS-Zuordnungen.
    struct Folder {
        std::string id;
        std::string name;
        std::string accent = "#3ee0c8";
        std::vector<std::string> items;
        json tile = json::object();
        std::string sort = "manual";
    };

    inline bool IsFolderId(const std::string &id) { return id.rfind("fld-", 0) == 0; }

    class Library {
        public:
            json apps = json::object();       // tid -> { favorite, tile, launched }
            std::vector<Folder> folders;
            std::vector<std::string> rootOrder;

            static Library FromJson(const json &j, std::vector<std::string> *report);
            json ToJson() const;

            // Gleicht mit installierten Titeln ab: Verweise auf fehlende Titel werden ausgeblendet,
            // Duplikate entfernt, neue Titel hinten angehängt. Anpassungen fehlender Titel bleiben gespeichert.
            void Reconcile(const std::vector<std::string> &installed, std::vector<std::string> *report);

            Folder *FindFolder(const std::string &id);
            Folder *FolderOf(const std::string &tid);
            bool IsFav(const std::string &tid) const;
            void ToggleFav(const std::string &tid);
            json &TileOf(const std::string &id);
            void AddToFolder(const std::string &tid, const std::string &fid);
            void RemoveFromFolder(const std::string &tid);
            void DeleteFolder(const std::string &fid);
            std::string CreateFolder(const std::string &name, int insertAt);
            void MarkLaunched(const std::string &tid, double when);
            double Launched(const std::string &tid) const;

        private:
            std::vector<std::string> installedCache;
    };

}
