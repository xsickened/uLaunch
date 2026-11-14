#pragma once
#include "../core/accent.hpp"
#include "../core/layout.hpp"
#include "../core/particles.hpp"
#include "../core/store.hpp"
#include "../nx/anim.hpp"
#include "../nx/apps.hpp"
#include "../nx/homebrew.hpp"
#include "../nx/sound.hpp"
#include "../nx/gfx.hpp"
#include "../nx/images.hpp"
#include "../nx/platform.hpp"
#include "host.hpp"
#include "online.hpp"
#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus {

    struct Entry {
        std::string id;
        bool folder = false;
        std::string name;
        std::string iconKey;
        std::string accent;
        std::vector<std::string> folderIcons;   // bis zu 4 Icons für die Ordnerkachel
        int folderCount = 0;
    };

    struct ArtPaths {
        std::string grid, hero, logo;
    };

    struct Row {
        std::string label;
        std::function<std::string()> value;      // leer = Aktionszeile
        std::function<void(int)> adjust;        // ←/→
        std::function<void()> activate;         // A
        bool head = false;
        std::string thumb;                      // Bildvorschau (Dialog mit preview)
        bool dark = false;                      // Vorschau auf dunklem Karo (transparente Logos)
    };

    struct Dialog {
        std::string title, sub;
        std::function<std::vector<Row>()> rows;
        int sel = 0;
        double scroll = 0;
        double x = 0, y = 0, w = 420, h = 0;    // h = 0: automatisch
        bool shade = false, settings = false, wide = false;
        bool preview = false;                   // rechts großes Vorschaubild der Auswahl
        std::function<void(int)> onSelect;      // pro Frame mit der aktuellen Auswahl (Vorladen)
        std::vector<std::pair<std::string, std::string>> hints;
        std::function<void()> onB, onX, onY, onClose;
    };

    struct Disp {
        double x = 0, y = 0, w = 0, h = 0, rot = 0, squash = 1, sc = 1, op = 0;
        double vx = 0, vy = 0, vw = 0, vh = 0, vrot = 0, vsq = 0, vsc = 0, vop = 0;
        double delay = 0;
        double caseA = 0;    // aktuelle Drehung der 3D-Hülle (Grad, mit Vorzeichen)
        double caseV = 0;    // Drehgeschwindigkeit (Feder)
        double lean = 0, leanV = 0;   // Neigung durch den Schwung beim Scrollen (Grad)
        double lift = 0, liftV = 0;   // gewählte Hülle kommt nach vorn (Tiefe in px)
        bool caseInit = false;
        bool init = false;
    };

    class Home {
        public:
            enum class Screen { Main, Profile, Lock };

            bool Init(SDL_Renderer *r, Gfx *g, ImageCache *img, Apps *apps, NexusData *data, bool safeMode, bool applet, Host *host = nullptr);
            // false = Beenden gewünscht (nur Test-App)
            bool Frame(const plat::Input &in, double dt);
            void Save();
            void SetScreen(Screen s);
            Screen GetScreen() const { return this->screen; }
            void DrawOnly();        // nur zeichnen (verschachtelte Render-Schleifen von uMenu)
            void OnHome();          // HOME-Taste (über uSystem gemeldet)
            // Tastatur & Co. außerhalb des Zeichnens ausführen (uMenu ruft das aus der Eingabe-Phase auf)
            void RunDeferred();
            void Shutdown();

        private:
            SDL_Renderer *r = nullptr;
            Gfx *g = nullptr;
            ImageCache *img = nullptr;
            Apps *apps = nullptr;
            NexusData *d = nullptr;
            bool applet = false;
            Host *host = nullptr;
            Screen screen = Screen::Main;
            std::vector<UserInfo> users;
            UserInfo currentUser;
            int userFocus = 0;
            double profileT = 0;
            bool pendingAutoSelect = false;
            bool exitRequested = false;   // nach erfolgreicher Startanfrage sofort sauber beenden
            std::function<void()> deferred;
            Online online;
            bool autoArtQueued = false;
            AccentParams accentP;
            // Bildausschnitt-Editor (Zoom, Schnitt)
            // Partikel (Kachel-Aura, Hintergrund-Effekte)
            ParticleSystem fx;
            std::vector<EmitRect> auraRects;
            AuraCfg auraC;
            BgCfg bgC;
            // Bildschirmschoner
            double idleT = 0, slideT = 0;
            bool idleActive = false, idleLocked = false;
            int slideIdx = -1;
            std::string slideKey, slidePrev, slideId;
            bool startApplied = false;
            struct LogoMove { bool active = false; json backup; } logoMove;
            struct CropEdit { bool active = false; std::string path, title; double aspect = 16.0 / 9.0; Crop c, backup; std::function<void()> done; } crop;

            // Zustand
            std::string section = "start";
            std::string openFolder;
            int focus = 0;
            double scroll = 0;
            std::vector<Entry> list;
            LResult lay;
            std::vector<LItem> litems;
            bool listDirty = true, layoutDirty = true, entryAnim = true;
            std::unordered_map<std::string, Disp> disp;
            std::unordered_map<std::string, AppInfo> appMap;
            std::vector<std::string> installed;
            int appsVersion = -1;
            std::unordered_map<std::string, ArtPaths> art;
            double time = 0;
            double frameDt = 1.0 / 60.0;
            int crashCount = 0;
            bool fatal = false;
            std::string fatalMsg;

            // Bewegungsmodus (Position ändern)
            bool moving = false;
            std::string moveId;
            std::vector<std::string> moveBackup;

            // Kachel-Editor
            std::string editId;
            json editBackup;

            // Fokus-Präsentation (Hero + Logo)
            struct HeroSlot { std::string key; bool icon = false; std::string folderAccent; double alpha = 0; };
            HeroSlot heroA, heroB;     // A = aktuell, B = vorher (blendet aus)
            std::string heroFor, logoKeyFor;
            double heroTimer = 0;
            std::string pendingHeroId;
            double logoAlpha = 0, logoSlide = 0;
            std::string logoId;
            std::string lastHeroKey;

            // Touch-Trefferflächen (werden beim Zeichnen gefüllt)
            struct Hit { double x, y, w, h; std::string id; int idx; };
            std::vector<Hit> tabHits, rowHits;
            Hit panelHit{ 0, 0, 0, 0, "", -1 };
            double wobbleT = 1;

            // Sperrbildschirm
            bool locked = false, unlocking = false;
            double unlockT = 0;

            // Dialoge, Hinweise
            std::vector<std::shared_ptr<Dialog>> dialogs;
            std::string toastText;
            double toastT = 0;
            bool dirtySave = false;
            double saveTimer = 0;

            // Aufbau
            void SyncApps();
            void BuildList();
            const ArtPaths &Art(const std::string &id);
            json TileOverride(const std::string &id);
            json Tile(const std::string &id);
            void Relayout();
            void SetFocus(int i);
            void SwitchSection(const std::string &s);
            void CycleSection(int dir);
            std::vector<std::string> *ManualOrder();

            // Eingabe
            void HandleInput(const plat::Input &in);
            void Press(uint32_t b);
            void Tap(float x, float y);
            void Activate();
            void Toast(const std::string &t);
            void MarkDirty();

            // Dialoge
            void Push(std::shared_ptr<Dialog> dl);
            void Pop();
            void CloseAll();
            void OpenContext();
            void OpenEditor(const std::string &id);
            void OpenFolderPicker(const std::vector<std::string> &ids);
            void OpenSettings(const std::string &group = "");
            void OpenGroup(const std::string &group);
            void OpenArtInfo(const std::string &id);
            void Confirm(const std::string &title, const std::string &sub, const std::string &yes, std::function<void()> act);
            void CreateFolderFlow(const std::vector<std::string> &ids);
            bool SettingVisible(const std::string &path) const;
            void AskText(const std::string &header, const std::string &initial, size_t maxLen, std::function<void(std::string)> done);
            // Bildauswahl: Quellen (Beschriftung, Ordner), optional Album, Ergebnis "" = kein Bild / Standard
            void OpenImagePicker(const std::string &title, const std::vector<std::pair<std::string, std::string>> &dirs, bool album, bool dark, std::function<void(const std::string &)> onPick, const std::string &noneLabel);
            void OpenArtKind(const std::string &id, const std::string &kind);
            void OpenSgdbFlow(const std::string &id, const std::string &name, const std::string &term);
            void OpenSgdbKinds(const std::string &id, const SgdbGame &game);
            void OpenSgdbImages(const std::string &id, const SgdbGame &game, const std::string &kind, const std::string &key);
            void OpenSgdbKeyMissing();
            void OpenRaDialog(const std::string &id, const std::string &name);
            void OpenRaAssign(const std::string &id);
            void OpenRaSearch(const std::string &id, const std::string &term);
            // Profilbild: eigenes Bild (mit Ausschnitt) vor dem Bild der Konsole
            SDL_Texture *UserTex(const UserInfo &u, Crop &crop);
            void DrawUserPic(const UserInfo &u, const Xf &xf, double rad, double alpha);
            void OpenAvatarPicker(const std::string &uid, const std::string &name);
            // Vorderseite der gewählten 3D-Hülle (für die Hervorhebung), gültig wenn caseQuadTime == time
            SDL_FPoint caseQuad[4] {};
            double caseQuadTime = -1;
            void DrawHighlight(const Xf &fx, double rad, bool hex);
            void ImportKeys(bool quiet);
            void OpenCropEditor(const std::string &path, const std::string &title, double aspect, std::function<void()> done = nullptr);
            void CropInput(const plat::Input &in);
            void DrawCropEditor();
            double TileAspect() const;
            void OpenThemes();
            void StartLogoMove();
            void DrawParticles(int layer);
            void DrawIdle();
            void UpdateIdle(double dt, bool input);
            void ApplyStartSection();
            void SetArt(const std::string &id, const std::string &kind, const std::string &path);
            std::string DisplayName(const std::string &id);
            void ForgetFile(const std::string &path);
            void PrefetchAround();
            bool gcDone = false;
            // Homebrew, Sounds, GIF, Suche, Mehrfachauswahl, Spielzeit, Spielfarbe, Auto-Theme, Profile
            Homebrew hbScan;
            std::unordered_map<std::string, HbInfo> hbMap;
            int hbVersion = -1;
            Sound snd;
            AnimBg anim;
            std::string searchQuery;
            bool multi = false;
            std::set<std::string> multiSel;
            struct PlayCache { bool ok = false; plat::PlayInfo info; };
            std::unordered_map<std::string, PlayCache> play;
            double gameR = -1, gameG = 0, gameB = 0;
            double autoCheck = 0;
            std::string lookUser;
            uint32_t heldNow = 0;
            void OpenSearch();
            void OpenMultiMenu();
            void QuickLaunch();
            void LaunchHb(const HbInfo &h);
            void OpenStickers(const std::string &id);
            void DrawStickers(const Entry &e, const Xf &xf, double w, double h, double op);
            const PlayCache &Play(const std::string &id);
            double DrawPlaytime(const Entry &e, double x, double y, double w, double alpha);
            void UpdateGameColor(double dt);
            void UpdateAutoTheme(double dt);
            void SwitchUserLook(const std::string &newUser);
            void Sfx(const char *ev);
            // 3D-Hüllen, Startanimation, Zoom
            void DrawCase(const std::string &style, const json &t, SDL_Texture *tex, Xf &xf, const Disp &ds, double op, double rad, bool cover, double posX, double posY, double zoom, const std::string &title, bool focused);
            struct LaunchFx { bool active = false; double t = 0; std::string id, name; std::function<void()> go; Xf from; bool haveFrom = false; } launchFx;
            Xf lastFocusXf;
            bool haveLastFocus = false;
            void StartLaunchFx(const std::string &id, const std::string &name, std::function<void()> go);
            void DrawLaunchFx();
            void RealLaunch(uint64_t tid, const std::string &id, const std::string &name);
            double zoomTarget = -1;
            bool zoomHeld = false;
            uint32_t ZoomBit() const;
            void CycleZoom();
            void UpdateZoom(double dt);
            std::vector<UserTheme> AllThemes();
            void OpenThemePicker(const std::string &key);
            bool ApplyThemeFile(const std::string &path, const std::string &name);
            void LogoMoveInput(const plat::Input &in);
            void QueueMissingArt(bool force);
            void ArtChanged(const std::string &id);
            std::vector<Row> SgdbRows();
            std::vector<Row> RaRows();
            bool DrawBgImage(const std::string &path, double oy, double alpha);
            void DrawRaBadge(const Entry &e, const Xf &xf, double w, double h, double op);
            void DrawRaUnderLogo(const Entry &e, double x, double y, double w, double alpha);
            void ApplyTheme(int idx);
            void CrashReset(const std::string &what);
            bool FrameInner(const plat::Input &in, double dt);
            void DoLaunch(uint64_t tid, const std::string &id, const std::string &name);
            void OpenQuickMenu();
            void ProfilePress(uint32_t b);
            void DrawProfile();
            std::vector<UserInfo> ProfileItems() const;

            // Zeichnen
            void Update(double dt);
            void Draw();
            void DrawBackground();
            void DrawHero(double dt);
            void DrawLogo(double dt);
            void DrawTiles();
            void DrawTopbar();
            void DrawHints();
            void DrawDialogs();
            void DrawLock();
            void DrawToast();
            void DrawFatal();
            LayoutEnv Env() const;
            Col TextCol() const;
            Col MutedCol() const;
            Col Accent() const;
            Col AccentAtU(double u) const;
            ColFn AccentFn(double x, double y, double w, double h) const;
            Col UiCol() const;
            bool Light() const;
            double DialogX(double w) const;
    };

}
