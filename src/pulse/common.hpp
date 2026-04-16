#pragma once

/// PulseAudio backend — internal header.
/// Not included by any public API header.
///
/// Requires libpulse-simple (pkg-config: libpulse-simple).
/// A dedicated fill thread calls MixerData::mixSamples() and writes
/// each block to the pa_simple connection via pa_simple_write().

#include <pulse/simple.h>
#include <pulse/error.h>

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

namespace systems::leal::campello_audio::pulse {

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
// PulseData — all PulseAudio state for one AudioEngine instance.
// ---------------------------------------------------------------------------

struct PulseData {
    /// Synchronous PulseAudio connection — nullptr until init() succeeds.
    pa_simple*        pa           = nullptr;

    /// Fill thread — calls mixSamples() + pa_simple_write() in a loop.
    std::thread       fillThread;
    std::atomic<bool> running{false};

    /// Frames per write — set from AudioEngineDescriptor::bufferSize.
    uint32_t          bufferFrames = 512;

    /// Platform-independent mixer — called from the fill thread.
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
// Fill thread entry point — declared here, defined in audio_engine.cpp.
// ---------------------------------------------------------------------------

void fillProc(PulseData* d);

} // namespace systems::leal::campello_audio::pulse
