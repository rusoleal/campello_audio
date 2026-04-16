#pragma once

/// @file rtpc.hpp
/// @brief Internal RTPC types: AudioParameterData, ParameterBinding,
///        curve evaluation helpers.

#include <cstdint>
#include <string>
#include <campello_audio/audio_parameter.hpp>   // AudioSourceProperty, CurveType

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// AudioParameterData — stored in AudioParameter::native
// ---------------------------------------------------------------------------
struct AudioParameterData {
    std::string name;
    float       value    = 0.0f;
    float       minValue = 0.0f;
    float       maxValue = 1.0f;
};

// ---------------------------------------------------------------------------
// ParameterBinding — one source-to-property mapping
// ---------------------------------------------------------------------------
struct ParameterBinding {
    std::string         paramName;
    AudioSourceProperty property      = AudioSourceProperty::Volume;
    CurveType           curve         = CurveType::Linear;
    float               outputMin     = 0.0f;
    float               outputMax     = 1.0f;
    uint32_t            filterSlot    = 0;
    uint32_t            filterParamId = 0;
};

// ---------------------------------------------------------------------------
// Curve evaluation
// ---------------------------------------------------------------------------

/// @brief Map a normalised input @p t ∈ [0, 1] through @p curve.
/// @return Normalised output in [0, 1].
float evaluateCurve(CurveType curve, float t);

/// @brief Map a raw parameter value through a binding's curve to the output range.
/// @param paramValue   Current parameter value (raw game units).
/// @param paramMin     Parameter minimum.
/// @param paramMax     Parameter maximum.
/// @param b            Binding descriptor (curve, outputMin, outputMax).
/// @return Mapped property value.
float applyBinding(float paramValue, float paramMin, float paramMax,
                   const ParameterBinding& b);

} // namespace systems::leal::campello_audio::pi
