#pragma once

#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

/// @brief Simple delay-line echo / feedback filter.
///
/// @par Parameters
/// | Id | Name    | Range      | Default |
/// |----|---------|------------|---------|
/// |  0 | Delay   | 0..2s      | 0.3 s   |
/// |  1 | Decay   | 0..1       | 0.5     |
/// |  2 | Filter  | 0..1       | 0.0     |
class EchoFilter : public Filter {
public:
    /// @param delaySecs  Echo delay in seconds.
    /// @param decay      Feedback gain [0..1]. 0 = single echo; 1 = infinite loop.
    /// @param filterVal  Low-pass coefficient applied to each echo [0..1].
    EchoFilter(float delaySecs = 0.3f, float decay = 0.5f, float filterVal = 0.0f);
    ~EchoFilter() override;

    static constexpr uint32_t PARAM_DELAY  = 0;
    static constexpr uint32_t PARAM_DECAY  = 1;
    static constexpr uint32_t PARAM_FILTER = 2;
};

} // namespace systems::leal::campello_audio
