/// @file mixer.cpp
/// @brief Platform-independent mixing core.
///
/// This file contains the stub for the central mix loop.
/// Implementation is tracked in TODO.md (Phase 2 — Mixer Core).

#include "mixer.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace systems::leal::campello_audio::pi {

void MixerData::mixSamples(float* outputBuffer, uint32_t frameCount) {
    const uint32_t totalSamples = frameCount * config.channels;
    std::memset(outputBuffer, 0, totalSamples * sizeof(float));

    // TODO(Phase 2): iterate active voices, decode source samples,
    //   apply volume/pan/pitch, accumulate into outputBuffer.
    //   Handle 3D attenuation, Doppler shift, filter chain processing.

    // Apply master volume
    for (uint32_t i = 0; i < totalSamples; ++i) {
        outputBuffer[i] *= globalVolume;
    }

    // Copy to visualization buffer if enabled
    if (vizEnabled && !vizBuffer.empty()) {
        const uint32_t n = std::min<uint32_t>(totalSamples,
                                              static_cast<uint32_t>(vizBuffer.size()));
        std::copy(outputBuffer, outputBuffer + n, vizBuffer.data());
    }
}

} // namespace systems::leal::campello_audio::pi
