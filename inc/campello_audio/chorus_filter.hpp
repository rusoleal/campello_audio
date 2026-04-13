#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Stereo chorus effect — thickens sounds by layering slightly detuned copies.
///
/// Useful for pads, strings, voice lines, and any sound that benefits from a
/// wider, richer texture.
///
/// @par Parameters
/// | Id | Name     | Range     | Default |
/// |----|----------|-----------|---------|
/// |  0 | Depth    | 0..1      | 0.5     |
/// |  1 | Rate     | 0.01..10Hz| 1.0 Hz  |
/// |  2 | Wet      | 0..1      | 0.5     |
/// |  3 | Dry      | 0..1      | 1.0     |
/// |  4 | Voices   | 2..8      | 3       |
class ChorusFilter : public Filter {
public:
    /// @param depth   Modulation depth [0..1].
    /// @param rateHz  LFO rate in Hz.
    /// @param wet     Wet (chorus) signal level.
    ChorusFilter(float depth = 0.5f, float rateHz = 1.0f, float wet = 0.5f);
    ~ChorusFilter() override;

    static constexpr uint32_t PARAM_DEPTH  = 0;
    static constexpr uint32_t PARAM_RATE   = 1;
    static constexpr uint32_t PARAM_WET    = 2;
    static constexpr uint32_t PARAM_DRY    = 3;
    static constexpr uint32_t PARAM_VOICES = 4;
};

} // namespace systems::leal::campello_audio
