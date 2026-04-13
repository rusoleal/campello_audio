#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Freeverb-based algorithmic reverb filter.
///
/// @par Parameters
/// | Id | Name       | Range  | Default |
/// |----|------------|--------|---------|
/// |  0 | RoomSize   | 0..1   | 0.5     |
/// |  1 | Damping    | 0..1   | 0.5     |
/// |  2 | Wet        | 0..1   | 0.33    |
/// |  3 | Dry        | 0..1   | 1.0     |
/// |  4 | Width      | 0..1   | 1.0     |
class ReverbFilter : public Filter {
public:
    /// @param roomSize  Perceived room size [0=small..1=large].
    /// @param damping   High-frequency damping [0=bright..1=dark].
    /// @param wet       Wet (reverb) signal level [0..1].
    /// @param dry       Dry (original) signal level [0..1].
    ReverbFilter(float roomSize = 0.5f, float damping = 0.5f,
                 float wet = 0.33f,     float dry = 1.0f);
    ~ReverbFilter() override;

    static constexpr uint32_t PARAM_ROOM_SIZE = 0;
    static constexpr uint32_t PARAM_DAMPING   = 1;
    static constexpr uint32_t PARAM_WET       = 2;
    static constexpr uint32_t PARAM_DRY       = 3;
    static constexpr uint32_t PARAM_WIDTH     = 4;
};

} // namespace systems::leal::campello_audio
