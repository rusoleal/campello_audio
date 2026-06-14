/// @file test_mini_notation.cpp
/// @brief Universal tests for the mini-notation parser.

#include <gtest/gtest.h>
#include <pattern/mini_notation.hpp>

using namespace systems::leal::campello_audio::pi;

static std::vector<std::string> collectLabels(const Pattern& pat) {
    std::vector<std::string> out;
    for (const auto& ev : pat.events) out.push_back(ev.sourceLabel);
    return out;
}

static std::vector<double> collectBeats(const Pattern& pat) {
    std::vector<double> out;
    for (const auto& ev : pat.events) out.push_back(ev.beat);
    return out;
}

// ---------------------------------------------------------------------------
// Empty / error cases
// ---------------------------------------------------------------------------

TEST(MiniNotation, EmptyString) {
    auto pat = parseMiniNotation("", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_TRUE(pat->events.empty());
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 4.0);
}

TEST(MiniNotation, OnlyRests) {
    auto pat = parseMiniNotation("~ . ~", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_TRUE(pat->events.empty());
}

TEST(MiniNotation, InvalidTokens) {
    EXPECT_EQ(parseMiniNotation("@#$", 4.0), nullptr);
}

TEST(MiniNotation, MissingCloseBracket) {
    EXPECT_EQ(parseMiniNotation("[bd sd", 4.0), nullptr);
}

// ---------------------------------------------------------------------------
// Single events and sequences
// ---------------------------------------------------------------------------

TEST(MiniNotation, SingleWord) {
    auto pat = parseMiniNotation("bd", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 1u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
}

TEST(MiniNotation, TwoWords) {
    auto pat = parseMiniNotation("bd sd", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 2.0);
}

TEST(MiniNotation, FourWords) {
    auto pat = parseMiniNotation("bd sd hh cp", 4.0);
    ASSERT_NE(pat, nullptr);
    auto beats = collectBeats(*pat);
    ASSERT_EQ(beats.size(), 4u);
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_DOUBLE_EQ(beats[1], 1.0);
    EXPECT_DOUBLE_EQ(beats[2], 2.0);
    EXPECT_DOUBLE_EQ(beats[3], 3.0);
}

// ---------------------------------------------------------------------------
// Repetition
// ---------------------------------------------------------------------------

TEST(MiniNotation, RepeatSingle) {
    auto pat = parseMiniNotation("bd*4", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(pat->events[i].sourceLabel, "bd");
        EXPECT_DOUBLE_EQ(pat->events[i].beat, static_cast<double>(i));
    }
}

TEST(MiniNotation, RepeatInSequence) {
    // bd*4 and sd share the cycle equally: bd*4 gets first 2 beats, sd at beat 2
    auto pat = parseMiniNotation("bd*4 sd", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 5u);
    // bd*4 in first half (0..2): bd at 0, 0.5, 1.0, 1.5
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 0.5);
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 1.0);
    EXPECT_DOUBLE_EQ(pat->events[3].beat, 1.5);
    // sd in second half: at 2.0
    EXPECT_EQ(pat->events[4].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[4].beat, 2.0);
}

// ---------------------------------------------------------------------------
// Grouping
// ---------------------------------------------------------------------------

TEST(MiniNotation, SimpleGroup) {
    // [hh oh] is a group of 2 inside a sequence of 1 -> takes full cycle
    // Actually [hh oh] alone is a sequence of 2, so hh at 0, oh at 2
    auto pat = parseMiniNotation("[hh oh]", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[0].sourceLabel, "hh");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 2.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "oh");
}

TEST(MiniNotation, GroupInSequence) {
    // 3 top-level elements share the 4-beat cycle equally (4/3 beats each).
    auto pat = parseMiniNotation("bd [hh oh] cp", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);           // bd
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 4.0 / 3.0);     // hh
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 2.0);           // oh
    EXPECT_DOUBLE_EQ(pat->events[3].beat, 8.0 / 3.0);     // cp
}

