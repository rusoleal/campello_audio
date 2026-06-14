#pragma once

namespace systems::leal::campello_audio {

/// @brief Curve shape used when mapping an AudioParameter value to an audio property.
///
/// Lets audio designers express non-linear relationships without code changes,
/// e.g. "speed → engine pitch" with an exponential curve for more dramatic feel.
enum class CurveType {
    /// Value maps proportionally (y = x).
    Linear,
    /// Slow change at low values, fast at high (y = x^2).
    Exponential,
    /// Fast change at low values, slow at high (y = sqrt(x)).
    Logarithmic,
    /// Smooth ease-in / ease-out (cubic S-curve).
    SCurve,
    /// Half-sine arc — rises and falls smoothly.
    Sine,
    /// Linear ramp from min to max (same shape as Linear, semantically distinct for LFOs).
    Saw,
    /// Smooth pseudo-random noise (Perlin-like).
    Perlin,
    /// Square wave — instant jump between min and max at midpoint.
    Square,
    /// Triangle wave — linear rise then linear fall.
    Triangle
};

} // namespace systems::leal::campello_audio
