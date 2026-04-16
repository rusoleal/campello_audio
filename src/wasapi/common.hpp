#pragma once

/// WASAPI backend — internal header.
/// Not included by any public API header.
///
/// Requires WIN32_LEAN_AND_MEAN and NOMINMAX (set by windows.cmake).

#include <Windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

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

namespace systems::leal::campello_audio::wasapi {

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
// WASAPIData — all WASAPI state for one AudioEngine instance.
// ---------------------------------------------------------------------------

struct WASAPIData {
    // COM audio objects — released in deinit().
    IAudioClient*        audioClient   = nullptr;
    IAudioRenderClient*  renderClient  = nullptr;
    HANDLE               hEvent        = nullptr;   ///< Event signalled by WASAPI each buffer.
    UINT32               bufferFrames  = 0;         ///< Total frames in the WASAPI buffer.

    // Render thread.
    std::thread              renderThread;
    std::atomic<bool>        running{false};
    std::atomic<bool>        deviceLost{false};  ///< Set when AUDCLNT_E_DEVICE_INVALIDATED.

    /// Platform-independent mixer — called from the render thread.
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
// Render thread entry point — declared here, defined in audio_engine.cpp.
// ---------------------------------------------------------------------------

void renderProc(WASAPIData* d);

} // namespace systems::leal::campello_audio::wasapi
