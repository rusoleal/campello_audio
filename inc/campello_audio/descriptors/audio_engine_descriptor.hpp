#pragma once

#include <cstdint>

namespace systems::leal::campello_audio {

/// @brief Configuration passed to AudioEngine::init().
struct AudioEngineDescriptor {
    /// Output sample rate in Hz. Defaults to 44100.
    uint32_t sampleRate   = 44100;

    /// Number of samples per audio callback buffer. Power-of-two recommended.
    /// Lower values reduce latency; higher values reduce CPU overhead.
    uint32_t bufferSize   = 512;

    /// Maximum simultaneous *active* (processed) voices.
    /// When exceeded, the quietest unprotected active voice is stopped to make
    /// room — unless virtual voices are enabled (see maxVirtualVoices).
    uint32_t maxVoices    = 32;

    /// Maximum simultaneous *virtual* voices.
    /// Voices below virtualizeThreshold are suspended (position tracked, no DSP)
    /// instead of being killed. They resume when they become audible again.
    /// Set to 0 to disable virtualization (all excess voices are killed).
    uint32_t maxVirtualVoices   = 256;

    /// Volume threshold below which a voice is virtualized rather than killed.
    /// Evaluated after 3D attenuation. Typical range: 0.0001 – 0.01.
    float    virtualizeThreshold = 0.001f;

    /// Output channel count: 1 = mono, 2 = stereo, 6 = 5.1, 8 = 7.1.
    uint32_t channels     = 2;

    /// Number of independent listeners (1 = normal, 2+ = split-screen co-op).
    /// Each listener produces its own spatial mix; the results are summed.
    uint32_t listenerCount = 1;

    /// Enable visualization data collection (FFT / waveform). Slightly
    /// increases CPU cost per buffer; disable in shipping builds if unused.
    bool     visualization = false;
};

} // namespace systems::leal::campello_audio
