#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nexus {

    struct UserInfo {
        std::string id;        // Hex der Account-UID
        std::string name;
        std::string iconKey;   // Schlüssel im ImageCache ("mem:user-...")
    };

    struct SysAction {
        std::string label;
        std::function<void()> act;
        std::string confirm;   // nicht leer: vorher nachfragen (Text)
        std::string id;        // feste Kennung für die Systemleiste (album, controller, sleep, …)
    };

    // Schnittstelle zum Unterbau. In der Test-App (NRO) gibt es keinen Host.
    // In uMenu (uLaunch) stellt der Host Spielstart, Fortsetzen, Profile und Systemfunktionen bereit.
    class Host {
        public:
            virtual ~Host() = default;
            virtual uint32_t Launch(uint64_t tid) = 0;          // 0 = ok (uSystem beendet danach uMenu)
            virtual bool HasSuspended() = 0;
            virtual uint64_t SuspendedTid() = 0;                // 0 wenn keine Anwendung (oder Homebrew) pausiert
            virtual void Resume() = 0;
            virtual void CloseSuspended() = 0;
            virtual std::vector<UserInfo> Users() = 0;
            virtual void SelectUser(const std::string &id) = 0;
            virtual void AddUser() = 0;
            virtual void Logoff() = 0;
            virtual UserInfo CurrentUser() = 0;
            virtual void OnUnlock() = 0;
            virtual std::vector<SysAction> SystemActions() = 0;
            virtual void BeforeLeave() = 0;                       // vor jedem Verlassen der Oberfläche (Startwächter zurücksetzen)
            // Homebrew (.nro) über uLaunch starten; asApp = als Anwendung (voller Speicher, sofern eingerichtet)
            virtual uint32_t LaunchHomebrew(const std::string &path, bool asApp) = 0;
            virtual bool CanLaunchHomebrewAsApp() = 0;
    };

}
