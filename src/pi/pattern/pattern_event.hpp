#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <campello_audio/constants/curve_type.hpp>

namespace systems::leal::campello_audio::pi {

/// @brief Target parameter for a continuous modulation curve on a pattern event.
enum class PatternParam : uint32_t {
    None = 0,
    Gain,
    Pitch,
    Pan,
    LpfCutoff,
    HpfCutoff,
    // Room for expansion
    Count
};

/// @brief A time-varying parameter curve attached to a PatternEvent.
///
/// Evaluated at runtime on the audio thread. If rtpcName is non-empty,
/// the curve's output is multiplied by the current RTPC value.
struct ParameterCurve {
    PatternParam targetParam = PatternParam::None;
    CurveType    type        = CurveType::Linear;
    double       periodBeats = 4.0;   // Cycle length in beats
    float        minValue    = 0.0f;
    float        maxValue    = 1.0f;
    std::string  rtpcName;            // If non-empty, overrides min/max with RTPC value

    /// Evaluate the curve at a given phase [0..1) within its period.
    float evaluate(double phase) const;

    /// Evaluate with RTPC override. If rtpcName is set, @p rtpcValue replaces the normalized output.
    float evaluate(double phase, float rtpcValue) const;
};

/// @brief One scheduled trigger in a compiled pattern timeline.
struct PatternEvent {
    double beat = 0.0;          // Position within pattern (beats)
    double duration = 0.0;      // In beats (0 = one-shot)
    std::string sourceLabel;    // Key into PatternBank's source registry
    float gain = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    float probability = 1.0f;   // For generative variation (0..1]

    // Parameter modulation curves evaluated at runtime
    std::vector<ParameterCurve> paramCurves;
};

} // namespace systems::leal::campello_audio::pi