TEST(MiniNotation, GroupRepeat) {
    // [hh oh]*2 repeated twice across the cycle
    auto pat = parseMiniNotation("[hh oh]*2", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0); // hh
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 1.0); // oh
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 2.0); // hh
    EXPECT_DOUBLE_EQ(pat->events[3].beat, 3.0); // oh
}

// ---------------------------------------------------------------------------
// Rests
// ---------------------------------------------------------------------------

TEST(MiniNotation, RestInSequence) {
    // 3 top-level elements share the 4-beat cycle equally.
    auto pat = parseMiniNotation("bd ~ sd", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 8.0 / 3.0);
}

// ---------------------------------------------------------------------------
// Euclidean rhythms
// ---------------------------------------------------------------------------

TEST(MiniNotation, EuclideanThreeOverEight) {
    auto pat = parseMiniNotation("hh(3,8)", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 3u);
    // 3 hits over 8 steps in a 4-beat cycle: steps are 0.5 beats each
    // Bresenham(3,8) -> [x . . x . . x .] -> hits at steps 0, 3, 6
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 1.5);
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 3.0);
}

TEST(MiniNotation, EuclideanFourOverFour) {
    auto pat = parseMiniNotation("bd(4,4)", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 1.0);
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 2.0);
    EXPECT_DOUBLE_EQ(pat->events[3].beat, 3.0);
}

TEST(MiniNotation, EuclideanZeroHits) {
    auto pat = parseMiniNotation("bd(0,8)", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_TRUE(pat->events.empty());
}

// ---------------------------------------------------------------------------
// Complex combinations
// ---------------------------------------------------------------------------

TEST(MiniNotation, ClassicBeat) {
    // Four-on-the-floor kick with snare on 3
    auto pat = parseMiniNotation("bd*4 sd", 4.0);
    ASSERT_NE(pat, nullptr);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 5u);
    EXPECT_EQ(labels[0], "bd");
    EXPECT_EQ(labels[1], "bd");
    EXPECT_EQ(labels[2], "bd");
    EXPECT_EQ(labels[3], "bd");
    EXPECT_EQ(labels[4], "sd");
}

TEST(MiniNotation, NestedGroup) {
    // a [b [c d]] in 4 beats:
    // a at 0 (2 beats)
    // [b [c d]] at 2 (2 beats) -> b at 2 (1 beat), [c d] at 3 (1 beat) -> c at 3, d at 3.5
    auto pat = parseMiniNotation("a [b [c d]]", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);  // a
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 2.0);  // b
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 3.0);  // c
    EXPECT_DOUBLE_EQ(pat->events[3].beat, 3.5);  // d
}

// ---------------------------------------------------------------------------
// Different cycle lengths
// ---------------------------------------------------------------------------

TEST(MiniNotation, ThreeBeatCycle) {
    auto pat = parseMiniNotation("bd sd hh", 3.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 3u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 1.0);
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 2.0);
}

// ---------------------------------------------------------------------------
// Slow cat < >
// ---------------------------------------------------------------------------

TEST(MiniNotation, SlowCatTwo) {
    // <bd sd> — 2 elements, pattern length doubles
    auto pat = parseMiniNotation("<bd sd>", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 4.0);
}

TEST(MiniNotation, SlowCatThree) {
    // <bd sd hh> — 3 elements, pattern length triples
    auto pat = parseMiniNotation("<bd sd hh>", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 12.0);
    ASSERT_EQ(pat->events.size(), 3u);
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 4.0);
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 8.0);
}

TEST(MiniNotation, SlowCatWithRepeat) {
    // <bd*2 sd> — bd*2 in first cycle, sd in second
    auto pat = parseMiniNotation("<bd*2 sd>", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    ASSERT_EQ(pat->events.size(), 3u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 2.0);
    EXPECT_EQ(pat->events[2].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 4.0);
}

// ---------------------------------------------------------------------------
// Parallel ,
// ---------------------------------------------------------------------------

TEST(MiniNotation, ParallelTwo) {
    // bd,sd — both at beat 0
    auto pat = parseMiniNotation("bd,sd", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 4.0);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "sd");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 0.0);
}

