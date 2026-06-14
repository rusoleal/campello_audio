/// @file test_pattern_track.cpp
/// @brief Universal tests for PatternTrack — no audio device required.

#include <gtest/gtest.h>
#include <campello_audio/pattern_track.hpp>
#include <pattern/pattern_bank.hpp>
#include <campello_audio/constants/transition_rule.hpp>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Construction / defaults
// ---------------------------------------------------------------------------

TEST(PatternTrackUnit, DefaultState) {
    PatternTrack t;
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

TEST(PatternTrackUnit, SetBpm) {
    PatternTrack t;
    t.setBpm(90.0f);
    EXPECT_FLOAT_EQ(t.getBpm(), 90.0f);
}

TEST(PatternTrackUnit, SetBpmClampsToMinimum) {
    PatternTrack t;
    t.setBpm(0.0f);
    EXPECT_GT(t.getBpm(), 0.0f);
}

TEST(PatternTrackUnit, SetTimeSignature) {
    PatternTrack t;
    t.setTimeSignature(3, 4);
    EXPECT_EQ(t.getBeatsPerBar(), 3u);
    EXPECT_EQ(t.getBeatUnit(), 4u);
}

// ---------------------------------------------------------------------------
// PatternBank
// ---------------------------------------------------------------------------

TEST(PatternTrackUnit, SetPatternBank) {
    PatternTrack t;
    auto bank = std::make_shared<PatternBank>();
    t.setPatternBank(bank);
    // No public getter for bank; just verify it doesn't crash.
    SUCCEED();
}

// ---------------------------------------------------------------------------
// addSection / getSectionCount / getSectionLabel
// ---------------------------------------------------------------------------

TEST(PatternTrackUnit, AddSection) {
    PatternTrack t;
    t.addSection("pat_a", "intro");
    t.addSection("pat_b", "main");
    EXPECT_EQ(t.getSectionCount(), 2u);
    EXPECT_EQ(t.getSectionLabel(0), "intro");
    EXPECT_EQ(t.getSectionLabel(1), "main");
}

TEST(PatternTrackUnit, AddSectionEmptyPatternLabelIgnored) {
    PatternTrack t;
    t.addSection("", "x");
    EXPECT_EQ(t.getSectionCount(), 0u);
}

TEST(PatternTrackUnit, AddSectionEmptySectionLabelIgnored) {
    PatternTrack t;
    t.addSection("pat", "");
    EXPECT_EQ(t.getSectionCount(), 0u);
}

TEST(PatternTrackUnit, GetSectionLabelOutOfRangeReturnsEmpty) {
    PatternTrack t;
    t.addSection("pat", "a");
    EXPECT_EQ(t.getSectionLabel(99), "");
}

// ---------------------------------------------------------------------------
// addTransition / getTransitionCount
// ---------------------------------------------------------------------------

TEST(PatternTrackUnit, AddTransition) {
    PatternTrack t;
    t.addSection("pat_a", "a");
    t.addSection("pat_b", "b");
    t.addTransition("a", "b", TransitionRule::OnBar, 0.0);
    EXPECT_EQ(t.getTransitionCount(), 1u);
}

TEST(PatternTrackUnit, AddTransitionReplacesExisting) {
    PatternTrack t;
    t.addSection("pat_a", "a");
    t.addSection("pat_b", "b");
    t.addTransition("a", "b", TransitionRule::Immediate, 0.0);
    t.addTransition("a", "b", TransitionRule::OnBeat,   0.5);
    // Same (from,to) pair — should replace, not append.
    EXPECT_EQ(t.getTransitionCount(), 1u);
}

TEST(PatternTrackUnit, MultipleTransitions) {
    PatternTrack t;
    t.addSection("pat_a", "a");
    t.addSection("pat_b", "b");
    t.addSection("pat_c", "c");
    t.addTransition("a", "b", TransitionRule::OnBar);
    t.addTransition("b", "c", TransitionRule::CrossFade, 1.0);
    t.addTransition("c", "a", TransitionRule::OnNextSection);
    EXPECT_EQ(t.getTransitionCount(), 3u);
}

// ---------------------------------------------------------------------------
// Beat clock math (manual advance, no engine)
// ---------------------------------------------------------------------------

TEST(PatternTrackUnit, BeatClockOneBarAt120Bpm) {
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

TEST(PatternTrackUnit, BeatClockTwoBarsAt60Bpm) {
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

TEST(PatternTrackUnit, BeatClockStaysInRange) {
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
