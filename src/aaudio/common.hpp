#pragma once

/// Android AAudio backend — internal header.
/// Not included by any public API header.
///
/// Requires Android API level 26+. Linked via -laaudio (android.cmake).
/// For devices < API 26 the library will fail to load aaudio symbols at
/// runtime; the caller is expected to check Build.VERSION.SDK_INT before
/// calling AudioEngine::init().

#include <aaudio/AAudio.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
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

namespace systems::leal::campello_audio::aaudio {

// ---------------------------------------------------------------------------
// Snapshot blend state — accessed on the game thread only.
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
// AAudioData — all AAudio state for one AudioEngine instance.
// ---------------------------------------------------------------------------

struct AAudioData {
    /// Opened AAudio stream — nullptr until init() succeeds.
    AAudioStream* stream = nullptr;

    /// Set by the error callback when AAUDIO_ERROR_DISCONNECTED is received.
    /// The stream becomes invalid; the user must call deinit()/init() to recover.
    std::atomic<bool> deviceLost{false};

    /// Platform-independent mixer — called from the AAudio data callback thread.
    pi::MixerData            mixer;

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
// AAudio callbacks — declared here, defined in audio_engine.cpp.
// ---------------------------------------------------------------------------

aaudio_data_callback_result_t dataCallback(AAudioStream* stream,
                                           void*         userData,
                                           void*         audioData,
                                           int32_t       numFrames);

void errorCallback(AAudioStream* stream,
                   void*         userData,
                   aaudio_result_t error);

} // namespace systems::leal::campello_audio::aaudio
