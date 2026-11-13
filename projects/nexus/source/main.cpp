// NEXUS HOME – Test-App (NRO). Läuft neben uLaunch, verändert das System nicht und schreibt nur nach sdmc:/nexus/.
#include "core/store.hpp"
#include "nx/apps.hpp"
#include "nx/gfx.hpp"
#include "nx/images.hpp"
#include "nx/platform.hpp"
#include "ui/home.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

using namespace nexus;

int main(int, char **) {
    EnsureDir(RootDir);
    EnsureDir(CacheDir);
    EnsureDir(ArtDir);
    LogInit(LogFile);
    Log("NEXUS 0.1 startet");

    if(!plat::Init()) Log("Schriftdienst (pl) nicht verfügbar");
    const bool applet = plat::IsAppletMode();
    // Abgesicherter Modus: L beim Start gedrückt halten oder zwei nicht abgeschlossene Starts
    const bool holdL = plat::HeldAtStart(plat::BtnL);
    const bool guard = BootGuardStart(BootFile);
    const bool safeMode = holdL || guard;

    std::string source;
    std::vector<std::string> report;
    NexusData data = LoadData(DataFile, source, report);
    Log("Konfiguration aus %s, %d Korrekturen", source.c_str(), static_cast<int>(report.size()));
    if(safeMode) {
        const json *ra = JGet(data.settings.data, "ra");
        const json keepRa = ra ? *ra : json::object();
        data.settings.data = Settings::Defaults();
        data.settings.data["ra"] = keepRa;
        data.settings.data = Settings::Sanitize(data.settings.data, nullptr);
        Log("Abgesicherter Modus (L=%d, Wächter=%d)", holdL ? 1 : 0, guard ? 1 : 0);
    }

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        Log("SDL_Init fehlgeschlagen: %s", SDL_GetError());
        plat::Exit();
        return 1;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();
    SDL_Window *win = SDL_CreateWindow("NEXUS HOME", 0, 0, 1280, 720, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if(!ren) {
        Log("Renderer fehlgeschlagen: %s", SDL_GetError());
        if(win) SDL_DestroyWindow(win);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); plat::Exit();
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, 1280, 720);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    const void *font = nullptr;
    size_t fontSize = 0;
    plat::GetSharedFont(&font, &fontSize);
    Gfx gfx;
    gfx.Init(ren, font, fontSize);

    ImageCache images;
    // Im Applet-Modus deutlich weniger Texturspeicher und kleinere Bilder
    images.Start(applet ? 48u * 1024 * 1024 : 160u * 1024 * 1024, applet ? 960 : 1280);

    Apps apps;
    if(!apps.Start(&images)) Log("Titelliste nicht verfügbar: %s", apps.Error().c_str());

    Home home;
    home.Init(ren, &gfx, &images, &apps, &data, safeMode, applet);

    uint64_t last = SDL_GetTicks64();
    uint64_t started = last;
    bool bootOk = false;
    plat::Input in;
    while(plat::Loop()) {
        const uint64_t now = SDL_GetTicks64();
        const double dt = static_cast<double>(now - last) / 1000.0;
        last = now;
        plat::PollInput(in, now);
        images.Pump(ren, 6);
        if(!home.Frame(in, dt)) break;
        SDL_RenderPresent(ren);
        if(!bootOk && now - started > 3000) {
            BootGuardOk(BootFile);   // Start erfolgreich
            bootOk = true;
        }
    }

    home.Save();
    if(!bootOk) BootGuardOk(BootFile);   // sauber beendet zählt ebenfalls als erfolgreich
    Log("NEXUS beendet");
    apps.Stop();
    images.Stop();
    gfx.Exit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    plat::Exit();
    return 0;
}
