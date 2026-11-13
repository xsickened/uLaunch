#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace nexus::plat {

    enum Btn : uint32_t {
        BtnA = 1u << 0, BtnB = 1u << 1, BtnX = 1u << 2, BtnY = 1u << 3,
        BtnL = 1u << 4, BtnR = 1u << 5, BtnZL = 1u << 6, BtnZR = 1u << 7,
        BtnPlus = 1u << 8, BtnMinus = 1u << 9,
        BtnLeft = 1u << 10, BtnRight = 1u << 11, BtnUp = 1u << 12, BtnDown = 1u << 13
    };

    struct Input {
        uint32_t down = 0;   // neu gedrückt (inkl. Tastenwiederholung für Richtungen)
        uint32_t held = 0;
        bool tap = false;    // Touch losgelassen ohne Wischen
        float tapX = 0, tapY = 0;
    };

    bool Init();
    // In uMenu: Eingabe kommt von Plutonium, nur Schrift/Akku/WLAN-Dienste öffnen
    bool InitEmbedded();
    // Gemeinsame Eingabeaufbereitung (Tastenwiederholung, Tippen/Wischen); Rohwerte = HidNpadButton-Bits, Touch in 1280x720
    void ProcessRaw(Input &in, uint64_t hidDown, uint64_t hidHeld, bool touching, float tx, float ty, uint64_t nowMs);
    void Exit();
    bool Loop();                          // appletMainLoop
    void PollInput(Input &in, uint64_t nowMs);
    bool HeldAtStart(uint32_t btn);
    int BatteryPercent();                 // -1 wenn unbekannt
    bool BatteryCharging();
    int WifiBars();                       // -1 kein Netz, 0..3
    bool IsAppletMode();
    bool GetSharedFont(const void **data, size_t *size);
    // Startet einen Titel. Gibt Fehlercode zurück (0 = ok). In der Testapp ggf. nicht erlaubt.
    uint32_t LaunchApplication(uint64_t tid);
    // Bildschirmtastatur. false, wenn nicht verfügbar (z. B. Applet-Modus) oder abgebrochen.
    bool Keyboard(const std::string &header, const std::string &initial, size_t maxLen, std::string &out);
    // Spielzeit aus der Systemstatistik (pdm). userHex = 32-stellige Benutzer-ID oder leer (alle Benutzer)
    struct PlayInfo { uint64_t minutes = 0; uint32_t launches = 0; uint64_t lastPlayed = 0; };
    bool PlayStats(uint64_t tid, const std::string &userHex, PlayInfo &out);

}
