#pragma once
#include "accent.hpp"
#include "settings.hpp"
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

// Partikel für die Kachel-Aura und Hintergrund-Effekte. Reine Simulation ohne Grafik,
// damit sie am PC getestet werden kann. Gezeichnet wird in ui/home (Gfx::Sprites).
namespace nexus {

    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, ay = 0;
        float life = 0, max = 1, size = 4, rot = 0, vr = 0;
        float opacity = 1, phase = 0;
        uint8_t r = 255, g = 255, b = 255;
        uint8_t layer = 0;     // 0 = Aura hinten, 1 = Aura vorne, 2 = Hintergrund hinten, 3 = Hintergrund vorne
        uint8_t motion = 0;    // 0 normal, 1 funkeln, 2 umherschwirren, 3 pendeln
        uint8_t fade = 0;      // 0 beides, 1 ausblenden, 2 schrumpfen, 3 keins
        uint8_t stretch = 0;   // 1 = in Flugrichtung gestreckt (Regen)
    };

    struct AuraCfg {
        bool enabled = false;
        std::string target = "focus", shape = "glow", colorMode = "accent", direction = "outward", emit = "edge", fadeMode = "both";
        bool additive = true, spin = true, front = false;
        Rgb color{ 255, 255, 255 };
        double spread = 25, density = 30, velocity = 60, size = 10, sizeVar = 0.4, lifetime = 1.4, opacity = 0.9, gravity = 0;
    };

    struct BgCfg {
        std::string type = "none", colorMode = "auto";
        bool additive = true, front = false;
        Rgb color{ 255, 255, 255 };
        double density = 24, speed = 1, size = 1, opacity = 0.7;
    };

    AuraCfg AuraFrom(const Settings &s);
    BgCfg BgFrom(const Settings &s);
    // Sprite-Form eines Hintergrund-Effekts (für eigene Bilder: "custom")
    std::string BgShape(const BgCfg &c);

    struct EmitRect {
        double cx, cy, w, h;
    };

    class ParticleSystem {
        public:
            static constexpr size_t MaxParticles = 700;
            std::vector<Particle> parts;
            using ColorFn = std::function<Rgb(double x, double y)>;

            ParticleSystem() : rng(12345) {}
            void Seed(uint32_t s) { this->rng.seed(s); }
            void SpawnAura(const AuraCfg &c, const std::vector<EmitRect> &rects, double dt, double time, const ColorFn &accent);
            void SpawnBg(const BgCfg &c, double dt, double time, double w, double h, const ColorFn &accent);
            void Update(double dt, double time);
            void Clear() { this->parts.clear(); this->auraAcc = this->bgAcc = 0; }
            // Aussehen zum Zeitpunkt t: Deckkraft 0..1 und Größe in Pixeln
            static void Look(const Particle &p, double time, float &alpha, float &size);

        private:
            std::mt19937 rng;
            double auraAcc = 0, bgAcc = 0;
            double Rand(double a, double b) { return std::uniform_real_distribution<double>(a, b)(this->rng); }
            void Push(const Particle &p);
    };

}
