#pragma once

/// macOS / iOS CoreAudio backend — internal header.
/// Not included by any public API header.

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <atomic>
#include <chrono>
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

namespace systems::leal::campello_audio::coreaudio {

// ---------------------------------------------------------------------------
// Snapshot blend state — all accessed on the game thread only.
// ---------------------------------------------------------------------------

/// Per-bus volume interpolation: resolved busId + from/to values.
struct BusVolumeBlend {
    uint32_t busId = 0;
    float    from  = 1.0f;
    float    to    = 1.0f;
};

/// Saved bus filter so it can be restored on revertSnapshot().
struct SavedBusFilter {
    uint32_t                                      busId  = 0;
    uint32_t                                      slot   = 0;
    std::shared_ptr<systems::leal::campello_audio::Filter> filter; // may be nullptr
};

struct SnapshotBlendState {
    // Registered snapshots keyed by name.
    std::unordered_map<std::string,
                       std::shared_ptr<systems::leal::campello_audio::AudioSnapshot>> registry;

    bool     blending      = false;
    bool     reverting     = false;   ///< true when going back to baseline
    double   blendTimeSecs = 0.0;
    double   blendElapsed  = 0.0;

    /// Active snapshot; nullptr when idle or reverting.
    std::shared_ptr<systems::leal::campello_audio::AudioSnapshot> current;

    // Interpolation
    float                       globalFrom = 1.0f;
    float                       globalTo   = 1.0f;
    std::vector<BusVolumeBlend> busBlends;

    // Baseline (captured once, the first time any snapshot is applied).
    bool     baselineCaptured = false;
    float    baselineGlobal   = 1.0f;
    struct   BaselineBus { uint32_t busId; float volume; };
    std::vector<BaselineBus> baselineBuses;

    // Saved filters for revert.
    std::vector<SavedBusFilter> savedFilters;
};

} // namespace systems::leal::campello_audio::coreaudio

namespace systems::leal::campello_audio::coreaudio {

/// @brief One pending async load — future + destination source + completion callback.
///
/// Stored in CoreAudioData::pendingLoads and polled each AudioEngine::tick().
/// All fields are accessed only on the game thread (tick / loadAsync), never
/// from the audio callback thread, so no mutex is needed.
struct PendingLoad {
    AudioSource*                      source;
    std::future<std::vector<uint8_t>> future;
    LoadCallback                      onDone;
};

/// @brief All CoreAudio state for one AudioEngine instance.
struct CoreAudioData {
    AudioComponentInstance   audioUnit = nullptr;
    bool                     running   = false;

    /// Platform-independent mixer — the render callback writes into this.
    pi::MixerData            mixer;

    /// Async loads awaiting future resolution — drained in AudioEngine::tick().
    std::vector<PendingLoad> pendingLoads;

    /// Registered RTPC parameters — keyed by name.
    /// Accessed only from the game thread (registerParameter / setParameter / getParameter).
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>> parameters;

    /// Mix snapshot blend state — accessed only from the game thread.
    SnapshotBlendState snapshots;

    /// Clock for computing delta time in tick().
    std::chrono::steady_clock::time_point lastTickTime;
    bool                                  lastTickTimeValid = false;
};

/// @brief AudioUnit render callback.
OSStatus renderCallback(void*                       inRefCon,
                        AudioUnitRenderActionFlags* ioActionFlags,
                        const AudioTimeStamp*       inTimeStamp,
                        UInt32                      inBusNumber,
                        UInt32                      inNumberFrames,
                        AudioBufferList*            ioData);

// ---------------------------------------------------------------------------
// iOS-only: AVAudioSession lifecycle and interruption handling.
// Implemented in av_session.mm; compiled only when CAMPELLO_AUDIO_IOS is set.
// ---------------------------------------------------------------------------
#ifdef CAMPELLO_AUDIO_IOS
void setupAVAudioSession();
void teardownAVAudioSession();
void registerInterruptionHandler(CoreAudioData* data);
#endif

} // namespace systems::leal::campello_audio::coreaudio
