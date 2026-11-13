#pragma once
#include "../core/layout.hpp"
#include "../core/store.hpp"
#include "../nx/apps.hpp"
#include "../nx/gfx.hpp"
#include "../nx/images.hpp"
#include "../nx/platform.hpp"
#include <array>
#include <functional>
#include <memory>
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
    };

    struct Dialog {
        std::string title, sub;
        std::function<std::vector<Row>()> rows;
        int sel = 0;
        double scroll = 0;
        double x = 0, y = 0, w = 420, h = 0;    // h = 0: automatisch
        bool shade = false, settings = false, wide = false;
        std::vector<std::pair<std::string, std::string>> hints;
        std::function<void()> onB, onX, onY, onClose;
    };

    struct Disp {
        double x = 0, y = 0, w = 0, h = 0, rot = 0, squash = 1, sc = 1, op = 0;
        double vx = 0, vy = 0, vw = 0, vh = 0, vrot = 0, vsq = 0, vsc = 0, vop = 0;
        double delay = 0;
        bool init = false;
    };

    class Home {
        public:
            bool Init(SDL_Renderer *r, Gfx *g, ImageCache *img, Apps *apps, NexusData *data, bool safeMode, bool applet);
            // false = Beenden gewünscht
            bool Frame(const plat::Input &in, double dt);
            void Save();

        private:
            SDL_Renderer *r = nullptr;
            Gfx *g = nullptr;
            ImageCache *img = nullptr;
            Apps *apps = nullptr;
            NexusData *d = nullptr;
            bool applet = false;

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
            void ApplyTheme(int idx);
            void CrashReset(const std::string &what);

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
            Col UiCol() const;
            bool Light() const;
            double DialogX(double w) const;
    };

}
