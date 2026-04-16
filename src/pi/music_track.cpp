/// @file music_track.cpp
/// @brief MusicTrack — public API implementation and MusicTrackData helpers.

#include <campello_audio/music_track.hpp>
#include <campello_audio/audio_source.hpp>
#include "music_track.hpp"
#include <algorithm>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// MusicTrackData helpers
// ---------------------------------------------------------------------------

int32_t MusicTrackData::findSection(const std::string& label) const {
    for (int32_t i = 0; i < static_cast<int32_t>(sections.size()); ++i) {
        if (sections[i].label == label) return i;
    }
    return -1;
}

const TransitionDef* MusicTrackData::findTransition(int32_t fromIdx, int32_t toIdx) const {
    if (fromIdx < 0 || toIdx < 0) return nullptr;
    if (fromIdx >= static_cast<int32_t>(sections.size())) return nullptr;
    if (toIdx   >= static_cast<int32_t>(sections.size())) return nullptr;
    const std::string& fromLabel = sections[fromIdx].label;
    const std::string& toLabel   = sections[toIdx].label;
    for (const auto& t : transitions) {
        if (t.from == fromLabel && t.to == toLabel) return &t;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// MusicTrack — lifecycle
// ---------------------------------------------------------------------------

MusicTrack::MusicTrack() {
    native = new MusicTrackHandle();
}

MusicTrack::~MusicTrack() {
    delete static_cast<MusicTrackHandle*>(native);
    native = nullptr;
}

// ---------------------------------------------------------------------------
// Section setup
// ---------------------------------------------------------------------------

void MusicTrack::addSection(std::shared_ptr<AudioSource> audio,
                             const std::string& label) {
    if (!audio || label.empty()) return;
    auto& data = static_cast<MusicTrackHandle*>(native)->data;
    SectionEntry e;
    e.audio = std::move(audio);
    e.label = label;
    data.sections.push_back(std::move(e));
}

void MusicTrack::addTransition(const std::string& from,
                                const std::string& to,
                                TransitionRule     rule,
                                double             blendSecs) {
    auto& data = static_cast<MusicTrackHandle*>(native)->data;
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

void MusicTrack::setBpm(float bpm) {
    static_cast<MusicTrackHandle*>(native)->data.bpm =
        std::max(bpm, 0.1f);
}

void MusicTrack::setTimeSignature(uint32_t beatsPerBar, uint32_t beatUnit) {
    auto& data        = static_cast<MusicTrackHandle*>(native)->data;
    data.beatsPerBar  = std::max(beatsPerBar, 1u);
    data.beatUnit     = std::max(beatUnit,    1u);
}

float    MusicTrack::getBpm()         const {
    return static_cast<const MusicTrackHandle*>(native)->data.bpm;
}
uint32_t MusicTrack::getBeatsPerBar() const {
    return static_cast<const MusicTrackHandle*>(native)->data.beatsPerBar;
}
uint32_t MusicTrack::getBeatUnit()    const {
    return static_cast<const MusicTrackHandle*>(native)->data.beatUnit;
}

// ---------------------------------------------------------------------------
// Runtime state query (benign race — display-only)
// ---------------------------------------------------------------------------

std::string MusicTrack::getCurrentSection() const {
    const auto& data = static_cast<const MusicTrackHandle*>(native)->data;
    if (data.currentSection < 0 ||
        data.currentSection >= static_cast<int32_t>(data.sections.size()))
        return {};
    return data.sections[data.currentSection].label;
}

std::string MusicTrack::getPendingSection() const {
    const auto& data = static_cast<const MusicTrackHandle*>(native)->data;
    if (data.pendingSection < 0 ||
        data.pendingSection >= static_cast<int32_t>(data.sections.size()))
        return {};
    return data.sections[data.pendingSection].label;
}

float MusicTrack::getCurrentBeat() const {
    return static_cast<float>(
        static_cast<const MusicTrackHandle*>(native)->data.beatPos);
}

uint32_t MusicTrack::getSectionCount() const {
    return static_cast<uint32_t>(
        static_cast<const MusicTrackHandle*>(native)->data.sections.size());
}

std::string MusicTrack::getSectionLabel(uint32_t index) const {
    const auto& secs = static_cast<const MusicTrackHandle*>(native)->data.sections;
    if (index >= secs.size()) return {};
    return secs[index].label;
}

uint32_t MusicTrack::getTransitionCount() const {
    return static_cast<uint32_t>(
        static_cast<const MusicTrackHandle*>(native)->data.transitions.size());
}
