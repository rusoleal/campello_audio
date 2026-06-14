#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/constants/transition_rule.hpp>

namespace systems::leal::campello_audio {
namespace pi { class PatternBank; }
using PatternBank = pi::PatternBank;

/// @brief Interactive / adaptive music source driven by compiled pattern slots.
///
/// A PatternTrack is composed of named sections, where each section references
/// a compiled Pattern from a PatternBank. The game drives the soundtrack by
/// requesting transitions; the engine executes them at the musically correct
/// moment, spawning one-shot voices for pattern events.
///
/// @code
/// auto bank = std::make_shared<PatternBank>();
/// // ... load or compile patterns into the bank ...
///
/// auto track = std::make_shared<PatternTrack>();
/// track->setPatternBank(bank);
/// track->setBpm(128.0f);
/// track->setTimeSignature(4, 4);
///
/// track->addSection("explore_pat", "explore");
/// track->addSection("combat_pat",  "combat");
/// track->addSection("boss_pat",    "boss");
///
/// track->addTransition("explore", "combat", TransitionRule::OnBar,   0.5);
/// track->addTransition("combat",  "explore",TransitionRule::OnBar,   1.0);
/// track->addTransition("combat",  "boss",   TransitionRule::OnBeat,  0.0);
/// track->addTransition("boss",    "explore",TransitionRule::CrossFade,2.0);
///
/// engine.play(*track);
/// engine.requestMusicTransition("combat");   // request — happens at next bar
/// @endcode
class PatternTrack : public AudioSource {
public:
    PatternTrack();
    ~PatternTrack() override;

    // -----------------------------------------------------------------------
    // Pattern bank
    // -----------------------------------------------------------------------

    /// @brief Set the pattern bank that supplies compiled patterns for sections.
    void setPatternBank(std::shared_ptr<PatternBank> bank);

    // -----------------------------------------------------------------------
    // Section setup
    // -----------------------------------------------------------------------

    /// @brief Add a named section that references a pattern in the bank.
    /// @param patternLabel  Label of a Pattern in the assigned PatternBank.
    /// @param sectionLabel  Unique name used in transition rules.
    void addSection(const std::string& patternLabel, const std::string& sectionLabel);

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

    /// @brief Set the tempo in beats per minute.
    void setBpm(float bpm);

    /// @brief Set the time signature.
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
    std::string getPendingSection() const;

    /// @return Current beat position within the current bar [0..beatsPerBar).
    float getCurrentBeat() const;

    // -----------------------------------------------------------------------
    // Section / transition introspection
    // -----------------------------------------------------------------------

    /// @return Number of sections registered with addSection().
    uint32_t getSectionCount() const;

    /// @return Label of the section at @p index, or "" if out of range.
    std::string getSectionLabel(uint32_t index) const;

    /// @return Number of transitions registered with addTransition().
    uint32_t getTransitionCount() const;
};

} // namespace systems::leal::campello_audio
