#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <campello_audio/types/sound_handle.hpp>
#include <campello_audio/constants/attenuation_model.hpp>
#include <campello_audio/constants/loop_mode.hpp>
#include <campello_audio/filter.hpp>
#include <vector_math/vector_math.hpp>
#include "decoder.hpp"
#include "filter_engine.hpp"
#include "rtpc.hpp"
#include "music_track.hpp"
#include "pattern_track.hpp"

namespace systems::leal::campello_audio::pi {

// Forward declaration — full type in audio_stream.hpp (included by mixer.cpp).
struct AudioStreamData;

using Vec3 = systems::leal::vector_math::Vector3<float>;

// ---------------------------------------------------------------------------
// Voice — one simultaneously playing audio instance.
// ---------------------------------------------------------------------------
struct Voice {
    uint64_t  id          = 0;
    bool      active      = false;
    bool      paused      = false;
    bool      protect     = false;     ///< Protected voices are never stolen.
    bool      is3d        = false;
    uint64_t  lruAge      = 0;         ///< Activation timestamp; lowest = oldest for LRU steal.

    // Source data — one of the three must be set before the voice is mixed.
    //   buffer      : non-owning pointer into the AudioSource's decoded PCM.
    //   ownedBuffer : unique buffer allocated for this voice (ToneSource etc.).
    //                 When ownedBuffer is set, buffer points to ownedBuffer.get().
    //   stream      : ring-buffer streaming source (Phase 14).  When set,
    //                 buffer/ownedBuffer are left null and the mixer reads from
    //                 the ring buffer instead.
    const DecodedBuffer*             buffer      = nullptr;
    std::unique_ptr<DecodedBuffer>   ownedBuffer;   ///< Non-null only for generated sources.
    std::shared_ptr<AudioStreamData> stream;         ///< Non-null only for streaming sources.

    // Playback state
    double    readPos     = 0.0;       ///< Fractional frame index into buffer->samples.
    bool      pingPongFwd = true;      ///< Direction flag for LoopMode::PingPong.
    LoopMode  loopMode    = LoopMode::None;

    // Current computed parameters (may be overridden by fades / LFOs)
    float     volume      = 1.0f;
    float     pan         = 0.0f;      ///< -1 (full left) … +1 (full right)
    float     pitch       = 1.0f;      ///< Playback-speed multiplier (1.0 = normal rate)

    // ---- Filter chain ------------------------------------------------------
    std::array<std::shared_ptr<systems::leal::campello_audio::Filter>,
               systems::leal::campello_audio::Filter::kMaxFiltersPerSource> filters{};
    std::array<FilterState,
               systems::leal::campello_audio::Filter::kMaxFiltersPerSource> filterStates{};

    // ---- 3D state ----------------------------------------------------------
    Vec3             pos              = {0.0f, 0.0f, 0.0f};
    Vec3             vel              = {0.0f, 0.0f, 0.0f};
    float            minDist          = 1.0f;
    float            maxDist          = 1000.0f;
    float            rolloff          = 1.0f;
    float            dopplerFactor    = 1.0f;
    AttenuationModel attenuationModel = AttenuationModel::Inverse;

    // Computed by update3d() — written from game thread, read by mixer thread.
    // Both default to 1 so that 2D voices (is3d = false) are unaffected.
    float            attenuationGain  = 1.0f;  ///< Distance gain in [0, 1].
    float            dopplerPitch     = 1.0f;  ///< Pitch multiplier from Doppler effect.

    // ---- Fade automation (linear ramp, per-sample) -------------------------
    float  fadeVolFrom        = 1.0f;  float  fadeVolTo          = 1.0f;
    double fadeVolTime        = 0.0;   double fadeVolElapsed      = 0.0;
    bool   fadeVolActive      = false; bool   fadeVolStopOnDone  = false;

    float  fadePanFrom        = 0.0f;  float  fadePanTo          = 0.0f;
    double fadePanTime        = 0.0;   double fadePanElapsed      = 0.0;
    bool   fadePanActive      = false;

    float  fadePitchFrom      = 1.0f;  float  fadePitchTo        = 1.0f;
    double fadePitchTime      = 0.0;   double fadePitchElapsed    = 0.0;
    bool   fadePitchActive    = false;

    // ---- RTPC bindings (copied from source at play time) -------------------
    std::vector<ParameterBinding> bindings;

    // ---- Bus routing -------------------------------------------------------
    uint32_t busId = 0;   ///< 0 = main mix; non-zero = routed to buses[busId-1].

    // ---- LFO automation (sinusoidal oscillation, per-sample) ---------------
    float  lfoVolFrom         = 0.0f;  float  lfoVolTo           = 0.0f;
    double lfoVolPeriod       = 1.0;   double lfoVolPhase        = 0.0;
    bool   lfoVolActive       = false;

    float  lfoPanFrom         = 0.0f;  float  lfoPanTo           = 0.0f;
    double lfoPanPeriod       = 1.0;   double lfoPanPhase        = 0.0;
    bool   lfoPanActive       = false;

