#pragma once

#include <vector>
#include "pattern_event.hpp"

namespace systems::leal::campello_audio::pi {

/// @brief Immutable, queryable pattern timeline.
///
/// Produced by the PatternCompiler (editor/tooling). The runtime queries
/// it from the audio thread to find events that should fire in a given
/// beat interval.
class Pattern {
public:
    /// Length of one pattern cycle in beats (e.g. 4.0 for one 4/4 bar).
    double lengthInBeats = 4.0;

    /// Sorted by beat position. Events are expected to lie in [0, lengthInBeats).
    std::vector<PatternEvent> events;

    /// @brief Return all events whose [beat, beat+duration) overlaps [from, to).
    ///
    /// Lock-free and allocation-free — called from the audio thread.
    /// @p out is cleared and filled with pointers into this Pattern's events.
    void query(double fromBeat, double toBeat,
               std::vector<const PatternEvent*>& out) const;

    /// @brief Return all events whose [beat, beat+duration) overlaps [from, to),
    ///        wrapping around the pattern cycle.
    ///
    /// If fromBeat < 0 or toBeat > lengthInBeats, the query wraps around.
    void queryWrapped(double fromBeat, double toBeat,
                      std::vector<const PatternEvent*>& out) const;
};

} // namespace systems::leal::campello_audio::pi
