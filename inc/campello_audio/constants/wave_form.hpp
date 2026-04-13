#pragma once

namespace systems::leal::campello_audio {

/// @brief Waveform shape for procedural tone generation via ToneSource.
enum class WaveForm {
    /// Smooth sinusoidal wave.
    Sine,
    /// Square wave alternating between +1 and -1.
    Square,
    /// Sawtooth wave with linear rise and instant fall.
    Saw,
    /// Triangle wave with linear rise and fall.
    Triangle,
    /// White noise (random samples).
    Noise
};

} // namespace systems::leal::campello_audio
