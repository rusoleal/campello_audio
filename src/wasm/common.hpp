#pragma once

/// WebAssembly backend — internal header.
/// Not included by any public API header.
///
/// Uses Emscripten's Web Audio AudioWorklet API for audio output.
/// Requires Emscripten with -pthread and SharedArrayBuffer support.
/// The browser must serve COOP/COEP headers for pthreads to work.

#include <emscripten/webaudio.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/audio_snapshot.hpp>
#include <campello_audio/filter.hpp>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <campello_audio/types/loader.hpp>
#include "../pi/mixer.hpp"

namespace systems::leal::campello_audio {
class AudioSource;
}

namespace systems::leal::campello_audio::wasm {

// ---------------------------------------------------------------------------
// Snapshot blend state — all accessed on the game thread only.
// ---------------------------------------------------------------------------

struct BusVolumeBlend {
    uint32_t busId = 0;
    float    from  = 1.0f;
    float    to    = 1.0f;
};

struct SavedBusFilter {
    uint32_t                                            busId  = 0;
    uint32_t                                            slot   = 0;
    std::shared_ptr<systems::leal::campello_audio::Filter> filter;
};

struct SnapshotBlendState {
    std::unordered_map<std::string,
                       std::shared_ptr<systems::leal::campello_audio::AudioSnapshot>> registry;

    bool     blending      = false;
    bool     reverting     = false;
    double   blendTimeSecs = 0.0;
    double   blendElapsed  = 0.0;

    std::shared_ptr<systems::leal::campello_audio::AudioSnapshot> current;

    float                       globalFrom = 1.0f;
    float                       globalTo   = 1.0f;
    std::vector<BusVolumeBlend> busBlends;

    bool     baselineCaptured = false;
    float    baselineGlobal   = 1.0f;
    struct   BaselineBus { uint32_t busId; float volume; };
    std::vector<BaselineBus> baselineBuses;

    std::vector<SavedBusFilter> savedFilters;
};

// ---------------------------------------------------------------------------
// Pending async load
// ---------------------------------------------------------------------------

struct PendingLoad {
    AudioSource*                      source;
    std::future<std::vector<uint8_t>> future;
    LoadCallback                      onDone;
};

// ---------------------------------------------------------------------------
// WasmData — all WASM backend state for one AudioEngine instance.
// ---------------------------------------------------------------------------

struct WasmData {
    /// Web Audio AudioContext handle.
    EMSCRIPTEN_WEBAUDIO_T audioContext = 0;

    /// AudioWorkletNode handle.
    EMSCRIPTEN_AUDIO_WORKLET_NODE_T audioNode = 0;

    /// Set once the AudioWorklet node is created and connected.
    std::atomic<bool> running{false};

    /// Set during deinit() to signal async callbacks to abort.
    std::atomic<bool> destroyed{false};

    /// Setup state: 0=init, 1=thread_started, 2=processor_created, 3=node_created.
    std::atomic<int> setupState{0};

    /// Stack memory for the AudioWorklet thread (must be 16-byte aligned).
    std::vector<uint8_t> workletStack;

    /// Scratch buffer for interleaved → planar conversion in the process callback.
    std::vector<float> processScratch;

    /// Platform-independent mixer — called from the AudioWorklet process callback.
    pi::MixerData mixer;

    /// Pending async loads — drained in AudioEngine::tick().
    std::vector<PendingLoad> pendingLoads;

    /// Registered RTPC parameters.
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>> parameters;

    /// Mix snapshot blend state.
    SnapshotBlendState snapshots;

    /// Delta-time clock for tick().
    std::chrono::steady_clock::time_point lastTickTime;
    bool                                  lastTickTimeValid = false;
};

// ---------------------------------------------------------------------------
// AudioWorklet callbacks — declared here, defined in audio_engine.cpp.
// ---------------------------------------------------------------------------

void workletThreadInitialized(EMSCRIPTEN_WEBAUDIO_T audioContext,
                              EM_BOOL success,
                              void* userData);

void workletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T audioContext,
                             EM_BOOL success,
                             void* userData);

EM_BOOL processCallback(int numInputs,
                        const AudioSampleFrame* inputs,
                        int numOutputs,
                        AudioSampleFrame* outputs,
                        int numParams,
                        const AudioParamFrame* params,
                        void* userData);

} // namespace systems::leal::campello_audio::wasm
