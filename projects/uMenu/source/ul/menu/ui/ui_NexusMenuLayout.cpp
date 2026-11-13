#include <ul/menu/ui/ui_NexusMenuLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_BackgroundScreenCapture.hpp>
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
// NEXUS (Quellcode in projects/nexus/source, gemeinsam mit der Test-App)
#include "ui/home.hpp"

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        int g_NexusState = -1;   // -1 = noch nicht entschieden, 0 = klassisch, 1 = NEXUS
        constexpr char ClassicFlagPath[] = "sdmc:/nexus/classic.flag";
        constexpr char UmenuBootPath[] = "sdmc:/nexus/umenu-boot.cnt";
        bool g_FirstProfileRequest = true;
        pu::ui::Color g_NexusFadeColor = { 11, 15, 23, 255 };
        // Start gilt als gelungen, sobald so viele Bilder fehlerfrei gezeichnet wurden
        constexpr int BootOkFrames = 20;

        void UpdateFadeColor(const nexus::Settings &s) {
            const auto c = nexus::ParseCol(s.S("theme.ui"));
            g_NexusFadeColor = { c.r, c.g, c.b, 255 };
        }

        std::string UidToHex(const AccountUid &uid) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%016lX%016lX", static_cast<unsigned long>(uid.uid[0]), static_cast<unsigned long>(uid.uid[1]));
            return buf;
        }

        u64 NowMs() {
            return armTicksToNs(armGetSystemTick()) / 1000000;
        }

        // Verbindung von NEXUS zum uLaunch-Unterbau
        class UmenuHost : public nexus::Host {
            public:
                nexus::ImageCache *images = nullptr;

                std::string UserIconKey(const AccountUid &uid) {
                    const auto key = "mem:user-" + UidToHex(uid);
                    if(this->images && !this->images->HasMemory(key)) {
                        u8 *data = nullptr;
                        size_t size = 0;
                        if(R_SUCCEEDED(acc::LoadAccountImage(uid, data, size)) && data && size > 0) {
                            this->images->ProvideMemory(key, std::vector<uint8_t>(data, data + size));
                        }
                        if(data) delete[] data;
                    }
                    return key;
                }

                uint32_t Launch(uint64_t tid) override {
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    const auto rc = smi::LaunchApplication(tid);
                    if(R_SUCCEEDED(rc)) {
                        // uSystem beendet uMenu jetzt selbst (wie im Original-uLaunch)
                        g_MenuApplication->Finalize();
                        return 0;
                    }
                    g_MenuApplication->FadeIn();
                    g_MenuApplication->ResetFade();
                    return rc;
                }

                bool HasSuspended() override {
                    return g_GlobalSettings.IsSuspended();
                }

                uint64_t SuspendedTid() override {
                    return g_GlobalSettings.IsTitleSuspended() ? static_cast<uint64_t>(g_GlobalSettings.system_status.suspended_app_id) : 0;
                }

                void Resume() override {
                    nexus::BootGuardOk(UmenuBootPath);
                    if(HasScreenCaptureBackground()) {
                        RequestResumeScreenCaptureBackground();   // blendet das Spielbild ein und setzt dann fort
                    }
                    else {
                        smi::ResumeApplication();
                    }
                }

                void CloseSuspended() override {
                    smi::TerminateApplication();
                    g_GlobalSettings.ResetSuspendedApplication();
                    RequestHideScreenCaptureBackground();
                }

                std::vector<nexus::UserInfo> Users() override {
                    std::vector<nexus::UserInfo> out;
                    std::vector<AccountUid> uids;
                    if(R_FAILED(acc::ListAccounts(uids))) return out;
                    for(const auto &uid: uids) {
                        nexus::UserInfo u;
                        u.id = UidToHex(uid);
                        if(R_FAILED(acc::GetAccountName(uid, u.name)) || u.name.empty()) u.name = "Benutzer";
                        u.iconKey = this->UserIconKey(uid);
                        out.push_back(u);
                    }
                    return out;
                }

                void SelectUser(const std::string &id) override {
                    std::vector<AccountUid> uids;
                    if(R_FAILED(acc::ListAccounts(uids))) return;
                    for(const auto &uid: uids) {
                        if(UidToHex(uid) == id) {
                            g_GlobalSettings.SetSelectedUser(uid);
                            g_MenuApplication->LoadMenu(MenuType::Main);
                            return;
                        }
                    }
                }

                void AddUser() override {
                    g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMyPage);
                    smi::OpenAddUser();
                    g_MenuApplication->Finalize();
                }

                void Logoff() override {
                    g_GlobalSettings.system_status.selected_user = {};
                    g_MenuApplication->LoadMenu(MenuType::Startup);
                }

                nexus::UserInfo CurrentUser() override {
                    nexus::UserInfo u;
                    const auto uid = g_GlobalSettings.system_status.selected_user;
                    if(!accountUidIsValid(&uid)) return u;
                    u.id = UidToHex(uid);
                    acc::GetAccountName(uid, u.name);
                    u.iconKey = this->UserIconKey(uid);
                    return u;
                }

                void OnUnlock() override {
                    if(accountUidIsValid(&g_GlobalSettings.system_status.selected_user)) g_MenuApplication->LoadMenu(MenuType::Main);
                    else g_MenuApplication->LoadMenu(MenuType::Startup);
                }

                std::vector<nexus::SysAction> SystemActions() override {
                    return {
                        { "Album", []() { ShowAlbum(); }, "" },
                        { "Controller", []() { ShowController(); }, "" },
                        { "Benutzerseite", []() { ShowUserPage(); }, "" },
                        { "Mii-Editor", []() { ShowMiiEdit(); }, "" },
                        { "Webbrowser", []() { ShowWebPage(); }, "" },
                        { "Homebrew-Menü", []() {
                            g_MenuApplication->FadeOutToNonLibraryApplet();
                            if(R_SUCCEEDED(smi::LaunchHomebrewLibraryApplet(ul::HbmenuPath, ul::HbmenuPath))) g_MenuApplication->Finalize();
                            else { g_MenuApplication->FadeIn(); g_MenuApplication->ResetFade(); }
                        }, "" },
                        { "Konsolen-Einstellungen (uLaunch)", []() { g_MenuApplication->LoadMenu(MenuType::Settings); }, "" },
                        { "Klassisches uLaunch-Menü", []() {
                            SetNexusClassicMode(true);
                            g_MenuApplication->LoadMenu(MenuType::Main);
                        }, "NEXUS wird ausgeschaltet. Zurück im klassischen Menü: ZL gedrückt halten und ZR drücken." },
                        { "Standby", []() { SleepSystem(); }, "" },
                        { "Neu starten", []() { RebootSystem(); }, "Die Konsole wird neu gestartet. Ein laufendes Spiel wird beendet." },
                        { "Ausschalten", []() { ShutdownSystem(); }, "Die Konsole wird ausgeschaltet. Ein laufendes Spiel wird beendet." }
                    };
                }

                void BeforeLeave() override {
                    nexus::BootGuardOk(UmenuBootPath);
                }

                bool CanLaunchHomebrewAsApp() override {
                    return g_GlobalSettings.cache_hb_takeover_app_id != os::InvalidApplicationId;
                }

                uint32_t LaunchHomebrew(const std::string &path, bool asApp) override {
                    nexus::BootGuardOk(UmenuBootPath);
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    const auto rc = (asApp && this->CanLaunchHomebrewAsApp()) ? smi::LaunchHomebrewApplication(path, path) : smi::LaunchHomebrewLibraryApplet(path, path);
                    if(R_SUCCEEDED(rc)) {
                        g_MenuApplication->Finalize();
                        return 0;
                    }
                    g_MenuApplication->FadeIn();
                    g_MenuApplication->ResetFade();
                    return rc;
                }
        };

    }

    bool IsNexusActive() {
        if(g_NexusState < 0) {
            nexus::EnsureDir(nexus::RootDir);
            nexus::LogInit(nexus::LogFile);
            bool active = !nexus::FileExists(ClassicFlagPath);
            if(active) {
                PadState pad;
                padInitializeDefault(&pad);
                padUpdate(&pad);
                if(padGetButtons(&pad) & HidNpadButton_L) {
                    active = false;
                    nexus::Log("uMenu: L gehalten -> klassisches Menü für diese Sitzung");
                }
            }
            if(active && nexus::BootGuardStart(UmenuBootPath)) {
                active = false;
                nexus::Log("uMenu: mehrere Fehlstarts -> klassisches Menü (ZL+ZR schaltet NEXUS wieder ein)");
            }
            g_NexusState = active ? 1 : 0;
            if(active) {
                std::string source;
                std::vector<std::string> report;
                UpdateFadeColor(nexus::LoadData(nexus::DataFile, source, report).settings);
            }
            nexus::Log("uMenu startet, NEXUS %s", active ? "aktiv" : "aus");
        }
        return g_NexusState == 1;
    }

    pu::ui::Color NexusFadeColor() {
        return g_NexusFadeColor;
    }

    bool NexusLockOnWake() {
        std::string source;
        std::vector<std::string> report;
        return nexus::LoadData(nexus::DataFile, source, report).settings.B("lock.enabled");
    }

    void SetNexusClassicMode(const bool classic) {
        if(classic) nexus::WriteFileAtomic(ClassicFlagPath, "1");
        else std::remove(ClassicFlagPath);
        nexus::BootGuardOk(UmenuBootPath);
        g_NexusState = classic ? 0 : 1;
        nexus::Log("uMenu: NEXUS %s", classic ? "ausgeschaltet (klassisch)" : "eingeschaltet");
    }

    // Vollbild-Element, das NEXUS in den Plutonium-Renderer von uMenu zeichnet (1920x1080, NEXUS rechnet in 1280x720)
    class NexusElement : public pu::ui::elm::Element {
        private:
            bool inited = false, busy = false, bootOk = false;
            int okFrames = 0, colorTick = 0;
            u64 keysDown = 0, keysHeld = 0;
            pu::ui::TouchPoint touch;
            u64 lastMs = 0, startMs = 0;
            nexus::Gfx gfx;
            nexus::ImageCache images;
            nexus::Apps apps;
            nexus::NexusData data;
            nexus::Home home;
            UmenuHost host;
            nexus::plat::Input in;
            nexus::Home::Screen pendingScreen = nexus::Home::Screen::Main;

            void Init(SDL_Renderer *r) {
                nexus::Log("NEXUS in uMenu wird initialisiert");
                nexus::EnsureDir(nexus::CacheDir);
                nexus::EnsureDir(nexus::ArtDir);
                nexus::plat::InitEmbedded();
                IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
                const void *font = nullptr;
                size_t fontSize = 0;
                nexus::plat::GetSharedFont(&font, &fontSize);
                this->gfx.Init(r, font, fontSize);
                // uMenu läuft als Library-Applet: wenig Speicher -> kleines Texturbudget
                // Texturbudget nach tatsächlich freiem Speicher: ein Viertel davon, zwischen 24 und 96 MB
                u64 total = 0, used = 0;
                size_t budget = 24u * 1024 * 1024;
                if(R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) && R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)) && total > used) {
                    budget = std::clamp<size_t>(static_cast<size_t>((total - used) / 3), 24u * 1024 * 1024, 128u * 1024 * 1024);
                }
                nexus::Log("Speicher: gesamt %lu MB, belegt %lu MB, Bildbudget %lu MB", static_cast<unsigned long>(total >> 20), static_cast<unsigned long>(used >> 20), static_cast<unsigned long>(budget >> 20));
                // uMenu zeichnet in 1920x1080: Bilder bis 1920 px, damit Vollbild-Hintergründe scharf bleiben
                this->images.Start(budget, budget >= 48u * 1024 * 1024 ? 1920 : 1280);
                this->host.images = &this->images;
                if(!this->apps.Start(&this->images)) nexus::Log("Titelliste nicht verfügbar: %s", this->apps.Error().c_str());
                std::string source;
                std::vector<std::string> report;
                this->data = nexus::LoadData(nexus::DataFile, source, report);
                nexus::Log("Konfiguration aus %s, %d Korrekturen", source.c_str(), static_cast<int>(report.size()));
                this->home.Init(r, &this->gfx, &this->images, &this->apps, &this->data, false, true, &this->host);
                this->home.SetScreen(this->pendingScreen);
                this->startMs = this->lastMs = NowMs();
                this->inited = true;
            }

        public:
            NexusElement() : Element() {}
            PU_SMART_CTOR(NexusElement)

            s32 GetX() override { return 0; }
            s32 GetY() override { return 0; }
            s32 GetWidth() override { return pu::ui::render::ScreenWidth; }
            s32 GetHeight() override { return pu::ui::render::ScreenHeight; }

            void SetScreen(const nexus::Home::Screen s) {
                this->pendingScreen = s;
                if(this->inited) this->home.SetScreen(s);
            }

            void OnHome() {
                if(this->inited) this->home.OnHome();
            }

            // Tastatur & Co. hier (Eingabe-Phase) statt mitten im Zeichnen öffnen
            void RunDeferred() {
                if(this->inited && !this->busy) this->home.RunDeferred();
            }

            void Feed(const u64 down, const u64 held, const pu::ui::TouchPoint tp) {
                this->keysDown |= down;
                this->keysHeld = held;
                this->touch = tp;
            }

            void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 x, const s32 y) override {
                SDL_Renderer *r = pu::ui::render::GetMainRenderer();
                if(!r) return;
                if(!this->inited) this->Init(r);
                float sx = 1, sy = 1;
                SDL_RenderGetScale(r, &sx, &sy);
                const float k = static_cast<float>(pu::ui::render::ScreenFactor);
                SDL_RenderSetScale(r, sx * k, sy * k);
                if(this->busy) {
                    // verschachtelte Render-Schleife (Überblendung/Dialog von uMenu): nur zeichnen
                    this->home.DrawOnly();
                }
                else {
                    this->busy = true;
                    const u64 now = NowMs();
                    const double dt = static_cast<double>(now - this->lastMs) / 1000.0;
                    this->lastMs = now;
                    this->images.Pump(r, 4);
                    const bool touching = !this->touch.IsEmpty();
                    nexus::plat::ProcessRaw(this->in, this->keysDown, this->keysHeld, touching,
                        touching ? static_cast<float>(this->touch.x) / k : 0, touching ? static_cast<float>(this->touch.y) / k : 0, now);
                    this->keysDown = 0;
                    this->home.Frame(this->in, dt);
                    if(!this->bootOk && ++this->okFrames >= BootOkFrames) {
                        nexus::BootGuardOk(UmenuBootPath);
                        this->bootOk = true;
                    }
                    if(++this->colorTick >= 30) { this->colorTick = 0; UpdateFadeColor(this->data.settings); }
                    this->busy = false;
                }
                SDL_RenderSetScale(r, sx, sy);
            }

            void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override {}
    };

    NexusMenuLayout::NexusMenuLayout() : IMenuLayout() {
        this->elem = NexusElement::New();
        this->Add(this->elem);
        // Bild des pausierten Spiels (für die Fortsetzen-Animation von uLaunch)
        this->Add(GetScreenCaptureBackground());
    }

    void NexusMenuLayout::SetScreen(const Screen s) {
        nexus::Home::Screen target = nexus::Home::Screen::Main;
        if(s == Screen::Profile) {
            // Beim ersten Mal nach dem Einschalten ggf. zuerst der NEXUS-Sperrbildschirm
            std::string source;
            std::vector<std::string> report;
            const bool lockFirst = g_FirstProfileRequest && nexus::LoadData(nexus::DataFile, source, report).settings.B("lock.enabled");
            g_FirstProfileRequest = false;
            target = lockFirst ? nexus::Home::Screen::Lock : nexus::Home::Screen::Profile;
        }
        else if(s == Screen::Lock) target = nexus::Home::Screen::Lock;
        this->elem->SetScreen(target);
    }

    void NexusMenuLayout::OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
        UpdateScreenCaptureBackground(false);
        this->elem->Feed(keys_down, keys_held, touch_pos);
        this->elem->RunDeferred();
    }

    bool NexusMenuLayout::OnHomeButtonPress() {
        this->elem->OnHome();
        return true;
    }

}
