#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/constants/transition_rule.hpp>

namespace systems::leal::campello_audio {

/// @brief Interactive / adaptive music source with beat-aware section transitions.
///
/// A MusicTrack is composed of named sections (loops) and a transition table
/// that defines how and when to move between them. The game drives the
/// soundtrack by requesting transitions; the engine executes them at the
/// musically correct moment.
///
/// @code
/// auto track = std::make_shared<MusicTrack>();
/// track->setBpm(120.0f);
/// track->setTimeSignature(4, 4);
///
/// auto explore = std::make_shared<AudioStream>(); explore->open("explore.ogg");
/// auto combat  = std::make_shared<AudioStream>(); combat->open("combat.ogg");
/// auto boss    = std::make_shared<AudioStream>(); boss->open("boss.ogg");
///
/// track->addSection(explore, "explore");
/// track->addSection(combat,  "combat");
/// track->addSection(boss,    "boss");
///
/// track->addTransition("explore", "combat", TransitionRule::OnBar,   0.5);
/// track->addTransition("combat",  "explore",TransitionRule::OnBar,   1.0);
/// track->addTransition("combat",  "boss",   TransitionRule::OnBeat,  0.0);
/// track->addTransition("boss",    "explore",TransitionRule::CrossFade,2.0);
///
/// engine.play(*track);
/// engine.requestMusicTransition("combat");   // request — happens at next bar
/// @endcode
class MusicTrack : public AudioSource {
public:
    MusicTrack();
    ~MusicTrack() override;

    // -----------------------------------------------------------------------
    // Section setup
    // -----------------------------------------------------------------------

    /// @brief Add a named section (looping audio layer).
    /// The first section added becomes the initial playback state.
    /// @param audio  The audio source to loop for this section.
    /// @param label  Unique name used in transition rules.
    void addSection(std::shared_ptr<AudioSource> audio, const std::string& label);

    /// @brief Define a transition between two sections.
    /// @param from       Source section label.
    /// @param to         Target section label.
    /// @param rule       When the transition executes.
    /// @param blendSecs  Cross-fade duration in seconds (used by CrossFade rule).
    void addTransition(const std::string& from,
                       const std::string& to,
                       TransitionRule     rule,
                       double             blendSecs = 0.0);

    // -----------------------------------------------------------------------
    // Timing
    // -----------------------------------------------------------------------

    /// @brief Set the tempo in beats per minute (needed for OnBeat / OnBar transitions).
    void setBpm(float bpm);

    /// @brief Set the time signature.
    /// @param beatsPerBar  Number of beats in one bar (e.g. 4 for 4/4 time).
    /// @param beatUnit     Note value of one beat (e.g. 4 = quarter note).
    void setTimeSignature(uint32_t beatsPerBar, uint32_t beatUnit = 4);

    float    getBpm()          const;
    uint32_t getBeatsPerBar()  const;
    uint32_t getBeatUnit()     const;

    // -----------------------------------------------------------------------
    // Runtime state query
    // -----------------------------------------------------------------------

    /// @return Label of the currently playing section.
    std::string getCurrentSection() const;

    /// @return Label of the pending section (transition queued but not yet executed).
    ///         Empty string if no transition is pending.
    std::string getPendingSection() const;

    /// @return Current beat position within the current bar [0..beatsPerBar).
    float getCurrentBeat() const;

    // -----------------------------------------------------------------------
    // Section / transition introspection (useful for debugging)
    // -----------------------------------------------------------------------

    /// @return Number of sections registered with addSection().
    uint32_t getSectionCount() const;

    /// @return Label of the section at @p index, or "" if out of range.
    std::string getSectionLabel(uint32_t index) const;

    /// @return Number of transitions registered with addTransition().
    uint32_t getTransitionCount() const;
};

} // namespace systems::leal::campello_audio
