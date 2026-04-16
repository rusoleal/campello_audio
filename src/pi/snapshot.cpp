/// @file snapshot.cpp
/// @brief AudioSnapshot — named mix state (volumes + filter overrides).

#include <campello_audio/audio_snapshot.hpp>
#include <campello_audio/audio_bus.hpp>
#include "snapshot.hpp"

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

AudioSnapshot::AudioSnapshot(const std::string& name) {
    auto* d  = new AudioSnapshotData();
    d->name  = name;
    native   = d;
}

AudioSnapshot::~AudioSnapshot() {
    delete static_cast<AudioSnapshotData*>(native);
    native = nullptr;
}

const std::string& AudioSnapshot::getName() const {
    return static_cast<const AudioSnapshotData*>(native)->name;
}

void AudioSnapshot::setGlobalVolume(float volume) {
    auto* d          = static_cast<AudioSnapshotData*>(native);
    d->globalVolume  = volume;
    d->hasGlobalVolume = true;
}

void AudioSnapshot::setBusVolume(AudioBus* bus, float volume) {
    if (!bus) return;
    auto* d = static_cast<AudioSnapshotData*>(native);
    for (auto& e : d->busVolumes) {
        if (e.bus == bus) { e.volume = volume; return; }
    }
    d->busVolumes.push_back({bus, volume});
}

void AudioSnapshot::setBusFilter(AudioBus* bus,
                                  std::shared_ptr<Filter> filter, uint32_t slot) {
    if (!bus) return;
    auto* d = static_cast<AudioSnapshotData*>(native);
    for (auto& e : d->busFilters) {
        if (e.bus == bus && e.slot == slot) { e.filter = std::move(filter); return; }
    }
    d->busFilters.push_back({bus, std::move(filter), slot});
}

void AudioSnapshot::setPriority(uint32_t priority) {
    static_cast<AudioSnapshotData*>(native)->priority = priority;
}

uint32_t AudioSnapshot::getPriority() const {
    return static_cast<const AudioSnapshotData*>(native)->priority;
}
