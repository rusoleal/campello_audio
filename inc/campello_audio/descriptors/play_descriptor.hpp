#pragma once

#include <vector_math/vector_math.hpp>

namespace systems::leal::campello_audio {

class AudioBus;

/// Convenience alias for the 3D vector type used throughout campello_audio.
using Vec3 = systems::leal::vector_math::Vector3<float>;

/// @brief Per-voice parameters supplied to AudioEngine::play().
///
/// All fields have sensible defaults so callers only need to set the
/// properties they care about.
struct PlayDescriptor {
    /// Initial volume [0..1]. Default 1.0 (full).
    float volume    = 1.0f;

    /// Stereo pan [-1..1]. -1 = full left, 0 = centre, 1 = full right.
    float pan       = 0.0f;

    /// Pitch / playback speed multiplier. 1.0 = normal, 2.0 = one octave up.
    float pitch     = 1.0f;

    /// Whether to loop the source when it reaches the end.
    bool  looping   = false;

    /// Start in paused state.
    bool  paused    = false;

    /// Protect this voice from being killed when maxVoices is exceeded.
    bool  protect   = false;

    /// Route output through this bus. nullptr = route directly to the main mix.
    AudioBus* bus   = nullptr;

    // ---- 3D audio (ignored unless enable3d is true) ----

    /// Enable 3D positional audio for this voice.
    bool  enable3d   = false;

    /// World-space position of the audio source.
    Vec3  position   = {0.0f, 0.0f, 0.0f};

    /// World-space velocity of the audio source (used for Doppler).
    Vec3  velocity   = {0.0f, 0.0f, 0.0f};

    /// Minimum distance at which full volume is heard.
    float minDistance = 1.0f;

    /// Distance beyond which volume no longer attenuates.
    float maxDistance = 1000.0f;

    /// Rolloff factor for the chosen attenuation model.
    float rolloff     = 1.0f;

    /// Doppler factor. 0 disables Doppler; 1 is physically accurate.
    float dopplerFactor = 1.0f;
};

} // namespace systems::leal::campello_audio
