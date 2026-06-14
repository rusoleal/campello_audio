/// @file test_pattern.cpp
/// @brief Universal tests for Pattern query and ParameterCurve.

#include <gtest/gtest.h>
#include <pattern/pattern.hpp>
#include <pattern/pattern_event.hpp>

using namespace systems::leal::campello_audio::pi;
using systems::leal::campello_audio::CurveType;

// ---------------------------------------------------------------------------
// ParameterCurve evaluation
// ---------------------------------------------------------------------------

TEST(ParameterCurve, Linear) {
    ParameterCurve c;
    c.type = CurveType::Linear;
    c.minValue = 0.0f;
    c.maxValue = 10.0f;
    EXPECT_FLOAT_EQ(c.evaluate(0.0), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(0.5), 5.0f);
    // Phase is periodic: 1.0 wraps to 0.0, so evaluate(1.0) == evaluate(0.0).
    EXPECT_FLOAT_EQ(c.evaluate(1.0), 0.0f);
    EXPECT_NEAR(c.evaluate(0.99), 9.9f, 0.01f);
}

TEST(ParameterCurve, Sine) {
    ParameterCurve c;
    c.type = CurveType::Sine;
    c.minValue = 0.0f;
    c.maxValue = 1.0f;
    EXPECT_FLOAT_EQ(c.evaluate(0.0), 0.0f);
    EXPECT_NEAR(c.evaluate(0.5), 1.0f, 1e-5f);
    EXPECT_NEAR(c.evaluate(1.0), 0.0f, 1e-5f);
}

TEST(ParameterCurve, PhaseWraps) {
    ParameterCurve c;
    c.type = CurveType::Linear;
    c.minValue = 0.0f;
    c.maxValue = 1.0f;
    EXPECT_FLOAT_EQ(c.evaluate(2.0), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(-1.0), 0.0f);
}

// ---------------------------------------------------------------------------
// Pattern query
// ---------------------------------------------------------------------------

static Pattern makeTestPattern() {
    Pattern p;
    p.lengthInBeats = 4.0;
    p.events = {
        {0.0, 0.5, "a", 1.0f, 1.0f, 0.0f, 1.0f, {}},
        {1.0, 0.5, "b", 1.0f, 1.0f, 0.0f, 1.0f, {}},
        {2.0, 0.5, "c", 1.0f, 1.0f, 0.0f, 1.0f, {}},
        {3.0, 0.5, "d", 1.0f, 1.0f, 0.0f, 1.0f, {}},
    };
    return p;
}

TEST(PatternQuery, QueryExactRange) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.query(0.0, 1.0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->sourceLabel, "a");
}

TEST(PatternQuery, QueryMultipleEvents) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.query(0.5, 2.5, out);
    // a ends at 0.5 (does not overlap), b [1,1.5) and c [2,2.5) overlap.
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0]->sourceLabel, "b");
    EXPECT_EQ(out[1]->sourceLabel, "c");
}

TEST(PatternQuery, QueryOverlapsDuration) {
    // Event 'a' spans [0.0, 0.5). Query [0.4, 0.6) should still find it.
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.query(0.4, 0.6, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->sourceLabel, "a");
}

TEST(PatternQuery, QueryEmptyRange) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.query(1.5, 1.5, out);
    EXPECT_TRUE(out.empty());
}

TEST(PatternQuery, QueryOutOfRange) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.query(10.0, 11.0, out);
    EXPECT_TRUE(out.empty());
}

TEST(PatternQuery, QueryWrappedNoWrap) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.queryWrapped(0.5, 2.5, out);
    // Same as query(0.5, 2.5) — a ends at 0.5, so only b and c.
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0]->sourceLabel, "b");
    EXPECT_EQ(out[1]->sourceLabel, "c");
}

TEST(PatternQuery, QueryWrappedAcrossBoundary) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.queryWrapped(3.5, 0.5, out);
    // d ends at 3.5, so it does not overlap [3.5, 4.0).
    // a spans [0.0, 0.5) and overlaps [0.0, 0.5).
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->sourceLabel, "a");
}

TEST(PatternQuery, QueryWrappedNegative) {
    auto p = makeTestPattern();
    std::vector<const PatternEvent*> out;
    p.queryWrapped(-0.5, 0.5, out);
    // -0.5 wraps to 3.5. d ends at 3.5, so only a overlaps [0.0, 0.5).
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->sourceLabel, "a");
}

TEST(PatternQuery, EmptyPattern) {
    Pattern p;
    p.lengthInBeats = 4.0;
    std::vector<const PatternEvent*> out;
    p.query(0.0, 4.0, out);
    EXPECT_TRUE(out.empty());
}
