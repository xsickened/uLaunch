#include "sound.hpp"
#include "../core/store.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sys/stat.h>

namespace nexus {

    namespace {
        constexpr double Pi = 3.14159265358979323846;
        constexpr char SoundDir[] = "sdmc:/nexus/sounds/";

        std::string FindFile(const std::string &base) {
            for(const char *e: { ".wav", ".ogg", ".mp3" }) {
                const std::string p = std::string(SoundDir) + base + e;
                if(FileExists(p)) return p;
            }
            return "";
        }
    }

    bool Sound::Init(bool openAudio) {
        int f = 0, ch = 0;
        uint16_t fmt = 0;
        if(Mix_QuerySpec(&f, &fmt, &ch) == 0) {
            if(!openAudio) { Log("Sound: kein Audio geöffnet"); return false; }
            if(Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 1024) != 0) { Log("Sound: Mix_OpenAudio fehlgeschlagen"); return false; }
            this->ownAudio = true;
            if(Mix_QuerySpec(&f, &fmt, &ch) == 0) return false;
        }
        this->freq = f; this->format = fmt; this->channels = ch;
        this->ready = true;
        return true;
    }

    void Sound::Shutdown() {
        if(!this->ready) return;
        Mix_HaltMusic();
        if(this->music) { Mix_FreeMusic(this->music); this->music = nullptr; }
        for(auto &[k, s]: this->synth) if(s.chunk) Mix_FreeChunk(s.chunk);
        for(auto &[k, c]: this->files) if(c) Mix_FreeChunk(c);
        this->synth.clear();
        this->files.clear();
        if(this->ownAudio) Mix_CloseAudio();
        this->ready = false;
    }

    void Sound::Configure(bool en, const std::string &st, double vol, bool music, double musicVol) {
        this->enabled = en;
        this->style = st;
        this->volume = std::clamp(vol, 0.0, 1.0);
        if(music != this->bgm || std::fabs(musicVol - this->bgmVolume) > 1e-3) {
            this->bgm = music;
            this->bgmVolume = std::clamp(musicVol, 0.0, 1.0);
            if(this->ready) Mix_VolumeMusic(static_cast<int>(this->bgmVolume * 128));
            if(!music && this->ready && !this->playingMusic.empty()) { Mix_FadeOutMusic(400); this->playingMusic.clear(); }
        }
    }

    void Sound::Build(const std::string &key, const std::string &ev) {
        Synth s;
        // Tonfolge je Ereignis: Frequenzen (Hz), Dauer (s), Lautstärke
        std::vector<double> notes;
        double dur = 0.06, amp = 0.35;
        if(ev == "move") { notes = { 880 }; dur = 0.035; amp = 0.18; }
        else if(ev == "select") { notes = { 660, 990 }; dur = 0.09; }
        else if(ev == "back") { notes = { 560, 400 }; dur = 0.08; }
        else if(ev == "open") { notes = { 740, 880 }; dur = 0.07; amp = 0.25; }
        else if(ev == "launch") { notes = { 523, 659, 784, 1046 }; dur = 0.22; }
        else if(ev == "lock") { notes = { 500, 330 }; dur = 0.14; }
        else if(ev == "unlock") { notes = { 330, 500, 750 }; dur = 0.16; }
        else if(ev == "error") { notes = { 220, 180 }; dur = 0.16; amp = 0.3; }
        else { notes = { 1200 }; dur = 0.03; amp = 0.2; }   // toggle
        if(this->style == "glass") dur *= 2.2;
        const int n = std::max(1, static_cast<int>(dur * this->freq));
        s.pcm.resize(static_cast<size_t>(n) * this->channels);
        uint32_t noise = 12345;
        for(int i = 0; i < n; i++) {
            const double t = static_cast<double>(i) / this->freq, k = static_cast<double>(i) / n;
            const double f = notes[std::min(notes.size() - 1, static_cast<size_t>(k * notes.size()))];
            const double ph = 2 * Pi * f * t;
            double v;
            if(this->style == "retro") v = (std::sin(ph) >= 0 ? 0.6 : -0.6);
            else if(this->style == "click") { noise = noise * 1103515245 + 12345; v = 0.6 * std::sin(ph * 2) * std::exp(-k * 18) + 0.4 * ((noise >> 16 & 0x7fff) / 16384.0 - 1) * std::exp(-k * 40); }
            else if(this->style == "glass") v = 0.7 * std::sin(ph) + 0.3 * std::sin(ph * 2.76);
            else v = std::sin(ph);
            const double env = std::min(1.0, k * 40) * (this->style == "glass" ? std::exp(-k * 4) : std::exp(-k * 5));
            const auto smp = static_cast<int16_t>(std::clamp(v * env * amp, -1.0, 1.0) * 32000);
            for(int c = 0; c < this->channels; c++) s.pcm[static_cast<size_t>(i) * this->channels + c] = smp;
        }
        s.chunk = Mix_QuickLoad_RAW(reinterpret_cast<Uint8 *>(s.pcm.data()), static_cast<Uint32>(s.pcm.size() * sizeof(int16_t)));
        this->synth[key] = std::move(s);
        // Puffer wurde verschoben: Zeiger neu setzen
        auto &ref = this->synth[key];
        if(ref.chunk) ref.chunk->abuf = reinterpret_cast<Uint8 *>(ref.pcm.data());
    }

    Mix_Chunk *Sound::Chunk(const std::string &ev) {
        auto f = this->files.find(ev);
        if(f == this->files.end()) {
            const auto path = FindFile(ev);
            // Klänge liegen komplett im Speicher: zu große Dateien (z. B. ganze Lieder) nicht laden
            Mix_Chunk *c = nullptr;
            if(!path.empty()) {
                struct stat st;
                if(stat(path.c_str(), &st) == 0 && st.st_size > 0 && st.st_size <= 4 * 1024 * 1024) c = Mix_LoadWAV(path.c_str());
                else Log("Sound %s übersprungen (größer als 4 MB): %s", ev.c_str(), path.c_str());
            }
            f = this->files.emplace(ev, c).first;
        }
        if(f->second) return f->second;
        if(this->format != AUDIO_S16LSB) return nullptr;   // eingebaute Klänge nur im üblichen 16-Bit-Format
        const std::string key = this->style + ":" + ev;
        if(!this->synth.count(key)) this->Build(key, ev);
        return this->synth[key].chunk;
    }

    void Sound::Play(const std::string &ev) {
        if(!this->ready || !this->enabled || this->volume <= 0) return;
        Mix_Chunk *c = this->Chunk(ev);
        if(!c) return;
        Mix_VolumeChunk(c, static_cast<int>(this->volume * 128));
        Mix_PlayChannel(-1, c, 0);
    }

    void Sound::SetMusic(const std::string &which) {
        if(which != this->wantMusic) this->musicRetry = 0;
        this->wantMusic = which;
        this->musicCheck = 0;
    }

    void Sound::StartMusic() {
        if(this->music) { Mix_FreeMusic(this->music); this->music = nullptr; }
        this->playingMusic.clear();
        const auto path = FindFile(this->wantMusic == "lock" ? "lock" : "bgm");
        if(path.empty()) return;
        this->music = Mix_LoadMUS(path.c_str());
        // defekte oder nicht unterstützte Datei: nicht jede Sekunde neu versuchen
        if(!this->music) { this->musicRetry = 30; Log("Musik nicht abspielbar: %s", path.c_str()); return; }
        Mix_VolumeMusic(static_cast<int>(this->bgmVolume * 128));
        if(Mix_FadeInMusic(this->music, -1, 600) == 0) this->playingMusic = this->wantMusic;
        else { this->musicRetry = 30; Mix_FreeMusic(this->music); this->music = nullptr; }
    }

    void Sound::Tick(double dt) {
        if(!this->ready) return;
        this->musicCheck -= dt;
        if(this->musicCheck > 0) return;
        this->musicCheck = 1.0;
        if(this->musicRetry > 0) { this->musicRetry -= 1.0; return; }
        if(!this->bgm || this->wantMusic.empty()) return;
        // Sperrbildschirm ohne eigene lock-Datei spielt die Menü-Musik weiter
        const bool lockFile = !FindFile("lock").empty();
        const std::string effective = (this->wantMusic == "lock" && !lockFile) ? std::string("menu") : this->wantMusic;
        if(effective != this->playingMusic || !Mix_PlayingMusic()) {
            const auto keep = this->wantMusic;
            this->wantMusic = effective;
            this->StartMusic();
            this->wantMusic = keep;
        }
    }

}
