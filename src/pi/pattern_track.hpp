#pragma once

/// @file pattern_track.hpp
/// @brief Internal types for PatternTrack — stored in PatternTrack::native.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <campello_audio/constants/transition_rule.hpp>
#include "source_handle.hpp"
#include "music_track.hpp"
#include "pattern/pattern_bank.hpp"
#include "pattern/pattern_event.hpp"

namespace systems::leal::campello_audio::pi {

struct Pattern;
struct TransitionDef;

// ---------------------------------------------------------------------------
// PatternSectionEntry — one named pattern reference inside a PatternTrack.
// ---------------------------------------------------------------------------
struct PatternSectionEntry {
    std::string patternLabel;   ///< Key into PatternBank.
    std::string sectionLabel;   ///< User-facing name for transitions.
    const Pattern* pattern = nullptr; ///< Resolved at play time from PatternBank.
};

// ---------------------------------------------------------------------------
// PatternTrackData — full config + runtime state for one PatternTrack.
//
// Written by:
//   - Game thread: config fields (before play), pendingSection (after play,
//     under voiceMutex).
//   - Mixer thread: all runtime fields (currentSection, beatPos, …).
//
// The game-thread query methods (getCurrentSection, getBeat, …) read runtime
// fields with a benign race — acceptable for display-only purposes.
// ---------------------------------------------------------------------------
struct PatternTrackData {
    // ---- Config (set before play; read-only after) ----
    float    bpm         = 120.0f;
    uint32_t beatsPerBar = 4;
    uint32_t beatUnit    = 4;

    std::vector<PatternSectionEntry> sections;
    std::vector<TransitionDef>       transitions;

    std::shared_ptr<PatternBank> bank;

    // ---- Runtime (driven by mixer thread) ----
    bool    active          = false;
    float   volume          = 1.0f;
    int32_t currentSection  = 0;
    int32_t pendingSection  = -1;  ///< -1 = no transition queued.
    double  beatPos         = 0.0; ///< Beat position within the bar [0..beatsPerBar).
    double  totalBeatPos    = 0.0; ///< Absolute beats since start.

    // CrossFade state
    bool    crossFading      = false;
    int32_t crossFadeFrom    = -1;  ///< Outgoing section index during cross-fade.
    double  crossFadeFromPos = 0.0; ///< Outgoing section beat position.
    double  crossFadeElapsed = 0.0; ///< Seconds elapsed into the fade.
    double  crossFadeSecs    = 0.0; ///< Total fade duration in seconds.

    // Reusable query scratch buffer — avoids allocation in the audio thread.
    mutable std::vector<const PatternEvent*> queryScratch;

    // ---- Helpers ----

    /// Find a section index by label. Returns -1 if not found.
    int32_t findSection(const std::string& label) const;

    /// Find the transition definition from @p fromIdx → @p toIdx.
    /// Returns nullptr if no explicit rule exists (Immediate is used as default).
    const TransitionDef* findTransition(int32_t fromIdx, int32_t toIdx) const;
};

// ---------------------------------------------------------------------------
// PatternTrackHandle — AudioSourceHandle wrapper for PatternTrack.
// ---------------------------------------------------------------------------
struct PatternTrackHandle : public AudioSourceHandle {
    PatternTrackData data;
    PatternTrackHandle() { type = SourceType::PatternTrack; }
};

} // namespace systems::leal::campello_audio::pi