    // ---- Pattern event curves (set for pattern-spawned voices) -------------
    const PatternEvent* patternEvent = nullptr;  ///< Source event for parameter curves.
    float               spawnBpm     = 120.0f;   ///< BPM at spawn for curve timing.

    // ---- Per-event duration enforcement (0 = one-shot) ---------------------
    double maxDurationSecs = 0.0;   ///< 0 = play until sample ends.
    double elapsedWallSecs = 0.0;   ///< Wall-clock seconds since spawn.
};

// ---------------------------------------------------------------------------
// BusSlot — one active audio bus registered with the mixer.
//
// Voices routed to this bus (Voice::busId == BusSlot::busId) are mixed into
// mixBuffer each callback. The bus filter chain, sidechain gain, and bus
// volume are applied before the result is accumulated into the main output.
//
// All fields accessed only under MixerData::voiceMutex.
// ---------------------------------------------------------------------------
struct BusSlot {
    uint32_t busId   = 0;
    bool     active  = false;
    float    volume  = 1.0f;

    // Per-callback scratch — sized to frameCount × channels, zeroed each call.
    std::vector<float> mixBuffer;

    // Bus filter chain (shared_ptr allows sharing filters between sources/buses).
    std::array<std::shared_ptr<systems::leal::campello_audio::Filter>,
               systems::leal::campello_audio::Filter::kMaxFiltersPerSource> filters{};
    std::array<FilterState,
               systems::leal::campello_audio::Filter::kMaxFiltersPerSource> filterStates{};

    // Visualization (optional — enabled by engine.enableVisualization()).
    std::vector<float> vizBuffer;
    bool               vizEnabled = false;

    // Sidechain — gain reduction applied by a trigger bus driving this bus.
    bool     sidechainActive    = false;
    uint32_t sidechainTriggerId = 0;
    float    sidechainDuckDb    = 0.0f;   ///< Maximum duck amount in dB (≤ 0).
    float    sidechainAttCoef   = 1.0f;   ///< Per-sample attack coefficient.
    float    sidechainRelCoef   = 1.0f;   ///< Per-sample release coefficient.
    double   sidechainEnvDb     = 0.0;    ///< Current envelope in dB (≤ 0).
};

// ---------------------------------------------------------------------------
// MixerData — owns the voice pool and drives the sample-accurate mix loop.
//
// The audio backend calls mixSamples() on its audio thread.
// All other methods are called from the game thread under voiceMutex.
// ---------------------------------------------------------------------------
struct MixerData {
    AudioEngineDescriptor  config;

    std::mutex             voiceMutex;
    std::vector<Voice>     voices;
    uint64_t               nextId    = 1;
    uint64_t               lruClock  = 0;   ///< Monotonically increasing; used for LRU order.

    // Audio buses — registered via AudioEngine::play(AudioBus&).
    // Accessed only under voiceMutex.
    std::vector<BusSlot>   buses;
    uint32_t               nextBusId = 1;

    float    globalVolume            = 1.0f;

    // Global volume fade
    float    gFadeFrom               = 1.0f;  float  gFadeTo       = 1.0f;
    double   gFadeTime               = 0.0;   double gFadeElapsed  = 0.0;
    bool     gFadeActive             = false;

    // Listener state for 3D (primary listener, index 0)
    Vec3     listenerPos             = {0.0f, 0.0f,  0.0f};
    Vec3     listenerVel             = {0.0f, 0.0f,  0.0f};
    Vec3     listenerForward         = {0.0f, 0.0f, -1.0f};
    Vec3     listenerUp              = {0.0f, 1.0f,  0.0f};
    float    soundSpeed              = 343.0f;

    // Scratch buffer — reused each callback for per-voice filter processing.
    // Sized to at least bufferSize × channels; grown lazily if needed.
    std::vector<float>     voiceTempBuffer;

    // Visualization
    /// @brief Logically playing voices below virtualizeThreshold — no DSP cost.
    ///
    /// Access only under voiceMutex. Positions are advanced by mixSamples()
    /// each callback so that re-activation resumes at the correct frame.
    std::vector<Voice>     virtualVoices;

    std::vector<float>     vizBuffer;
    bool                   vizEnabled = false;

    /// Active music track players. Non-owning pointers — lifetime managed by
    /// MusicTrack objects owned by the game. Accessed only under voiceMutex.
    std::vector<MusicTrackData*> musicPlayers;

    /// Active pattern track players. Non-owning pointers — lifetime managed by
    /// PatternTrack objects owned by the game. Accessed only under voiceMutex.
    std::vector<PatternTrackData*> patternPlayers;

    std::atomic<uint32_t>  activeVoiceCount{0};
    std::atomic<uint32_t>  virtualVoiceCount{0};

    // Lightweight RNG state for probability checks on the audio thread.
    uint64_t rngState = 0;

    // RTPC value cache — updated by setParameter() under voiceMutex,
    // read by evaluatePatternCurves() under voiceMutex.
    std::unordered_map<std::string, float> rtpcCache;

    /// @brief Fill @p outputBuffer with @p frameCount × channels interleaved
    ///        float samples.  Called exclusively from the audio thread.
    void mixSamples(float* outputBuffer, uint32_t frameCount);
};

} // namespace systems::leal::campello_audio::pi
