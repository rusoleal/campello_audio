#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

class AudioBus;

/// @brief A named set of bus volumes and filters that can be blended in/out.
///
/// Snapshots capture a complete mix state — useful for global audio moods
/// like "combat", "underwater", "slow motion", or "main menu". Transitioning
/// between snapshots smoothly blends all their properties over time.
///
/// @code
/// auto underwater = std::make_shared<AudioSnapshot>("underwater");
/// underwater->setGlobalVolume(0.6f);
/// underwater->setBusVolume(&sfxBus,   0.4f);
/// underwater->setBusVolume(&musicBus, 0.8f);
/// underwater->setBusFilter(&sfxBus, std::make_shared<LowPassFilter>(400.0f), 0);
/// engine.registerSnapshot(underwater);
///
/// // Apply over 1.5 seconds when the player dives
/// engine.applySnapshot("underwater", 1.5);
///
/// // Revert to baseline over 1.0 second on surfacing
/// engine.revertSnapshot(1.0);
/// @endcode
class AudioSnapshot {
    friend class AudioEngine;

public:
    explicit AudioSnapshot(const std::string& name);
    ~AudioSnapshot();

    const std::string& getName() const;

    /// @brief Set the master volume for this snapshot [0..1].
    void setGlobalVolume(float volume);

    /// @brief Set the volume of a specific bus [0..1].
    void setBusVolume(AudioBus* bus, float volume);

    /// @brief Attach a filter to a bus slot within this snapshot.
    /// When the snapshot is reverted, the bus's original filter is restored.
    void setBusFilter(AudioBus* bus, std::shared_ptr<Filter> filter, uint32_t slot);

    /// @brief Set the priority of this snapshot.
    /// When two snapshots are active, the one with higher priority wins.
    void setPriority(uint32_t priority);
    uint32_t getPriority() const;

private:
    void* native = nullptr;
};

} // namespace systems::leal::campello_audio
