
#pragma once
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <memory>

// NEXUS HOME: ersetzt die sichtbare uLaunch-Oberfläche (Profilauswahl, Sperrbildschirm, Hauptmenü).
// Der Unterbau von uLaunch (uSystem, SMI-Befehle, Spielstart/Fortsetzen, System-Applets) bleibt unverändert.
// Notausgang: L beim Öffnen des Menüs halten (diese Sitzung klassisch), ZL+ZR im klassischen Menü
// schaltet NEXUS wieder ein, nach zwei Fehlstarts automatisch klassisch.

namespace ul::menu::ui {

    bool IsNexusActive();
    void SetNexusClassicMode(const bool classic);

    class NexusElement;

    class NexusMenuLayout : public IMenuLayout {
        public:
            enum class Screen { Profile, Main, Lock };

        private:
            std::shared_ptr<NexusElement> elem;

        public:
            NexusMenuLayout();
            PU_SMART_CTOR(NexusMenuLayout)

            void SetScreen(const Screen s);

            void OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;
            bool OnHomeButtonPress() override;
            void LoadSfx() override {}
            void DisposeSfx() override {}
    };

}
