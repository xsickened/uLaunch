#include "platform.hpp"
#include <switch.h>

namespace nexus::plat {

    namespace {
        PadState g_Pad;
        bool g_Pl = false, g_Psm = false, g_Nifm = false;
        uint64_t g_RepeatAt = 0;
        uint32_t g_RepeatBtn = 0;
        bool g_Touching = false;
        float g_TouchStartX = 0, g_TouchStartY = 0, g_TouchLastX = 0, g_TouchLastY = 0;

        uint32_t Map(uint64_t b) {
            uint32_t o = 0;
            if(b & HidNpadButton_A) o |= BtnA;
            if(b & HidNpadButton_B) o |= BtnB;
            if(b & HidNpadButton_X) o |= BtnX;
            if(b & HidNpadButton_Y) o |= BtnY;
            if(b & HidNpadButton_L) o |= BtnL;
            if(b & HidNpadButton_R) o |= BtnR;
            if(b & HidNpadButton_ZL) o |= BtnZL;
            if(b & HidNpadButton_ZR) o |= BtnZR;
            if(b & HidNpadButton_Plus) o |= BtnPlus;
            if(b & HidNpadButton_Minus) o |= BtnMinus;
            if(b & HidNpadButton_AnyLeft) o |= BtnLeft;
            if(b & HidNpadButton_AnyRight) o |= BtnRight;
            if(b & HidNpadButton_AnyUp) o |= BtnUp;
            if(b & HidNpadButton_AnyDown) o |= BtnDown;
            return o;
        }
    }

    bool Init() {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&g_Pad);
        hidInitializeTouchScreen();
        g_Pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
        g_Psm = R_SUCCEEDED(psmInitialize());
        g_Nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        padUpdate(&g_Pad);
        return g_Pl;
    }

    void Exit() {
        if(g_Nifm) nifmExit();
        if(g_Psm) psmExit();
        if(g_Pl) plExit();
    }

    bool Loop() {
        return appletMainLoop();
    }

    bool HeldAtStart(uint32_t btn) {
        padUpdate(&g_Pad);
        return (Map(padGetButtons(&g_Pad)) & btn) != 0;
    }

    void PollInput(Input &in, uint64_t nowMs) {
        padUpdate(&g_Pad);
        const uint32_t down = Map(padGetButtonsDown(&g_Pad));
        const uint32_t held = Map(padGetButtons(&g_Pad));
        in.held = held;
        in.down = down;
        // Tastenwiederholung für Richtungen (380 ms Verzögerung, dann alle 110 ms)
        const uint32_t dirs = BtnLeft | BtnRight | BtnUp | BtnDown;
        if(down & dirs) {
            g_RepeatBtn = down & dirs;
            g_RepeatAt = nowMs + 380;
        }
        else if((held & g_RepeatBtn) && nowMs >= g_RepeatAt) {
            in.down |= (held & g_RepeatBtn);
            g_RepeatAt = nowMs + 110;
        }
        else if(!(held & g_RepeatBtn)) {
            g_RepeatBtn = 0;
        }
        // Touch: Tippen = Loslassen ohne nennenswerte Bewegung
        in.tap = false;
        HidTouchScreenState st = {};
        const bool touching = hidGetTouchScreenStates(&st, 1) > 0 && st.count > 0;
        if(touching) {
            const float x = static_cast<float>(st.touches[0].x), y = static_cast<float>(st.touches[0].y);
            if(!g_Touching) { g_TouchStartX = x; g_TouchStartY = y; }
            g_TouchLastX = x; g_TouchLastY = y;
        }
        else if(g_Touching) {
            const float dx = g_TouchLastX - g_TouchStartX, dy = g_TouchLastY - g_TouchStartY;
            if(dx * dx + dy * dy < 30 * 30) {
                in.tap = true;
                in.tapX = g_TouchLastX;
                in.tapY = g_TouchLastY;
            }
            else if(dx * dx > dy * dy) {
                in.down |= dx < 0 ? BtnRight : BtnLeft;   // Wischen nach links = weiter
            }
            else {
                in.down |= dy < 0 ? BtnDown : BtnUp;
            }
        }
        g_Touching = touching;
    }

    int BatteryPercent() {
        if(!g_Psm) return -1;
        u32 p = 0;
        return R_SUCCEEDED(psmGetBatteryChargePercentage(&p)) ? static_cast<int>(p) : -1;
    }

    bool BatteryCharging() {
        if(!g_Psm) return false;
        PsmChargerType t = PsmChargerType_Unconnected;
        return R_SUCCEEDED(psmGetChargerType(&t)) && t != PsmChargerType_Unconnected;
    }

    int WifiBars() {
        if(!g_Nifm) return -1;
        NifmInternetConnectionType type;
        u32 strength = 0;
        NifmInternetConnectionStatus status;
        if(R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status))) return -1;
        if(status != NifmInternetConnectionStatus_Connected) return -1;
        return static_cast<int>(strength > 3 ? 3 : strength);
    }

    bool IsAppletMode() {
        const AppletType t = appletGetAppletType();
        return t != AppletType_Application && t != AppletType_SystemApplication;
    }

    bool GetSharedFont(const void **data, size_t *size) {
        if(!g_Pl) return false;
        PlFontData font = {};
        if(R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) return false;
        *data = font.address;
        *size = font.size;
        return font.address != nullptr && font.size > 0;
    }

    uint32_t LaunchApplication(uint64_t tid) {
        const Result rc = appletRequestLaunchApplication(tid, nullptr);
        return R_SUCCEEDED(rc) ? 0 : static_cast<uint32_t>(rc);
    }

    bool Keyboard(const std::string &header, const std::string &initial, size_t maxLen, std::string &out) {
        SwkbdConfig kbd;
        if(R_FAILED(swkbdCreate(&kbd, 0))) return false;
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, header.c_str());
        swkbdConfigSetInitialText(&kbd, initial.c_str());
        swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(maxLen));
        char buf[256] = {};
        const Result rc = swkbdShow(&kbd, buf, sizeof(buf));
        swkbdClose(&kbd);
        if(R_FAILED(rc)) return false;
        out = buf;
        return true;
    }

}
