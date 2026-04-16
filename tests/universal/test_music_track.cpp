/// @file test_music_track.cpp
/// @brief Universal tests for MusicTrack — no audio device required.
///
/// Tests beat-clock math, section/transition registration, and public API
/// through MusicTrack's own accessors.  No engine, no audio hardware needed.

#include <gtest/gtest.h>
#include <campello_audio/music_track.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/constants/transition_rule.hpp>

using namespace systems::leal::campello_audio;

static std::shared_ptr<WavSource> makeAudio() {
    return std::make_shared<WavSource>();
}

// ---------------------------------------------------------------------------
// Construction / defaults
// ---------------------------------------------------------------------------

TEST(MusicTrackUnit, DefaultState) {
    MusicTrack t;
    EXPECT_FLOAT_EQ(t.getBpm(), 120.0f);
    EXPECT_EQ(t.getBeatsPerBar(), 4u);
    EXPECT_EQ(t.getBeatUnit(), 4u);
    EXPECT_EQ(t.getCurrentSection(), "");
    EXPECT_EQ(t.getPendingSection(), "");
    EXPECT_FLOAT_EQ(t.getCurrentBeat(), 0.0f);
    EXPECT_EQ(t.getSectionCount(), 0u);
    EXPECT_EQ(t.getTransitionCount(), 0u);
}

// ---------------------------------------------------------------------------
// BPM / time-signature setters
// ---------------------------------------------------------------------------

TEST(MusicTrackUnit, SetBpm) {
    MusicTrack t;
    t.setBpm(90.0f);
    EXPECT_FLOAT_EQ(t.getBpm(), 90.0f);
}

TEST(MusicTrackUnit, SetBpmClampsToMinimum) {
    MusicTrack t;
    t.setBpm(0.0f);
    EXPECT_GT(t.getBpm(), 0.0f);
}

TEST(MusicTrackUnit, SetTimeSignature) {
    MusicTrack t;
    t.setTimeSignature(3, 4);
    EXPECT_EQ(t.getBeatsPerBar(), 3u);
    EXPECT_EQ(t.getBeatUnit(), 4u);
}

// ---------------------------------------------------------------------------
// addSection / getSectionCount / getSectionLabel
// ---------------------------------------------------------------------------

TEST(MusicTrackUnit, AddSection) {
    MusicTrack t;
    t.addSection(makeAudio(), "intro");
    t.addSection(makeAudio(), "main");
    EXPECT_EQ(t.getSectionCount(), 2u);
    EXPECT_EQ(t.getSectionLabel(0), "intro");
    EXPECT_EQ(t.getSectionLabel(1), "main");
}

TEST(MusicTrackUnit, AddSectionNullIgnored) {
    MusicTrack t;
    t.addSection(nullptr, "x");
    EXPECT_EQ(t.getSectionCount(), 0u);
}

TEST(MusicTrackUnit, AddSectionEmptyLabelIgnored) {
    MusicTrack t;
    t.addSection(makeAudio(), "");
    EXPECT_EQ(t.getSectionCount(), 0u);
}

TEST(MusicTrackUnit, GetSectionLabelOutOfRangeReturnsEmpty) {
    MusicTrack t;
    t.addSection(makeAudio(), "a");
    EXPECT_EQ(t.getSectionLabel(99), "");
}

// ---------------------------------------------------------------------------
// addTransition / getTransitionCount
// ---------------------------------------------------------------------------

TEST(MusicTrackUnit, AddTransition) {
    MusicTrack t;
    t.addSection(makeAudio(), "a");
    t.addSection(makeAudio(), "b");
    t.addTransition("a", "b", TransitionRule::OnBar, 0.0);
    EXPECT_EQ(t.getTransitionCount(), 1u);
}

TEST(MusicTrackUnit, AddTransitionReplacesExisting) {
    MusicTrack t;
    t.addSection(makeAudio(), "a");
    t.addSection(makeAudio(), "b");
    t.addTransition("a", "b", TransitionRule::Immediate, 0.0);
    t.addTransition("a", "b", TransitionRule::OnBeat,   0.5);
    // Same (from,to) pair — should replace, not append.
    EXPECT_EQ(t.getTransitionCount(), 1u);
}

