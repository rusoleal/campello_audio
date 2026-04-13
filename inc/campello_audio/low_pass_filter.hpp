#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Bi-quad low-pass filter — attenuates frequencies above the cutoff.
///
/// @par Parameters
/// | Id | Name      | Range       | Default |
/// |----|-----------|-------------|---------|
/// |  0 | Cutoff    | 10..20000Hz | 3000 Hz |
/// |  1 | Resonance | 0.1..10     | 0.707   |
///
/// @code
/// auto lpf = std::make_shared<LowPassFilter>(1000.0f, 1.0f);
/// source.addFilter(lpf, 0);
/// @endcode
class LowPassFilter : public Filter {
public:
    /// @param cutoffHz    Initial cutoff frequency in Hz.
    /// @param resonance   Q factor (resonance). 0.707 = Butterworth (no peak).
    LowPassFilter(float cutoffHz = 3000.0f, float resonance = 0.707f);
    ~LowPassFilter() override;

    static constexpr uint32_t PARAM_CUTOFF    = 0;
    static constexpr uint32_t PARAM_RESONANCE = 1;
};

} // namespace systems::leal::campello_audio
