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
    Sine
};

} // namespace systems::leal::campello_audio
