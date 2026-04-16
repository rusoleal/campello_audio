/// @file audio3d.cpp
/// @brief 3D audio math — distance attenuation, stereo pan, and Doppler shift.
///
/// All functions are pure (no side effects, no state).  Called by
/// AudioEngine::update3d() once per game frame for every active 3D voice.

#include "audio3d.hpp"
#include <cmath>
#include <algorithm>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// computeAttenuation
// ---------------------------------------------------------------------------

float computeAttenuation(AttenuationModel model,
                          float dist,
                          float minDist,
                          float maxDist,
                          float rolloff) {
    if (model == AttenuationModel::None) return 1.0f;
    if (maxDist <= minDist)             return 1.0f; // degenerate range
    if (dist    <= minDist)             return 1.0f;
    if (dist    >= maxDist) {
        return (model == AttenuationModel::Linear) ? 0.0f : 0.0f;
    }

    switch (model) {
    case AttenuationModel::Linear: {
        // OpenAL-style linear:  gain = 1 - rolloff * (d - minDist) / (maxDist - minDist)
        const float t = (dist - minDist) / (maxDist - minDist);
        return std::clamp(1.0f - rolloff * t, 0.0f, 1.0f);
    }
    case AttenuationModel::Inverse: {
        // OpenAL inverse distance:  gain = minDist / (minDist + rolloff * (d - minDist))
        const float denom = minDist + rolloff * (dist - minDist);
        if (denom < 0.0001f) return 1.0f;
        return std::clamp(minDist / denom, 0.0f, 1.0f);
    }
    case AttenuationModel::Exponential: {
        // gain = (d / minDist) ^ (-rolloff) = pow(minDist / d, rolloff)
        return std::clamp(std::pow(minDist / dist, rolloff), 0.0f, 1.0f);
    }
    default:
        return 1.0f;
    }
}

// ---------------------------------------------------------------------------
// computePan
// ---------------------------------------------------------------------------

float computePan(const Vec3& sourcePos,
                  const Vec3& listenerPos,
                  const Vec3& listenerForward,
                  const Vec3& listenerUp) {
    // Source-to-listener direction
    const float dx = sourcePos.x() - listenerPos.x();
    const float dy = sourcePos.y() - listenerPos.y();
    const float dz = sourcePos.z() - listenerPos.z();
    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dist < 0.0001f) return 0.0f;

    // Normalised source direction
    const float sdx = dx / dist;
    const float sdy = dy / dist;
    const float sdz = dz / dist;

    // Listener right axis = cross(forward, up)
    const float rx = listenerForward.y() * listenerUp.z() - listenerForward.z() * listenerUp.y();
    const float ry = listenerForward.z() * listenerUp.x() - listenerForward.x() * listenerUp.z();
    const float rz = listenerForward.x() * listenerUp.y() - listenerForward.y() * listenerUp.x();
    const float rlen = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (rlen < 0.0001f) return 0.0f;

    // Normalised right axis
    const float rnx = rx / rlen;
    const float rny = ry / rlen;
    const float rnz = rz / rlen;

    // Pan = dot(sourceDir, rightAxis)  →  [-1, +1]
    const float pan = sdx*rnx + sdy*rny + sdz*rnz;
    return std::clamp(pan, -1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// computeDoppler
// ---------------------------------------------------------------------------

float computeDoppler(const Vec3& sourcePos,
                      const Vec3& sourceVel,
                      const Vec3& listenerPos,
                      const Vec3& listenerVel,
                      float soundSpeed,
                      float dopplerFactor) {
    if (dopplerFactor <= 0.0f) return 1.0f;

    // Source → listener axis
    const float dx = listenerPos.x() - sourcePos.x();
    const float dy = listenerPos.y() - sourcePos.y();
    const float dz = listenerPos.z() - sourcePos.z();
    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dist < 0.0001f) return 1.0f;

    const float ax = dx / dist;
    const float ay = dy / dist;
    const float az = dz / dist;

    // Project velocities onto source→listener axis (positive = moving toward)
    const float vObserver = listenerVel.x()*ax + listenerVel.y()*ay + listenerVel.z()*az;
    const float vSource   = sourceVel.x()*ax   + sourceVel.y()*ay   + sourceVel.z()*az;

    // Standard acoustic Doppler formula.
    // vObserver: positive when listener moves toward source (standard convention).
    // vSource:   positive when source moves toward listener; this reduces the
    //            denominator (shorter wavelength → higher pitch).
    //   pitch = (v_sound + v_observer) / (v_sound - v_source)
    const float denom = soundSpeed - dopplerFactor * vSource;
    if (std::abs(denom) < 0.0001f) return 1.0f;

    const float pitch = (soundSpeed + dopplerFactor * vObserver) / denom;
    return std::clamp(pitch, 0.1f, 10.0f);
}

} // namespace systems::leal::campello_audio::pi
