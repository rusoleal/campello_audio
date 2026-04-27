/// @file main.cpp
/// @brief Minimal WASM example for campello_audio.
///
/// Creates an AudioEngine, generates a sine wave tone, and plays it.
/// Build with Emscripten and open the generated HTML in a browser.

#include <campello_audio/audio_engine.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <emscripten/emscripten.h>
#include <cstdio>

using namespace systems::leal::campello_audio;

static AudioEngine gEngine;
static ToneSource  gTone;

extern "C" {

/// Called from JavaScript when the user clicks the "Start Audio" button.
EMSCRIPTEN_KEEPALIVE
bool startAudio() {
    if (!gEngine.init({})) {
        std::printf("AudioEngine::init() failed\n");
        return false;
    }

    gTone.setWaveForm(WaveForm::Sine);
    gTone.setFrequency(440.0f);
    gTone.setVolume(0.3f);

    gEngine.play(gTone);
    std::printf("Audio started — 440 Hz sine tone\n");
    return true;
}

/// Called from JavaScript when the user clicks the "Stop Audio" button.
EMSCRIPTEN_KEEPALIVE
void stopAudio() {
    gEngine.deinit();
    std::printf("Audio stopped\n");
}

} // extern "C"

int main() {
    std::printf("WASM example loaded. Click 'Start Audio' to begin.\n");
    return 0;
}
