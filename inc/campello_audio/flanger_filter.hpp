#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Flanger effect — comb-filtering via a short, modulated delay line.
///
/// Classic "jet engine" or "swoosh" sound, widely used in music and sci-fi audio.
///
/// @par Parameters
/// | Id | Name     | Range      | Default |
/// |----|----------|------------|---------|
/// |  0 | Depth    | 0..1       | 0.5     |
/// |  1 | Rate     | 0.01..5 Hz | 0.25 Hz |
/// |  2 | Feedback | -1..1      | 0.5     |
/// |  3 | Wet      | 0..1       | 0.5     |
/// |  4 | Dry      | 0..1       | 1.0     |
class FlangerFilter : public Filter {
public:
    /// @param depth      Modulation depth [0..1].
    /// @param rateHz     LFO rate in Hz.
    /// @param feedback   Feedback amount [-1..1]. Negative values invert the phase.
    FlangerFilter(float depth = 0.5f, float rateHz = 0.25f, float feedback = 0.5f);
    ~FlangerFilter() override;

    static constexpr uint32_t PARAM_DEPTH    = 0;
    static constexpr uint32_t PARAM_RATE     = 1;
    static constexpr uint32_t PARAM_FEEDBACK = 2;
    static constexpr uint32_t PARAM_WET      = 3;
    static constexpr uint32_t PARAM_DRY      = 4;
};

} // namespace systems::leal::campello_audio
