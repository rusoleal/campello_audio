#pragma once

namespace systems::leal::campello_audio {

/// @brief Distance-based volume attenuation model for 3D audio sources.
enum class AttenuationModel {
    /// No attenuation — volume is constant regardless of distance.
    None,
    /// Volume decreases linearly from min to max distance.
    Linear,
    /// Volume follows an inverse distance curve (physically accurate).
    Inverse,
    /// Volume follows an exponential decay curve.
    Exponential
};

} // namespace systems::leal::campello_audio
