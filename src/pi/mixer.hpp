#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <campello_audio/types/sound_handle.hpp>
#include <vector_math/vector_math.hpp>

namespace systems::leal::campello_audio::pi {

using Vec3 = systems::leal::vector_math::Vector3<float>;

/// Internal voice state tracked by the mixer.
struct Voice {
    uint64_t  id          = 0;
    bool      active      = false;
    bool      paused      = false;
    bool      looping     = false;
    bool      protect     = false;
    bool      is3d        = false;
    float     volume      = 1.0f;
    float     pan         = 0.0f;
    float     pitch       = 1.0f;
    double    position    = 0.0;   ///< Current playback position in seconds.

    // 3D state
    Vec3      pos         = {0.0f, 0.0f, 0.0f};
    Vec3      vel         = {0.0f, 0.0f, 0.0f};
    float     minDist     = 1.0f;
    float     maxDist     = 1000.0f;
    float     rolloff     = 1.0f;
    float     dopplerFactor = 1.0f;

    // Fade/oscillate automation
    float     fadeVolTarget  = 0;
    double    fadeVolTime    = 0;
    bool      fadeVolActive  = false;
};

/// Platform-independent mixer core.
/// The backend calls mixSamples() from its audio thread.
struct MixerData {
    AudioEngineDescriptor config;

    std::mutex            voiceMutex;
    std::vector<Voice>    voices;
    uint64_t              nextId = 1;

    float                 globalVolume = 1.0f;

    // Listener state for 3D
    Vec3  listenerPos     = {0.0f, 0.0f,  0.0f};
    Vec3  listenerVel     = {0.0f, 0.0f,  0.0f};
    Vec3  listenerForward = {0.0f, 0.0f, -1.0f};
    Vec3  listenerUp      = {0.0f, 1.0f,  0.0f};
    float soundSpeed      = 343.0f;

    // Visualization buffer
    std::vector<float>            vizBuffer;
    bool                          vizEnabled = false;

    std::atomic<uint32_t>         activeVoiceCount{0};

    /// @brief Fill @p outputBuffer with @p frameCount * channels interleaved
    ///        float samples. Called from the audio thread.
    void mixSamples(float* outputBuffer, uint32_t frameCount);
};

} // namespace systems::leal::campello_audio::pi
