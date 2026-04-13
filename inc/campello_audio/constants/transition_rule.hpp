#pragma once

namespace systems::leal::campello_audio {

/// @brief Controls when a MusicTrack switches from one section to another.
enum class TransitionRule {
    /// Switch immediately on the next audio buffer boundary.
    Immediate,
    /// Wait until the next beat (requires BPM set on the MusicTrack).
    OnBeat,
    /// Wait until the next bar/measure boundary.
    OnBar,
    /// Let the current section finish before entering the next.
    OnNextSection,
    /// Cross-fade out of the current section and into the next.
    CrossFade
};

} // namespace systems::leal::campello_audio