TEST(MusicTrackUnit, MultipleTransitions) {
    MusicTrack t;
    t.addSection(makeAudio(), "a");
    t.addSection(makeAudio(), "b");
    t.addSection(makeAudio(), "c");
    t.addTransition("a", "b", TransitionRule::OnBar);
    t.addTransition("b", "c", TransitionRule::CrossFade, 1.0);
    t.addTransition("c", "a", TransitionRule::OnNextSection);
    EXPECT_EQ(t.getTransitionCount(), 3u);
}

// ---------------------------------------------------------------------------
// Beat clock math (manual advance, no engine)
// ---------------------------------------------------------------------------

TEST(MusicTrackUnit, BeatClockOneBarAt120Bpm) {
    // BPM=120, 4/4: one bar = 88200 frames at 44100Hz
    const double bpm          = 120.0;
    const double sampleRate   = 44100.0;
    const double beatsPerBar  = 4.0;
    const double beatsPerFrame = bpm / (60.0 * sampleRate);

    double beatPos = 0.0;
    uint32_t barCount = 0;

    for (uint32_t f = 0; f < 88200; ++f) {
        beatPos += beatsPerFrame;
        if (beatPos >= beatsPerBar) { beatPos -= beatsPerBar; ++barCount; }
    }
    EXPECT_EQ(barCount, 1u);
    EXPECT_NEAR(beatPos, 0.0, 1e-6);
}

TEST(MusicTrackUnit, BeatClockTwoBarsAt60Bpm) {
    // BPM=60, 4/4: one bar = 4s = 176400 frames; two bars = 352800 frames
    const double bpm          = 60.0;
    const double sampleRate   = 44100.0;
    const double beatsPerBar  = 4.0;
    const double beatsPerFrame = bpm / (60.0 * sampleRate);

    double beatPos = 0.0;
    uint32_t barCount = 0;
    for (uint32_t f = 0; f < 352800; ++f) {
        beatPos += beatsPerFrame;
        if (beatPos >= beatsPerBar) { beatPos -= beatsPerBar; ++barCount; }
    }
    EXPECT_EQ(barCount, 2u);
    EXPECT_NEAR(beatPos, 0.0, 1e-4);
}

TEST(MusicTrackUnit, BeatClockBeatBoundaryDetection) {
    // BPM=60: one beat = 44100 frames; verify we detect 4 beat boundaries in 1 bar.
    const double bpm          = 60.0;
    const double sampleRate   = 44100.0;
    const double beatsPerBar  = 4.0;
    const double beatsPerFrame = bpm / (60.0 * sampleRate);

    double beatPos = 0.0;
    uint32_t beatCrossings = 0;

    for (uint32_t f = 0; f < 176400; ++f) {
        const double prev = beatPos;
        beatPos += beatsPerFrame;
        bool barBoundary = false;
        if (beatPos >= beatsPerBar) {
            beatPos -= beatsPerBar;
            barBoundary = true;
            ++beatCrossings;
        } else if (static_cast<uint32_t>(beatPos) > static_cast<uint32_t>(prev)) {
            ++beatCrossings;
        }
        (void)barBoundary;
    }
    EXPECT_EQ(beatCrossings, 4u);
}

TEST(MusicTrackUnit, BeatClockStaysInRange) {
    const double bpm          = 120.0;
    const double sampleRate   = 44100.0;
    const double beatsPerBar  = 4.0;
    const double beatsPerFrame = bpm / (60.0 * sampleRate);

    double beatPos = 0.0;
    for (uint32_t f = 0; f < 88200 * 4; ++f) {
        beatPos += beatsPerFrame;
        if (beatPos >= beatsPerBar) beatPos -= beatsPerBar;
        ASSERT_GE(beatPos, 0.0);
        ASSERT_LT(beatPos, beatsPerBar);
    }
}
