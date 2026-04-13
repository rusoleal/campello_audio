#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Dynamic range compressor.
///
/// Reduces the volume of loud signals that exceed the threshold, preventing
/// clipping during intense layered moments (combat, explosions). Essential
/// on bus channels and the master output.
///
/// @par Parameters
/// | Id | Name        | Range          | Default |
/// |----|-------------|----------------|---------|
/// |  0 | Threshold   | -60..0 dBFS    | -12 dB  |
/// |  1 | Ratio       | 1..∞ (e.g. 4)  | 4:1     |
/// |  2 | Attack      | 0.1..500 ms    | 5 ms    |
/// |  3 | Release     | 1..5000 ms     | 50 ms   |
/// |  4 | Knee        | 0..40 dB       | 6 dB    |
/// |  5 | MakeupGain  | 0..40 dB       | 0 dB    |
class CompressorFilter : public Filter {
public:
    /// @param thresholdDb  Level above which compression is applied (dBFS).
    /// @param ratio        Compression ratio (e.g. 4 means 4:1).
    /// @param attackMs     Attack time in milliseconds.
    /// @param releaseMs    Release time in milliseconds.
    /// @param kneeDb       Soft-knee width in dB (0 = hard knee).
    /// @param makeupGainDb Make-up gain applied after compression (dB).
    CompressorFilter(float thresholdDb  = -12.0f,
                     float ratio        =   4.0f,
                     float attackMs     =   5.0f,
                     float releaseMs    =  50.0f,
                     float kneeDb       =   6.0f,
                     float makeupGainDb =   0.0f);
    ~CompressorFilter() override;

    static constexpr uint32_t PARAM_THRESHOLD   = 0;
    static constexpr uint32_t PARAM_RATIO       = 1;
    static constexpr uint32_t PARAM_ATTACK      = 2;
    static constexpr uint32_t PARAM_RELEASE     = 3;
    static constexpr uint32_t PARAM_KNEE        = 4;
    static constexpr uint32_t PARAM_MAKEUP_GAIN = 5;

    /// @return Current gain reduction in dB (useful for metering / gain-reduction display).
    float getGainReductionDb() const;
};

} // namespace systems::leal::campello_audio
