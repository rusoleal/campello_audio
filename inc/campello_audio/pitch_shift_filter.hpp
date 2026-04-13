#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Real-time pitch shifter (time-domain phase vocoder).
///
/// Shifts the pitch of a sound without changing its duration, using a
/// phase-vocoder / WSOLA algorithm. Useful for voice-line pitch variation,
/// alien/robot effects, and runtime instrument transpositions.
///
/// @note Higher quality settings increase CPU cost significantly.
///
/// @par Parameters
/// | Id | Name      | Range        | Default |
/// |----|-----------|--------------|---------|
/// |  0 | Semitones | -24..24      | 0       |
/// |  1 | Quality   | 0(low)..2(hi)| 1       |
class PitchShiftFilter : public Filter {
public:
    /// @param semitones  Pitch offset in semitones. 0 = no shift, 12 = one octave up.
    explicit PitchShiftFilter(float semitones = 0.0f);
    ~PitchShiftFilter() override;

    static constexpr uint32_t PARAM_SEMITONES = 0;
    static constexpr uint32_t PARAM_QUALITY   = 1;
};

} // namespace systems::leal::campello_audio
