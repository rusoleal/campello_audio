#pragma once

#include <vector_math/vector_math.hpp>

namespace systems::leal::campello_audio {

/// Convenience alias for the 3D vector type used throughout campello_audio.
using Vec3 = systems::leal::vector_math::Vector3<float>;

/// @brief World-space description of the audio listener used by the 3D engine.
///
/// Pass to AudioEngine::set3dListenerParameters() each frame (or whenever
/// the listener moves / rotates).
struct ListenerDescriptor {
    /// World-space position of the listener (camera / player head).
    Vec3 position = {0.0f, 0.0f,  0.0f};

    /// World-space velocity of the listener (used for Doppler effect).
    Vec3 velocity = {0.0f, 0.0f,  0.0f};

    /// Normalised forward direction the listener is facing.
    Vec3 forward  = {0.0f, 0.0f, -1.0f};

    /// Normalised up direction of the listener's head.
    Vec3 up       = {0.0f, 1.0f,  0.0f};
};

} // namespace systems::leal::campello_audio
