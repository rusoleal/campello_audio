#pragma once

/// @file music_track.hpp
/// @brief Internal types for MusicTrack — stored in MusicTrack::native.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <campello_audio/constants/transition_rule.hpp>
#include "source_handle.hpp"

namespace systems::leal::campello_audio { class AudioSource; }

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// SectionEntry — one named looping audio layer inside a MusicTrack.
// ---------------------------------------------------------------------------
struct SectionEntry {
    std::shared_ptr<systems::leal::campello_audio::AudioSource> audio;
    std::string                                                  label;
    const DecodedBuffer*                                         pcm = nullptr; ///< Resolved at play time.
};

// ---------------------------------------------------------------------------
// TransitionDef — a rule that describes how to move between two sections.
// ---------------------------------------------------------------------------
struct TransitionDef {
    std::string    from;
    std::string    to;
    systems::leal::campello_audio::TransitionRule rule =
        systems::leal::campello_audio::TransitionRule::Immediate;
    double         blendSecs = 0.0;
};

// ---------------------------------------------------------------------------
// MusicTrackData — full config + runtime state for one MusicTrack.
//
// Written by:
//   - Game thread: config fields (before play), pendingSection (after play,
//     under voiceMutex).
//   - Mixer thread: all runtime fields (currentSection, readPos, beatPos, …).
//
// The game-thread query methods (getCurrentSection, getBeat, …) read runtime
// fields with a benign race — acceptable for display-only purposes.
// ---------------------------------------------------------------------------
struct MusicTrackData {
    // ---- Config (set before play; read-only after) ----
    float    bpm         = 120.0f;
    uint32_t beatsPerBar = 4;
    uint32_t beatUnit    = 4;

    std::vector<SectionEntry>  sections;
    std::vector<TransitionDef> transitions;

    // ---- Runtime (driven by mixer thread) ----
    bool    active          = false;
    float   volume          = 1.0f;
    int32_t currentSection  = 0;
    int32_t pendingSection  = -1;  ///< -1 = no transition queued.
    double  readPos         = 0.0; ///< Frame index into current section's PCM.
    double  beatPos         = 0.0; ///< Beat position within the bar [0..beatsPerBar).

    // CrossFade state
    bool    crossFading      = false;
    int32_t crossFadeFrom    = -1;  ///< Outgoing section index during cross-fade.
    double  crossFadeFromPos = 0.0; ///< Outgoing section read position.
    double  crossFadeElapsed = 0.0; ///< Seconds elapsed into the fade.
    double  crossFadeSecs    = 0.0; ///< Total fade duration in seconds.

    // ---- Helpers ----

    /// Find a section index by label. Returns -1 if not found.
    int32_t findSection(const std::string& label) const;

    /// Find the transition definition from @p fromIdx → @p toIdx.
    /// Returns nullptr if no explicit rule exists (Immediate is used as default).
    const TransitionDef* findTransition(int32_t fromIdx, int32_t toIdx) const;
};

// ---------------------------------------------------------------------------
// MusicTrackHandle — AudioSourceHandle wrapper for MusicTrack.
// ---------------------------------------------------------------------------
struct MusicTrackHandle : public AudioSourceHandle {
    MusicTrackData data;
    MusicTrackHandle() { type = SourceType::Music; }
};

} // namespace systems::leal::campello_audio::pi
