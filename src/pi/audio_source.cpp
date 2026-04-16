/// @file audio_source.cpp
/// @brief AudioSource base class — common state management.

#include <campello_audio/audio_source.hpp>
#include "source_handle.hpp"
#include <algorithm>

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::AudioSourceHandle;

// AudioSource does NOT allocate native here — each concrete subclass owns the
// allocation (as a larger derived handle struct). AudioSource just provides
// the common accessors that cast native to AudioSourceHandle*.

AudioSource::AudioSource()  = default;
AudioSource::~AudioSource() = default;

void AudioSource::setVolume(float volume) {
    if (!native) return;
    static_cast<Handle*>(native)->volume = volume;
}

float AudioSource::getVolume() const {
    if (!native) return 1.0f;
    return static_cast<Handle*>(native)->volume;
}

void AudioSource::setLooping(bool loop) {
    if (!native) return;
    static_cast<Handle*>(native)->looping = loop;
}

void AudioSource::setLoopMode(LoopMode mode) {
    if (!native) return;
    static_cast<Handle*>(native)->loopMode = mode;
}

void AudioSource::setSingleInstance(bool single) {
    if (!native) return;
    static_cast<Handle*>(native)->singleInstance = single;
}

void AudioSource::addFilter(std::shared_ptr<Filter> filter, uint32_t slot) {
    if (!native || slot >= Filter::kMaxFiltersPerSource) return;
    static_cast<Handle*>(native)->filters[slot] = std::move(filter);
}

void AudioSource::removeFilter(uint32_t slot) {
    if (!native || slot >= Filter::kMaxFiltersPerSource) return;
    static_cast<Handle*>(native)->filters[slot].reset();
}

void AudioSource::bindParameter(const std::string&  parameterName,
                                AudioSourceProperty property,
                                CurveType           curve,
                                float               outputMin,
                                float               outputMax,
                                uint32_t            filterSlot,
                                uint32_t            filterParamId) {
    if (!native) return;
    auto* h = static_cast<Handle*>(native);

    // Replace any existing binding for the same parameter name.
    for (auto& b : h->bindings) {
        if (b.paramName == parameterName) {
            b.property      = property;
            b.curve         = curve;
            b.outputMin     = outputMin;
            b.outputMax     = outputMax;
            b.filterSlot    = filterSlot;
            b.filterParamId = filterParamId;
            return;
        }
    }

    pi::ParameterBinding binding;
    binding.paramName    = parameterName;
    binding.property     = property;
    binding.curve        = curve;
    binding.outputMin    = outputMin;
    binding.outputMax    = outputMax;
    binding.filterSlot   = filterSlot;
    binding.filterParamId = filterParamId;
    h->bindings.push_back(std::move(binding));
}

void AudioSource::unbindParameter(const std::string& parameterName) {
    if (!native) return;
    auto* h = static_cast<Handle*>(native);
    auto& b = h->bindings;
    b.erase(std::remove_if(b.begin(), b.end(),
                [&](const pi::ParameterBinding& binding) {
                    return binding.paramName == parameterName;
                }),
            b.end());
}
