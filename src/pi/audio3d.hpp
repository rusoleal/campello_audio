#pragma once

#include <campello_audio/constants/attenuation_model.hpp>
#include <vector_math/vector_math.hpp>

namespace systems::leal::campello_audio::pi {

using Vec3 = systems::leal::vector_math::Vector3<float>;

// ---------------------------------------------------------------------------
// 3D audio math — pure functions, no state.
//
// Called from AudioEngine::update3d() once per game frame for every active
// and virtual 3D voice.  Results are written back to the voice and consumed
// by mixSamples() on the audio thread.
// ---------------------------------------------------------------------------

/// @brief Compute the distance-attenuation gain for a 3D voice.
///
/// @param model     Attenuation curve to apply.
/// @param dist      Distance from listener to source (metres).
/// @param minDist   Distance at which full volume is heard.
/// @param maxDist   Distance at which volume reaches zero.
/// @param rolloff   Curve steepness multiplier.
/// @return          Gain in [0, 1].  Returns 1 for AttenuationModel::None.
float computeAttenuation(AttenuationModel model,
                          float dist,
                          float minDist,
                          float maxDist,
                          float rolloff);

/// @brief Compute the stereo pan for a 3D source relative to the listener.
///
/// Uses the listener's right axis (cross(forward, up)) to project the
/// source direction onto the left-right axis.
///
/// @return  Pan in [-1, +1].  0 = centre; -1 = full left; +1 = full right.
float computePan(const Vec3& sourcePos,
                  const Vec3& listenerPos,
                  const Vec3& listenerForward,
                  const Vec3& listenerUp);

/// @brief Compute the Doppler pitch shift for a moving source/listener pair.
///
/// Uses the standard acoustic formula:
///   pitch = (soundSpeed + dopplerFactor * vObserver)
///         / (soundSpeed - dopplerFactor * vSource)
///
/// Velocities are projected onto the source→listener axis, so vSource is
/// positive when the source moves toward the listener (raising pitch).
///
/// Velocities are projected onto the source→listener axis.
/// Result is clamped to [0.1, 10] to avoid degenerate values.
///
/// @param dopplerFactor  0 = no effect; 1 = physically accurate; >1 = exaggerated.
/// @return               Pitch multiplier applied on top of the voice's base pitch.
float computeDoppler(const Vec3& sourcePos,
                      const Vec3& sourceVel,
                      const Vec3& listenerPos,
                      const Vec3& listenerVel,
                      float soundSpeed,
                      float dopplerFactor);

} // namespace systems::leal::campello_audio::pi
