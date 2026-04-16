/// @file rtpc.cpp
/// @brief RTPC — AudioParameter implementation + curve / binding evaluation.

#include <campello_audio/audio_parameter.hpp>
#include "rtpc.hpp"
#include <algorithm>
#include <cmath>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// AudioParameter
// ---------------------------------------------------------------------------

AudioParameter::AudioParameter(const std::string& name,
                               float minValue,
                               float maxValue) {
    auto* d    = new AudioParameterData();
    d->name    = name;
    d->value   = minValue;
    d->minValue = minValue;
    d->maxValue = maxValue;
    native = d;
}

AudioParameter::~AudioParameter() {
    delete static_cast<AudioParameterData*>(native);
    native = nullptr;
}

const std::string& AudioParameter::getName() const {
    return static_cast<const AudioParameterData*>(native)->name;
}

float AudioParameter::getValue() const {
    return static_cast<const AudioParameterData*>(native)->value;
}

float AudioParameter::getMinValue() const {
    return static_cast<const AudioParameterData*>(native)->minValue;
}

float AudioParameter::getMaxValue() const {
    return static_cast<const AudioParameterData*>(native)->maxValue;
}

void AudioParameter::setValue(float value) {
    auto* d = static_cast<AudioParameterData*>(native);
    d->value = std::clamp(value, d->minValue, d->maxValue);
}

// ---------------------------------------------------------------------------
// Curve evaluation
// ---------------------------------------------------------------------------

float pi::evaluateCurve(CurveType curve, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (curve) {
        case CurveType::Linear:      return t;
        case CurveType::Exponential: return t * t;
        case CurveType::Logarithmic: return std::sqrt(t);
        case CurveType::SCurve:      return t * t * (3.0f - 2.0f * t);
        case CurveType::Sine:        return std::sin(t * 3.14159265358979323846f * 0.5f);
    }
    return t; // unreachable
}

// ---------------------------------------------------------------------------
// Binding application
// ---------------------------------------------------------------------------

float pi::applyBinding(float paramValue, float paramMin, float paramMax,
                       const ParameterBinding& b) {
    // Normalise parameter into [0, 1].
    const float range = paramMax - paramMin;
    const float t = (range == 0.0f)
                    ? 0.0f
                    : std::clamp((paramValue - paramMin) / range, 0.0f, 1.0f);

    const float curved = evaluateCurve(b.curve, t);
    return b.outputMin + curved * (b.outputMax - b.outputMin);
}
