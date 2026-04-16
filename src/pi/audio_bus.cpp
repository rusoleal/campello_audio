/// @file audio_bus.cpp
/// @brief AudioBus — platform-independent implementation.
///
/// AudioBus is a submix channel. It must be registered with an AudioEngine
/// via engine.play(bus) before child sources can be routed through it.
/// After registration the bus holds a back-pointer to MixerData so that
/// setVolume() and getVisualizationData() can reach the live BusSlot.

#include <campello_audio/audio_bus.hpp>
#include "source_handle.hpp"
#include "mixer.hpp"

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// AudioBus lifecycle
// ---------------------------------------------------------------------------

AudioBus::AudioBus() {
    native = new BusSourceHandle();
}

AudioBus::~AudioBus() {
    delete static_cast<BusSourceHandle*>(native);
    native = nullptr;
}

// ---------------------------------------------------------------------------
// Play a child source routed through this bus
// ---------------------------------------------------------------------------

SoundHandle AudioBus::play(AudioSource& source) {
    return play(source, PlayDescriptor{});
}

SoundHandle AudioBus::play(AudioSource& source, const PlayDescriptor& descriptor) {
    auto* bsh = static_cast<BusSourceHandle*>(native);
    if (!bsh || !bsh->playFn || bsh->busId == 0) return SoundHandle::invalid;
    // AudioBus is a friend of AudioSource, so we can access source.native here.
    const auto* sh = static_cast<const AudioSourceHandle*>(source.native);
    return bsh->playFn(sh, descriptor, bsh->busId);
}

// ---------------------------------------------------------------------------
// Bus volume
// ---------------------------------------------------------------------------

void AudioBus::setVolume(float volume) {
    auto* bsh = static_cast<BusSourceHandle*>(native);
    if (!bsh || !bsh->mixerData || bsh->busId == 0) return;
    auto* m = static_cast<MixerData*>(bsh->mixerData);
    std::lock_guard lk(m->voiceMutex);
    for (auto& bus : m->buses) {
        if (bus.busId == bsh->busId) { bus.volume = volume; break; }
    }
}

// ---------------------------------------------------------------------------
// Bus visualization
// ---------------------------------------------------------------------------

const float* AudioBus::getVisualizationData(uint32_t& sampleCount) const {
    const auto* bsh = static_cast<const BusSourceHandle*>(native);
    if (!bsh || !bsh->mixerData || bsh->busId == 0) { sampleCount = 0; return nullptr; }
    const auto* m = static_cast<const MixerData*>(bsh->mixerData);
    // No lock needed — vizBuffer is written from the audio thread and read here.
    // Benign data-race: acceptable for visualization use cases (same as MixerData::vizBuffer).
    for (const auto& bus : m->buses) {
        if (bus.busId == bsh->busId && bus.active && bus.vizEnabled) {
            sampleCount = static_cast<uint32_t>(bus.vizBuffer.size());
            return bus.vizBuffer.data();
        }
    }
    sampleCount = 0;
    return nullptr;
}
