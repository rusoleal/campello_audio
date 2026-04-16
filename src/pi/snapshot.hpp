#pragma once

/// @file snapshot.hpp
/// @brief Internal types for AudioSnapshot — stored in AudioSnapshot::native.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio { class AudioBus; }

namespace systems::leal::campello_audio::pi {

/// One bus-volume override stored inside a snapshot.
struct BusVolumeEntry {
    systems::leal::campello_audio::AudioBus* bus    = nullptr;
    float                                    volume = 1.0f;
};

/// One bus-filter override stored inside a snapshot.
struct BusFilterEntry {
    systems::leal::campello_audio::AudioBus*     bus    = nullptr;
    std::shared_ptr<systems::leal::campello_audio::Filter> filter;
    uint32_t                                     slot   = 0;
};

/// Internal representation of an AudioSnapshot — stored as AudioSnapshot::native.
struct AudioSnapshotData {
    std::string                name;
    uint32_t                   priority       = 0;
    bool                       hasGlobalVolume = false;
    float                      globalVolume   = 1.0f;
    std::vector<BusVolumeEntry> busVolumes;
    std::vector<BusFilterEntry> busFilters;
};

} // namespace systems::leal::campello_audio::pi
