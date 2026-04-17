/// @file examples/windows/main.cpp
/// @brief Windows console keyboard sampler — demonstrates campello_audio on Windows.

#include <campello_audio/audio_bus.hpp>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/echo_filter.hpp>
#include <campello_audio/high_pass_filter.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <campello_audio/tone_source.hpp>

#include <algorithm>
#include <conio.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <windows.h>

namespace audio = systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// WindowsSampler
// ---------------------------------------------------------------------------

class WindowsSampler {
public:
    bool initialize() {
        audio::AudioEngineDescriptor desc;
        desc.sampleRate = 44100;
        desc.bufferSize = 512;
        desc.channels   = 2;

        if (!engine_.init(desc)) {
            std::cerr << "AudioEngine::init() failed.\n";
            return false;
        }

        engine_.play(sfxBus_);
        engine_.play(musicBus_);
        engine_.setSidechain(&sfxBus_, &musicBus_, -10.0f, 0.03f, 0.35f);
        sidechainEnabled_ = true;

        return true;
    }

    void shutdown() {
        engine_.deinit();
    }

    void run() {
        printHelp();

        bool running = true;
        while (running) {
            if (_kbhit()) {
                int ch = _getch();

                // Extended keys (arrows, etc.) — 0 or 0xE0 prefix
                if (ch == 0 || ch == 224) {
                    ch = _getch();
                    switch (ch) {
                        case 75: move3D(-0.5f, 0.0f); break; // left
                        case 77: move3D(0.5f, 0.0f);  break; // right
                        case 80: move3D(0.0f, 0.5f);  break; // down
                        case 72: move3D(0.0f, -0.5f); break; // up
                    }
                    printStatus();
                    continue;
                }

                switch (ch) {
                    case 'q': case 'Q': running = false; break;
                    case '1': playTone(0); break;
                    case '2': playTone(1); break;
                    case '3': playTone(2); break;
                    case '4': playTone(3); break;
                    case '5': playTone(4); break;
                    case 'm': case 'M': toggleMusic(); break;
                    case 'l': case 'L': toggleMusicLowPass(); break;
                    case 'h': case 'H': toggleSfxHighPass(); break;
                    case 'e': case 'E': toggleSfxEcho(); break;
                    case 'd': case 'D': toggleSidechain(); break;
                    case '+': case '=': adjustSfxVolume(0.1f); break;
                    case '-': case '_': adjustSfxVolume(-0.1f); break;
                    case 't': case 'T': trigger3D(); break;
                    case 'g': case 'G': reset3D(); break;
                    case '[': adjustLastVolume(-0.1f); break;
                    case ']': adjustLastVolume(0.1f); break;
                    case ',': adjustLastPan(-0.15f); break;
                    case '.': adjustLastPan(0.15f); break;
                    case ' ': toggleLastPause(); break;
                    case 127: case 8: stopLast(); break; // Delete / Backspace
                    default: break;
                }
                printStatus();
            }

            tick();
            Sleep(50);
        }
    }

private:
    void printHelp() {
        std::cout << R"(
========================================
  Campello Audio — Windows Sampler
========================================

1-5   : Play tones (SFX bus)
M     : Toggle music tone
L     : Toggle music low-pass filter
H     : Toggle SFX high-pass filter
E     : Toggle SFX echo
D     : Toggle sidechain ducking
+/-   : SFX bus volume up/down
T     : Trigger 3D voice
Arrows: Move 3D source
G     : Reset 3D source
[/]   : Last voice volume -/+
,/.   : Last voice pan left/right
Space : Pause/resume last voice
Del   : Stop last voice
Q     : Quit

)";
        printStatus();
    }

    void printStatus() {
        std::cout << "Active: " << engine_.getActiveVoiceCount()
                  << " | Virtual: " << engine_.getVirtualVoiceCount()
                  << " | SFX vol: " << sfxVolume_
                  << " | Sidechain: " << (sidechainEnabled_ ? "on" : "off")
                  << " | Music LPF: " << (musicLpfEnabled_ ? "on" : "off")
                  << " | SFX HPF: " << (sfxHpfEnabled_ ? "on" : "off")
                  << " | SFX Echo: " << (sfxEchoEnabled_ ? "on" : "off")
                  << " | 3D: (" << pos3D_.x() << ", " << pos3D_.z() << ")"
                  << "\n> " << std::flush;
    }

    void playTone(int index) {
        static const struct {
            audio::WaveForm wave;
            float freq;
            const char* name;
        } tones[] = {
            {audio::WaveForm::Sine,    261.63f, "C4 sine"},
            {audio::WaveForm::Square,  329.63f, "E4 square"},
            {audio::WaveForm::Saw,     392.00f, "G4 saw"},
            {audio::WaveForm::Sine,    523.25f, "C5 sine"},
            {audio::WaveForm::Square,  659.25f, "E5 square"},
        };

        if (index < 0 || index >= 5) return;

        audio::ToneSource tone(tones[index].wave, tones[index].freq);
        audio::PlayDescriptor pd;
        pd.volume = 0.8f;
        lastHandle_ = sfxBus_.play(tone, pd);
        std::cout << "\n[" << tones[index].name << "]\n";
    }

    void toggleMusic() {
        if (!musicHandle_.isValid()) {
            audio::ToneSource music(audio::WaveForm::Sine, 110.0f);
            audio::PlayDescriptor pd;
            pd.looping = true;
            pd.protect = true;
            pd.volume  = 0.5f;
            musicHandle_ = musicBus_.play(music, pd);
            musicPlaying_ = true;
            std::cout << "\n[Music started]\n";
        } else if (musicPlaying_) {
            engine_.pause(musicHandle_);
            musicPlaying_ = false;
            std::cout << "\n[Music paused]\n";
        } else {
            engine_.resume(musicHandle_);
            musicPlaying_ = true;
            std::cout << "\n[Music resumed]\n";
        }
    }

    void toggleMusicLowPass() {
        musicLpfEnabled_ = !musicLpfEnabled_;
        if (musicLpfEnabled_) {
            if (!musicLpf_) musicLpf_ = std::make_shared<audio::LowPassFilter>(900.0f, 0.85f);
            musicBus_.addFilter(musicLpf_, 0);
        } else {
            musicBus_.removeFilter(0);
        }
        std::cout << "\n[Music LPF " << (musicLpfEnabled_ ? "on" : "off") << "]\n";
    }

    void toggleSfxHighPass() {
        sfxHpfEnabled_ = !sfxHpfEnabled_;
        if (sfxHpfEnabled_) {
            if (!sfxHpf_) sfxHpf_ = std::make_shared<audio::HighPassFilter>(900.0f, 0.8f);
            sfxBus_.addFilter(sfxHpf_, 0);
        } else {
            sfxBus_.removeFilter(0);
        }
        std::cout << "\n[SFX HPF " << (sfxHpfEnabled_ ? "on" : "off") << "]\n";
    }

    void toggleSfxEcho() {
        sfxEchoEnabled_ = !sfxEchoEnabled_;
        if (sfxEchoEnabled_) {
            if (!sfxEcho_) sfxEcho_ = std::make_shared<audio::EchoFilter>(0.18f, 0.25f, 0.2f);
            sfxBus_.addFilter(sfxEcho_, 1);
        } else {
            sfxBus_.removeFilter(1);
        }
        std::cout << "\n[SFX Echo " << (sfxEchoEnabled_ ? "on" : "off") << "]\n";
    }

    void toggleSidechain() {
        sidechainEnabled_ = !sidechainEnabled_;
        if (sidechainEnabled_) {
            engine_.setSidechain(&sfxBus_, &musicBus_, -10.0f, 0.03f, 0.35f);
        } else {
            engine_.clearSidechain(&musicBus_);
        }
        std::cout << "\n[Sidechain " << (sidechainEnabled_ ? "on" : "off") << "]\n";
    }

    void adjustSfxVolume(float delta) {
        sfxVolume_ = std::clamp(sfxVolume_ + delta, 0.0f, 1.0f);
        sfxBus_.setVolume(sfxVolume_);
        std::cout << "\n[SFX volume: " << sfxVolume_ << "]\n";
    }

    void trigger3D() {
        audio::ToneSource tone(audio::WaveForm::Sine, 440.0f);
        audio::PlayDescriptor pd;
        pd.enable3d = true;
        pd.position = pos3D_;
        pd.volume   = 0.9f;
        handle3D_ = engine_.play(tone, pd);
        std::cout << "\n[3D voice at " << pos3D_.x() << ", " << pos3D_.z() << "]\n";
    }

    void move3D(float dx, float dz) {
        pos3D_.x() += dx;
        pos3D_.z() += dz;
        if (handle3D_.isValid()) {
            engine_.set3dSourceParameters(handle3D_, pos3D_.x(), pos3D_.y(), pos3D_.z());
            engine_.update3d();
        }
    }

    void reset3D() {
        pos3D_ = {2.0f, 0.0f, -4.0f};
        if (handle3D_.isValid()) {
            engine_.set3dSourceParameters(handle3D_, pos3D_.x(), pos3D_.y(), pos3D_.z());
            engine_.update3d();
        }
        std::cout << "\n[3D reset]\n";
    }

    void adjustLastVolume(float delta) {
        if (!lastHandle_.isValid()) return;
        lastVolume_ = std::clamp(lastVolume_ + delta, 0.0f, 1.5f);
        engine_.setVolume(lastHandle_, lastVolume_);
        std::cout << "\n[Last volume: " << lastVolume_ << "]\n";
    }

    void adjustLastPan(float delta) {
        if (!lastHandle_.isValid()) return;
        lastPan_ = std::clamp(lastPan_ + delta, -1.0f, 1.0f);
        engine_.setPan(lastHandle_, lastPan_);
        std::cout << "\n[Last pan: " << lastPan_ << "]\n";
    }

    void toggleLastPause() {
        if (!lastHandle_.isValid()) return;
        if (engine_.isPaused(lastHandle_)) {
            engine_.resume(lastHandle_);
            std::cout << "\n[Last resumed]\n";
        } else {
            engine_.pause(lastHandle_);
            std::cout << "\n[Last paused]\n";
        }
    }

    void stopLast() {
        if (!lastHandle_.isValid()) return;
        engine_.stop(lastHandle_);
        std::cout << "\n[Last stopped]\n";
    }

    void tick() {
        if (handle3D_.isValid()) {
            engine_.set3dSourceParameters(handle3D_, pos3D_.x(), pos3D_.y(), pos3D_.z());
        }
        engine_.update3d();
        engine_.tick();
    }

    audio::AudioEngine engine_;
    audio::AudioBus    sfxBus_;
    audio::AudioBus    musicBus_;
    audio::SoundHandle musicHandle_;
    audio::SoundHandle lastHandle_;
    audio::SoundHandle handle3D_;
    std::shared_ptr<audio::LowPassFilter>  musicLpf_;
    std::shared_ptr<audio::HighPassFilter> sfxHpf_;
    std::shared_ptr<audio::EchoFilter>     sfxEcho_;
    bool   musicPlaying_     = false;
    bool   musicLpfEnabled_  = false;
    bool   sfxHpfEnabled_    = false;
    bool   sfxEchoEnabled_   = false;
    bool   sidechainEnabled_ = false;
    float  sfxVolume_        = 1.0f;
    float  lastVolume_       = 0.8f;
    float  lastPan_          = 0.0f;
    audio::Vec3 pos3D_       = {2.0f, 0.0f, -4.0f};
};

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

int main() {
    WindowsSampler sampler;
    if (!sampler.initialize()) {
        return 1;
    }
    sampler.run();
    sampler.shutdown();
    return 0;
}
