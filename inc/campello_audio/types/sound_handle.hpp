#pragma once

#include <cstdint>

namespace systems::leal::campello_audio {

/// @brief Opaque handle referencing a playing voice.
///
/// Returned by AudioEngine::play(). May be stored and used to control the
/// voice (volume, pan, stop, seek, etc.). The handle becomes invalid once the
/// voice finishes or is explicitly stopped; use AudioEngine::isValid() to
/// check liveness before use.
///
/// Ignoring the handle is safe — this enables "fire and forget" playback.
struct SoundHandle {
    uint64_t id = 0;

    /// Returns true if the handle was obtained from a successful play() call.
    bool isValid() const { return id != 0; }

    bool operator==(const SoundHandle& o) const { return id == o.id; }
    bool operator!=(const SoundHandle& o) const { return id != o.id; }

    /// Sentinel representing an invalid / unset handle.
    static const SoundHandle invalid;
};

inline const SoundHandle SoundHandle::invalid{0};

} // namespace systems::leal::campello_audio
