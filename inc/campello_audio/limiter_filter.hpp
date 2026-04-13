#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief True-peak brickwall limiter.
///
/// Prevents output samples from exceeding the ceiling level. Place on the
/// master output bus as the last filter to satisfy platform loudness and
/// true-peak requirements (Apple, console certification).
///
/// @par Parameters
/// | Id | Name      | Range        | Default |
/// |----|-----------|--------------|---------|
/// |  0 | Ceiling   | -12..0 dBFS  | -0.3 dB |
/// |  1 | Lookahead | 0..20 ms     | 5 ms    |
/// |  2 | Release   | 1..1000 ms   | 50 ms   |
class LimiterFilter : public Filter {
public:
    /// @param ceilingDb    Maximum allowed output level (dBFS). -0.3 is standard.
    /// @param lookaheadMs  Lookahead window in milliseconds (increases latency by this amount).
    /// @param releaseMs    Release time in milliseconds.
    LimiterFilter(float ceilingDb   = -0.3f,
                  float lookaheadMs =  5.0f,
                  float releaseMs   = 50.0f);
    ~LimiterFilter() override;

    static constexpr uint32_t PARAM_CEILING   = 0;
    static constexpr uint32_t PARAM_LOOKAHEAD = 1;
    static constexpr uint32_t PARAM_RELEASE   = 2;

    /// @return Current gain reduction in dB.
    float getGainReductionDb() const;
};

} // namespace systems::leal::campello_audio