TEST(MiniNotation, ParallelWithSequence) {
    // bd*2, sd hh — each branch gets the full cycle
    auto pat = parseMiniNotation("bd*2, sd hh", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    auto beats = collectBeats(*pat);
    // Left: bd*2 in 4 beats → bd at 0, bd at 2
    // Right: sd hh in 4 beats → sd at 0, hh at 2
    EXPECT_DOUBLE_EQ(beats[0], 0.0); // bd
    EXPECT_DOUBLE_EQ(beats[1], 0.0); // sd (parallel)
    EXPECT_DOUBLE_EQ(beats[2], 2.0); // bd
    EXPECT_DOUBLE_EQ(beats[3], 2.0); // hh (parallel)
}

TEST(MiniNotation, ParallelWithSlowCat) {
    // <bd sd>, hh — slow cat expands, hh plays every cycle
    auto pat = parseMiniNotation("<bd sd>, hh", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    ASSERT_EQ(pat->events.size(), 4u);
    // bd at 0, hh at 0 (parallel in cycle 1)
    // sd at 4, hh at 4 (parallel in cycle 2)
    auto labels = collectLabels(*pat);
    auto beats = collectBeats(*pat);
    EXPECT_EQ(labels[0], "bd");
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_EQ(labels[1], "hh");
    EXPECT_DOUBLE_EQ(beats[1], 0.0);
    EXPECT_EQ(labels[2], "sd");
    EXPECT_DOUBLE_EQ(beats[2], 4.0);
    EXPECT_EQ(labels[3], "hh");
    EXPECT_DOUBLE_EQ(beats[3], 4.0);
}

// ---------------------------------------------------------------------------
// Slow operator /
// ---------------------------------------------------------------------------

TEST(MiniNotation, SlowOperator) {
    // a/2 b — a gets 2 slots, b gets 1 slot. Total = 3 slots.
    // a gets 2/3 of 4 = 8/3 ≈ 2.667 beats, b gets 4/3 ≈ 1.333 beats.
    auto pat = parseMiniNotation("a/2 b", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "a");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "b");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 8.0 / 3.0);
}

TEST(MiniNotation, SlowOperatorWithRepeat) {
    // a*2/2 b — a repeated twice within its (doubled) slot
    // a*2/2 gets weight 2, b gets weight 1. Total = 3.
    // Slot for a*2/2 = 8/3 beats. a*2 repeats twice in 8/3: a at 0, a at 4/3.
    auto pat = parseMiniNotation("a*2/2 b", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 3u);
    EXPECT_EQ(pat->events[0].sourceLabel, "a");
    EXPECT_DOUBLE_EQ(pat->events[0].beat, 0.0);
    EXPECT_EQ(pat->events[1].sourceLabel, "a");
    EXPECT_DOUBLE_EQ(pat->events[1].beat, 4.0 / 3.0);
    EXPECT_EQ(pat->events[2].sourceLabel, "b");
    EXPECT_DOUBLE_EQ(pat->events[2].beat, 8.0 / 3.0);
}

// ---------------------------------------------------------------------------
// Probability ?
// ---------------------------------------------------------------------------

TEST(MiniNotation, ProbabilityDefault) {
    // a? b — a has 50% probability, b is certain
    auto pat = parseMiniNotation("a? b", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_EQ(pat->events[0].sourceLabel, "a");
    EXPECT_FLOAT_EQ(pat->events[0].probability, 0.5f);
    EXPECT_EQ(pat->events[1].sourceLabel, "b");
    EXPECT_FLOAT_EQ(pat->events[1].probability, 1.0f);
}

TEST(MiniNotation, ProbabilityCustom) {
    // a?0.3 b — a has 30% probability
    auto pat = parseMiniNotation("a?0.3 b", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 2u);
    EXPECT_FLOAT_EQ(pat->events[0].probability, 0.3f);
    EXPECT_FLOAT_EQ(pat->events[1].probability, 1.0f);
}

TEST(MiniNotation, ProbabilityWithRepeat) {
    // a*4?0.8 — all 4 repeats have 80% probability
    auto pat = parseMiniNotation("a*4?0.8", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 4u);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.probability, 0.8f);
    }
}
