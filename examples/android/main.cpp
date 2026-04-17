/// @file examples/android/main.cpp
/// @brief Android NativeActivity sampler — demonstrates campello_audio on Android.

#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <campello_audio/audio_engine.hpp>
#include <campello_audio/tone_source.hpp>

#include <cmath>
#include <memory>

#define LOG_TAG "CampelloAudio"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

namespace audio = systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct AppState {
    std::unique_ptr<audio::AudioEngine> engine;
    bool initialized = false;
    int32_t width = 0;
    int32_t height = 0;

    bool init() {
        engine = std::make_unique<audio::AudioEngine>();
        audio::AudioEngineDescriptor desc;
        desc.sampleRate = 48000; // Android typically uses 48 kHz
        desc.bufferSize = 512;
        desc.channels   = 2;
        if (!engine->init(desc)) {
            LOGE("AudioEngine::init() failed");
            return false;
        }
        LOGI("AudioEngine initialized — sr=%d ch=%d",
             engine->getSampleRate(), engine->getChannels());
        initialized = true;
        return true;
    }

    void deinit() {
        if (engine) {
            engine->deinit();
            engine.reset();
        }
        initialized = false;
    }

    void playTone(int zone) {
        if (!initialized || !engine) return;

        static const float freqs[] = {261.63f, 329.63f, 392.00f, 523.25f, 659.25f};
        static const audio::WaveForm waves[] = {
            audio::WaveForm::Sine, audio::WaveForm::Square,
            audio::WaveForm::Saw, audio::WaveForm::Sine, audio::WaveForm::Square
        };

        if (zone < 0 || zone >= 5) return;

        audio::ToneSource tone(waves[zone], freqs[zone]);
        audio::PlayDescriptor pd;
        pd.volume = 0.8f;
        engine->play(tone, pd);

        LOGI("Zone %d — %.2f Hz", zone, freqs[zone]);
    }
};

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

static void handle_cmd(android_app* app, int32_t cmd) {
    auto* state = static_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                state->width  = ANativeWindow_getWidth(app->window);
                state->height = ANativeWindow_getHeight(app->window);
                if (!state->initialized) {
                    state->init();
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            state->deinit();
            break;
        case APP_CMD_LOST_FOCUS:
            // Audio continues in background; add pause logic here if desired.
            break;
        case APP_CMD_GAINED_FOCUS:
            break;
    }
}

static int32_t handle_input(android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            auto* state = static_cast<AppState*>(app->userData);
            if (state->width > 0) {
                float x = AMotionEvent_getX(event, 0);
                int zone = static_cast<int>(x / (state->width / 5.0f));
                if (zone < 0) zone = 0;
                if (zone > 4) zone = 4;
                state->playTone(zone);
            }
        }
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

void android_main(android_app* state) {
    AppState appState;
    state->userData = &appState;
    state->onAppCmd = handle_cmd;
    state->onInputEvent = handle_input;

    while (true) {
        int ident;
        int events;
        android_poll_source* source;

        while ((ident = ALooper_pollAll(appState.initialized ? 0 : -1,
                                        nullptr, &events,
                                        reinterpret_cast<void**>(&source))) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }
            if (state->destroyRequested != 0) {
                return;
            }
        }

        if (appState.initialized && appState.engine) {
            appState.engine->tick();
        }
    }
}
