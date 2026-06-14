/// @file pattern_track.cpp
/// @brief PatternTrack — public API implementation and PatternTrackData helpers.

#include <campello_audio/pattern_track.hpp>
#include "pattern/pattern_bank.hpp"
#include "pattern_track.hpp"
#include <algorithm>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// PatternTrackData helpers
// ---------------------------------------------------------------------------

int32_t PatternTrackData::findSection(const std::string& label) const {
    for (int32_t i = 0; i < static_cast<int32_t>(sections.size()); ++i) {
        if (sections[i].sectionLabel == label) return i;
    }
    return -1;
}

const TransitionDef* PatternTrackData::findTransition(int32_t fromIdx, int32_t toIdx) const {
    if (fromIdx < 0 || toIdx < 0) return nullptr;
    if (fromIdx >= static_cast<int32_t>(sections.size())) return nullptr;
    if (toIdx   >= static_cast<int32_t>(sections.size())) return nullptr;
    const std::string& fromLabel = sections[fromIdx].sectionLabel;
    const std::string& toLabel   = sections[toIdx].sectionLabel;
    for (const auto& t : transitions) {
        if (t.from == fromLabel && t.to == toLabel) return &t;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// PatternTrack — lifecycle
// ---------------------------------------------------------------------------

PatternTrack::PatternTrack() {
    native = new PatternTrackHandle();
}

PatternTrack::~PatternTrack() {
    delete static_cast<PatternTrackHandle*>(native);
    native = nullptr;
}

// ---------------------------------------------------------------------------
// Pattern bank
// ---------------------------------------------------------------------------

void PatternTrack::setPatternBank(std::shared_ptr<PatternBank> bank) {
    static_cast<PatternTrackHandle*>(native)->data.bank = std::move(bank);
}

// ---------------------------------------------------------------------------
// Section setup
// ---------------------------------------------------------------------------

void PatternTrack::addSection(const std::string& patternLabel,
                              const std::string& sectionLabel) {
    if (patternLabel.empty() || sectionLabel.empty()) return;
    auto& data = static_cast<PatternTrackHandle*>(native)->data;
    PatternSectionEntry e;
    e.patternLabel = patternLabel;
    e.sectionLabel = sectionLabel;
    data.sections.push_back(std::move(e));
}

void PatternTrack::addTransition(const std::string& from,
                                  const std::string& to,
                                  TransitionRule     rule,
                                  double             blendSecs) {
    auto& data = static_cast<PatternTrackHandle*>(native)->data;
    // Replace any existing rule for the same (from, to) pair.
    for (auto& t : data.transitions) {
        if (t.from == from && t.to == to) {
            t.rule      = rule;
            t.blendSecs = blendSecs;
            return;
        }
    }
    data.transitions.push_back({from, to, rule, blendSecs});
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void PatternTrack::setBpm(float bpm) {
    static_cast<PatternTrackHandle*>(native)->data.bpm =
        std::max(bpm, 0.1f);
}

void PatternTrack::setTimeSignature(uint32_t beatsPerBar, uint32_t beatUnit) {
    auto& data        = static_cast<PatternTrackHandle*>(native)->data;
    data.beatsPerBar  = std::max(beatsPerBar, 1u);
    data.beatUnit     = std::max(beatUnit,    1u);
}

float    PatternTrack::getBpm()         const {
    return static_cast<const PatternTrackHandle*>(native)->data.bpm;
}
uint32_t PatternTrack::getBeatsPerBar() const {
    return static_cast<const PatternTrackHandle*>(native)->data.beatsPerBar;
}
uint32_t PatternTrack::getBeatUnit()    const {
    return static_cast<const PatternTrackHandle*>(native)->data.beatUnit;
}

// ---------------------------------------------------------------------------
// Runtime state query (benign race — display-only)
// ---------------------------------------------------------------------------

std::string PatternTrack::getCurrentSection() const {
    const auto& data = static_cast<const PatternTrackHandle*>(native)->data;
    if (data.currentSection < 0 ||
        data.currentSection >= static_cast<int32_t>(data.sections.size()))
        return {};
    return data.sections[data.currentSection].sectionLabel;
}

std::string PatternTrack::getPendingSection() const {
    const auto& data = static_cast<const PatternTrackHandle*>(native)->data;
    if (data.pendingSection < 0 ||
        data.pendingSection >= static_cast<int32_t>(data.sections.size()))
        return {};
    return data.sections[data.pendingSection].sectionLabel;
}

float PatternTrack::getCurrentBeat() const {
    return static_cast<float>(
        static_cast<const PatternTrackHandle*>(native)->data.beatPos);
}

uint32_t PatternTrack::getSectionCount() const {
    return static_cast<uint32_t>(
        static_cast<const PatternTrackHandle*>(native)->data.sections.size());
}

std::string PatternTrack::getSectionLabel(uint32_t index) const {
    const auto& secs = static_cast<const PatternTrackHandle*>(native)->data.sections;
    if (index >= secs.size()) return {};
    return secs[index].sectionLabel;
}

uint32_t PatternTrack::getTransitionCount() const {
    return static_cast<uint32_t>(
        static_cast<const PatternTrackHandle*>(native)->data.transitions.size());
}
