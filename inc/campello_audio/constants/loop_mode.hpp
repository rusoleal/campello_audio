#pragma once

namespace systems::leal::campello_audio {

/// @brief Loop behaviour when an audio source reaches its end.
enum class LoopMode {
    /// Play once and stop.
    None,
    /// Loop from the beginning when the end is reached.
    Loop,
    /// Alternate forward and backward playback (ping-pong).
    PingPong
};

} // namespace systems::leal::campello_audio
