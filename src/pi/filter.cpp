/// @file filter.cpp
/// @brief Filter base class and derived filter implementations.
///
/// DSP logic lives in filter_engine.cpp — this file owns the FilterData
/// lifecycle (allocation in constructors, deletion in destructors) and
/// forwards setParam / fadeParam / oscillateParam to FilterData.

#include <campello_audio/filter.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <campello_audio/high_pass_filter.hpp>
#include <campello_audio/echo_filter.hpp>
#include <campello_audio/reverb_filter.hpp>
#include <campello_audio/compressor_filter.hpp>
#include <campello_audio/limiter_filter.hpp>
#include <campello_audio/chorus_filter.hpp>
#include <campello_audio/flanger_filter.hpp>
#include <campello_audio/pitch_shift_filter.hpp>
#include "filter_engine.hpp"

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// Filter base
// ---------------------------------------------------------------------------

Filter::Filter()
    : native(new FilterData()) {}

Filter::~Filter() {
    delete static_cast<FilterData*>(native);
    native = nullptr;
}

void Filter::setParam(uint32_t paramId, float value) {
    auto* d = static_cast<FilterData*>(native);
    if (!d || paramId >= FilterData::kMaxParams) return;
    ParamAuto& p  = d->params[paramId];
    p.value       = value;
    p.fadeActive  = false;
    p.lfoActive   = false;
}

void Filter::fadeParam(uint32_t paramId, float to, double timeSecs) {
    auto* d = static_cast<FilterData*>(native);
    if (!d || paramId >= FilterData::kMaxParams) return;
    ParamAuto& p   = d->params[paramId];
    p.fadeFrom     = p.value;
    p.fadeTo       = to;
    p.fadeTime     = timeSecs;
    p.fadeElapsed  = 0.0;
    p.fadeActive   = true;
    p.lfoActive    = false;  // fade cancels LFO
}

void Filter::oscillateParam(uint32_t paramId, float from, float to,
                             double periodSecs) {
    auto* d = static_cast<FilterData*>(native);
    if (!d || paramId >= FilterData::kMaxParams) return;
    ParamAuto& p  = d->params[paramId];
    p.lfoFrom     = from;
    p.lfoTo       = to;
    p.lfoPeriod   = periodSecs;
    p.lfoPhase    = 0.0;
    p.lfoActive   = true;
    p.fadeActive  = false;  // LFO cancels fade
}

// ---------------------------------------------------------------------------
// LowPassFilter
// ---------------------------------------------------------------------------

LowPassFilter::LowPassFilter(float cutoffHz, float resonance) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::LowPass;
    d->params[0].value = cutoffHz;
    d->params[1].value = resonance;
}
LowPassFilter::~LowPassFilter() = default;

// ---------------------------------------------------------------------------
// HighPassFilter
// ---------------------------------------------------------------------------

HighPassFilter::HighPassFilter(float cutoffHz, float resonance) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::HighPass;
    d->params[0].value = cutoffHz;
    d->params[1].value = resonance;
}
HighPassFilter::~HighPassFilter() = default;

// ---------------------------------------------------------------------------
// EchoFilter
// ---------------------------------------------------------------------------

EchoFilter::EchoFilter(float delaySecs, float decay, float filterVal) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Echo;
    d->params[0].value = delaySecs;
    d->params[1].value = decay;
    d->params[2].value = filterVal;
}
EchoFilter::~EchoFilter() = default;

// ---------------------------------------------------------------------------
// ReverbFilter
// ---------------------------------------------------------------------------

ReverbFilter::ReverbFilter(float roomSize, float damping, float wet, float dry) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Reverb;
    d->params[0].value = roomSize;
    d->params[1].value = damping;
    d->params[2].value = wet;
    d->params[3].value = dry;
    d->params[4].value = 1.0f;   // width default
}
ReverbFilter::~ReverbFilter() = default;

// ---------------------------------------------------------------------------
// CompressorFilter
// ---------------------------------------------------------------------------

CompressorFilter::CompressorFilter(float thresholdDb, float ratio,
                                    float attackMs,    float releaseMs,
                                    float kneeDb,      float makeupGainDb) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Compressor;
    d->params[0].value = thresholdDb;
    d->params[1].value = ratio;
    d->params[2].value = attackMs;
    d->params[3].value = releaseMs;
    d->params[4].value = kneeDb;
    d->params[5].value = makeupGainDb;
}
CompressorFilter::~CompressorFilter() = default;

float CompressorFilter::getGainReductionDb() const {
    const auto* d = static_cast<const FilterData*>(native);
    return d ? d->gainReductionDb : 0.0f;
}

// ---------------------------------------------------------------------------
// LimiterFilter
// ---------------------------------------------------------------------------

LimiterFilter::LimiterFilter(float ceilingDb, float lookaheadMs, float releaseMs) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Limiter;
    d->params[0].value = ceilingDb;
    d->params[1].value = lookaheadMs;
    d->params[2].value = releaseMs;
}
LimiterFilter::~LimiterFilter() = default;

float LimiterFilter::getGainReductionDb() const {
    const auto* d = static_cast<const FilterData*>(native);
    return d ? d->gainReductionDb : 0.0f;
}

// ---------------------------------------------------------------------------
// ChorusFilter
// ---------------------------------------------------------------------------

ChorusFilter::ChorusFilter(float depth, float rateHz, float wet) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Chorus;
    d->params[0].value = depth;
    d->params[1].value = rateHz;
    d->params[2].value = wet;
    d->params[3].value = 1.0f;   // dry default
    d->params[4].value = 3.0f;   // voices default
}
ChorusFilter::~ChorusFilter() = default;

// ---------------------------------------------------------------------------
// FlangerFilter
// ---------------------------------------------------------------------------

FlangerFilter::FlangerFilter(float depth, float rateHz, float feedback) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::Flanger;
    d->params[0].value = depth;
    d->params[1].value = rateHz;
    d->params[2].value = feedback;
    d->params[3].value = 0.5f;   // wet default
    d->params[4].value = 1.0f;   // dry default
}
FlangerFilter::~FlangerFilter() = default;

// ---------------------------------------------------------------------------
// PitchShiftFilter
// ---------------------------------------------------------------------------

PitchShiftFilter::PitchShiftFilter(float semitones) {
    auto* d            = static_cast<FilterData*>(native);
    d->type            = FilterType::PitchShift;
    d->params[0].value = semitones;
    d->params[1].value = 1.0f;   // quality default (medium)
}
PitchShiftFilter::~PitchShiftFilter() = default;
