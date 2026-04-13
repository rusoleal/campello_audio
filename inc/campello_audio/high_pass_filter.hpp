#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Bi-quad high-pass filter — attenuates frequencies below the cutoff.
///
/// @par Parameters
/// | Id | Name      | Range       | Default |
/// |----|-----------|-------------|---------|
/// |  0 | Cutoff    | 10..20000Hz | 1000 Hz |
/// |  1 | Resonance | 0.1..10     | 0.707   |
class HighPassFilter : public Filter {
public:
    /// @param cutoffHz    Initial cutoff frequency in Hz.
    /// @param resonance   Q factor (resonance). 0.707 = Butterworth (no peak).
    HighPassFilter(float cutoffHz = 1000.0f, float resonance = 0.707f);
    ~HighPassFilter() override;

    static constexpr uint32_t PARAM_CUTOFF    = 0;
    static constexpr uint32_t PARAM_RESONANCE = 1;
};

} // namespace systems::leal::campello_audio
