/// @file surround.hpp
/// @brief VBAP (Vector Base Amplitude Panning) for mono, stereo, 5.1, and 7.1.
///
/// Channel order follows the standard WAV/ITU interleaving:
///   Stereo : [FL, FR]
///   5.1    : [FL, FR, FC, LFE, BL, BR]
///   7.1    : [FL, FR, FC, LFE, BL, BR, SL, SR]
///
/// pan ∈ [-1, +1] maps to azimuth ∈ [-π/2, +π/2]:
///   pan = -1 → 90° to the left
///   pan =  0 → straight ahead (front centre for 5.1/7.1)
///   pan = +1 → 90° to the right
///
/// LFE (channel 3 in 5.1/7.1) always receives 0 gain — bass management is
/// left to the audio device driver.

#pragma once

#include <cstdint>
#include <cmath>

namespace systems::leal::campello_audio::pi {

namespace surround_detail {

struct SpkrAngle {
    uint32_t ch;
    float    azRad;
};

// Degrees → radians conversion constant (avoids #include <numbers> here).
static constexpr float kDTR = 3.14159265358979323846f / 180.0f;
static constexpr float kPi2 = 3.14159265358979323846f * 0.5f;

// 5.1 speakers sorted by ascending azimuth, LFE (ch3) excluded.
// ITU-R BS.775: FL=±30°, FC=0°, BL/BR=±110°.
static constexpr SpkrAngle kSpk51[5] = {
    {4, -110.0f * kDTR},  // BL
    {0,  -30.0f * kDTR},  // FL
    {2,    0.0f        },  // FC
    {1,   30.0f * kDTR},  // FR
    {5,  110.0f * kDTR},  // BR
};

// 7.1 speakers sorted by ascending azimuth, LFE (ch3) excluded.
// FL/FR=±30°, FC=0°, SL/SR=±90°, BL/BR=±150°.
static constexpr SpkrAngle kSpk71[7] = {
    {4, -150.0f * kDTR},  // BL
    {6,  -90.0f * kDTR},  // SL
    {0,  -30.0f * kDTR},  // FL
    {2,    0.0f        },  // FC
    {1,   30.0f * kDTR},  // FR
    {7,   90.0f * kDTR},  // SR
    {5,  150.0f * kDTR},  // BR
};

/// Pairwise VBAP over a sorted speaker arc.
/// Finds the pair bracketing `azimuthRad` and computes constant-power gains.
inline void vbapArc(float azimuthRad,
                    const SpkrAngle* spkrs, uint32_t n,
                    float* gains) {
    // Clamp at boundaries.
    if (azimuthRad <= spkrs[0].azRad)       { gains[spkrs[0].ch]   = 1.0f; return; }
    if (azimuthRad >= spkrs[n - 1].azRad)   { gains[spkrs[n-1].ch] = 1.0f; return; }

    for (uint32_t i = 0; i < n - 1; ++i) {
        if (azimuthRad <= spkrs[i + 1].azRad) {
            const float span  = spkrs[i + 1].azRad - spkrs[i].azRad;
            const float alpha = (span > 0.0f) ? (azimuthRad - spkrs[i].azRad) / span : 0.0f;
            gains[spkrs[i].ch]     = std::cos(alpha * kPi2);
            gains[spkrs[i + 1].ch] = std::sin(alpha * kPi2);
            return;
        }
    }
}

} // namespace surround_detail

/// Fill gains[outCh] with VBAP pan gains for the given pan value.
///
/// @param pan    Pan value ∈ [-1, +1].
/// @param outCh  Output channel count (1, 2, 6, or 8).
/// @param gains  Output array of at least outCh floats; zeroed on entry.
inline void computeVbapGains(float pan, uint32_t outCh, float* gains) {
    for (uint32_t i = 0; i < outCh; ++i) gains[i] = 0.0f;

    switch (outCh) {
    case 1:
        gains[0] = 1.0f;
        break;

    case 2: {
        // Constant-power stereo — identical to the existing Phase 2 formula.
        constexpr float kQPi = 3.14159265358979323846f * 0.25f;
        const float theta = (pan + 1.0f) * kQPi;
        gains[0] = std::cos(theta);  // FL
        gains[1] = std::sin(theta);  // FR
        break;
    }

    case 6:
        surround_detail::vbapArc(pan * surround_detail::kPi2,
                                 surround_detail::kSpk51, 5, gains);
        break;

    case 8:
        surround_detail::vbapArc(pan * surround_detail::kPi2,
                                 surround_detail::kSpk71, 7, gains);
        break;

    default:
        // Unknown channel count: route everything to channel 0.
        if (outCh > 0) gains[0] = 1.0f;
        break;
    }
}

} // namespace systems::leal::campello_audio::pi
