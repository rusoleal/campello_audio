#pragma once

namespace systems::leal::campello_audio {

/// @brief Resampling quality used when the source and output sample rates differ.
enum class ResampleQuality {
    /// Nearest-sample (lowest CPU cost, audible artefacts at low ratios).
    Point,
    /// Linear interpolation (good balance of quality and cost).
    Linear,
    /// Catmull-Rom cubic spline (highest quality, highest CPU cost).
    CatmullRom
};

} // namespace systems::leal::campello_audio
