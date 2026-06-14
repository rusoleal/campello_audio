/// @file test_pattern_compiler.cpp
/// @brief Universal tests for PatternCompiler transforms.

#include <gtest/gtest.h>
#include <pattern/compiler.hpp>
#include <pattern/pattern.hpp>
#include <pattern/pattern_event.hpp>

using namespace systems::leal::campello_audio::pi;
using systems::leal::campello_audio::CurveType;

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
// Construction / errors
// ---------------------------------------------------------------------------

TEST(PatternCompiler, EmptyExpression) {
    PatternCompiler comp;
    auto pat = comp.compile("", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_TRUE(pat->events.empty());
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 4.0);
}

TEST(PatternCompiler, UnknownFunction) {
    PatternCompiler comp;
    auto pat = comp.compile("foo(\"bd\")", 4.0);
    EXPECT_EQ(pat, nullptr);
    EXPECT_NE(comp.getLastError().find("Unknown function"), std::string::npos);
}

TEST(PatternCompiler, WrongArgCount) {
    PatternCompiler comp;
    auto pat = comp.compile("slow(2)", 4.0);
    EXPECT_EQ(pat, nullptr);
    EXPECT_NE(comp.getLastError().find("expects"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Mini-notation strings (baseline)
// ---------------------------------------------------------------------------

TEST(PatternCompiler, CompileMiniNotationString) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*4 sd\"", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 5u);
}

TEST(PatternCompiler, CompileBareIdentifier) {
    // Bare identifier without quotes is treated as mini-notation
    PatternCompiler comp;
    auto pat = comp.compile("bd", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 1u);
    EXPECT_EQ(pat->events[0].sourceLabel, "bd");
}

TEST(PatternCompiler, SoundFunction) {
    // sound("...") is the strudel-style wrapper for mini-notation
    PatternCompiler comp;
    auto pat = comp.compile("sound(\"bd*4 sd\")", 4.0);
    ASSERT_NE(pat, nullptr);
    ASSERT_EQ(pat->events.size(), 5u);
    auto labels = collectLabels(*pat);
    EXPECT_EQ(labels[0], "bd");
    EXPECT_EQ(labels[1], "bd");
    EXPECT_EQ(labels[2], "bd");
    EXPECT_EQ(labels[3], "bd");
    EXPECT_EQ(labels[4], "sd");
}

// ---------------------------------------------------------------------------
// stack()
// ---------------------------------------------------------------------------

TEST(PatternCompiler, StackTwoPatterns) {
    PatternCompiler comp;
    auto pat = comp.compile("stack(\"bd*4\", \"sd\")", 4.0);
    ASSERT_NE(pat, nullptr);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 5u);
    // bd*4 at beats 0,1,2,3 and sd at beat 0 → stable order preserves child insertion
    EXPECT_EQ(labels[0], "bd");
    EXPECT_EQ(labels[1], "sd");
    EXPECT_EQ(labels[2], "bd");
    EXPECT_EQ(labels[3], "bd");
    EXPECT_EQ(labels[4], "bd");
}

TEST(PatternCompiler, StackThreePatterns) {
    PatternCompiler comp;
    auto pat = comp.compile("stack(\"bd*2\", \"sd\", \"hh*2\")", 4.0);
    ASSERT_NE(pat, nullptr);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 5u);
}

TEST(PatternCompiler, StackNested) {
    PatternCompiler comp;
    auto pat = comp.compile("stack(\"bd\", stack(\"sd\", \"hh\"))", 4.0);
    ASSERT_NE(pat, nullptr);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 3u);
}

TEST(PatternCompiler, CatTwoPatterns) {
    PatternCompiler comp;
    auto pat = comp.compile("cat(\"bd*2\", \"sd\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 3u);
    EXPECT_EQ(labels[0], "bd");
    EXPECT_EQ(labels[1], "bd");
    EXPECT_EQ(labels[2], "sd");
    auto beats = collectBeats(*pat);
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_DOUBLE_EQ(beats[1], 2.0);
    EXPECT_DOUBLE_EQ(beats[2], 4.0);
}

TEST(PatternCompiler, CatThreePatterns) {
    PatternCompiler comp;
    auto pat = comp.compile("cat(\"bd\", \"sd\", \"hh\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 12.0);
    ASSERT_EQ(pat->events.size(), 3u);
    auto beats = collectBeats(*pat);
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_DOUBLE_EQ(beats[1], 4.0);
    EXPECT_DOUBLE_EQ(beats[2], 8.0);
}

TEST(PatternCompiler, RevPattern) {
    PatternCompiler comp;
    auto pat = comp.compile("rev(\"bd sd hh\")", 4.0);
    ASSERT_NE(pat, nullptr);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 3u);
    // Original: bd@0, sd@1.333, hh@2.667
    // Reversed: bd@0, hh@4-2.667=1.333, sd@4-1.333=2.667
    // Sorted:   bd@0, hh@1.333, sd@2.667
    EXPECT_EQ(labels[0], "bd");
    EXPECT_EQ(labels[1], "hh");
    EXPECT_EQ(labels[2], "sd");
}

TEST(PatternCompiler, StackWithSlowCat) {
    // stack adjusts to max child length
    PatternCompiler comp;
    auto pat = comp.compile("stack(\"<bd sd>\", \"hh*8\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    auto labels = collectLabels(*pat);
    ASSERT_EQ(labels.size(), 10u); // 2 from slow cat + 8 from hh
}

// ---------------------------------------------------------------------------
// slow() / fast()
// ---------------------------------------------------------------------------

TEST(PatternCompiler, SlowDoublesLength) {
    PatternCompiler comp;
    auto pat = comp.compile("slow(2, \"bd*4\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    auto beats = collectBeats(*pat);
    ASSERT_EQ(beats.size(), 4u);
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_DOUBLE_EQ(beats[1], 2.0);
    EXPECT_DOUBLE_EQ(beats[2], 4.0);
    EXPECT_DOUBLE_EQ(beats[3], 6.0);
}

TEST(PatternCompiler, FastHalvesLength) {
    PatternCompiler comp;
    auto pat = comp.compile("fast(2, \"bd*4\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 2.0);
    auto beats = collectBeats(*pat);
    ASSERT_EQ(beats.size(), 4u);
    EXPECT_DOUBLE_EQ(beats[0], 0.0);
    EXPECT_DOUBLE_EQ(beats[1], 0.5);
    EXPECT_DOUBLE_EQ(beats[2], 1.0);
    EXPECT_DOUBLE_EQ(beats[3], 1.5);
}

TEST(PatternCompiler, SlowWithStack) {
    PatternCompiler comp;
    auto pat = comp.compile("slow(2, stack(\"bd*2\", \"sd\"))", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    ASSERT_EQ(pat->events.size(), 3u);
}

// ---------------------------------------------------------------------------
// degradeBy()
// ---------------------------------------------------------------------------

TEST(PatternCompiler, DegradeByZeroRemovesNothing) {
    PatternCompiler comp;
    auto pat = comp.compile("degradeBy(0, \"bd*4\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_EQ(pat->events.size(), 4u);
}

TEST(PatternCompiler, DegradeByOneRemovesAll) {
    PatternCompiler comp;
    auto pat = comp.compile("degradeBy(1, \"bd*4\")", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_TRUE(pat->events.empty());
}

TEST(PatternCompiler, DegradeByWithSeedIsDeterministic) {
    PatternCompiler comp;
    auto pat1 = comp.compile("degradeBy(0.5, 42, \"bd*8\")", 4.0);
    ASSERT_NE(pat1, nullptr);
    auto pat2 = comp.compile("degradeBy(0.5, 42, \"bd*8\")", 4.0);
    ASSERT_NE(pat2, nullptr);
    EXPECT_EQ(pat1->events.size(), pat2->events.size());
}

TEST(PatternCompiler, DegradeByDifferentSeedsDiffer) {
    PatternCompiler comp;
    auto pat1 = comp.compile("degradeBy(0.5, 1, \"bd*8\")", 4.0);
    auto pat2 = comp.compile("degradeBy(0.5, 2, \"bd*8\")", 4.0);
    ASSERT_NE(pat1, nullptr);
    ASSERT_NE(pat2, nullptr);
    // Very unlikely to be identical with different seeds
    bool same = (pat1->events.size() == pat2->events.size());
    if (same && pat1->events.size() == pat2->events.size()) {
        for (size_t i = 0; i < pat1->events.size(); ++i) {
            if (pat1->events[i].beat != pat2->events[i].beat) { same = false; break; }
        }
    }
    EXPECT_FALSE(same);
}

// ---------------------------------------------------------------------------
// Method chains
// ---------------------------------------------------------------------------

TEST(PatternCompiler, GainMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*4\".gain(0.5)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.gain, 0.5f);
    }
}

TEST(PatternCompiler, PanMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*4\".pan(-0.7)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.pan, -0.7f);
    }
}

TEST(PatternCompiler, PitchMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*4\".pitch(1.5)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.pitch, 1.5f);
    }
}

TEST(PatternCompiler, ChainMultipleMethods) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*4\".gain(0.8).pan(0.3).pitch(1.2)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.gain, 0.8f);
        EXPECT_FLOAT_EQ(ev.pan, 0.3f);
        EXPECT_FLOAT_EQ(ev.pitch, 1.2f);
    }
}

TEST(PatternCompiler, LpfMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".lpf(200, 8000, 4.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].targetParam, PatternParam::LpfCutoff);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Sine);
        EXPECT_DOUBLE_EQ(ev.paramCurves[0].periodBeats, 4.0);
        EXPECT_FLOAT_EQ(ev.paramCurves[0].minValue, 200.0f);
        EXPECT_FLOAT_EQ(ev.paramCurves[0].maxValue, 8000.0f);
    }
}

TEST(PatternCompiler, HpfMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".hpf(80, 1200, 2.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].targetParam, PatternParam::HpfCutoff);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Sine);
        EXPECT_DOUBLE_EQ(ev.paramCurves[0].periodBeats, 2.0);
    }
}

TEST(PatternCompiler, MethodsOnStack) {
    PatternCompiler comp;
    auto pat = comp.compile("stack(\"bd*2\", \"sd\").gain(0.9).pan(0.1)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.gain, 0.9f);
        EXPECT_FLOAT_EQ(ev.pan, 0.1f);
    }
}

// ---------------------------------------------------------------------------
// Complex expressions
// ---------------------------------------------------------------------------

TEST(PatternCompiler, ClassicBeatWithStackAndMethods) {
    PatternCompiler comp;
    auto pat = comp.compile(
        "stack(\"bd*4\", \"sd\".gain(0.9), \"hh(7,16)\".lpf(800, 12000, 4.0))",
        4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_EQ(pat->events.size(), 5u + 7u); // 5 from bd+sd, 7 from hh(7,16)

    // Verify the snare has reduced gain
    int snareCount = 0;
    for (const auto& ev : pat->events) {
        if (ev.sourceLabel == "sd") {
            EXPECT_FLOAT_EQ(ev.gain, 0.9f);
            ++snareCount;
        }
    }
    EXPECT_EQ(snareCount, 1);
}

TEST(PatternCompiler, SlowThenGain) {
    PatternCompiler comp;
    auto pat = comp.compile("slow(2, \"bd*4\").gain(0.5)", 4.0);
    ASSERT_NE(pat, nullptr);
    EXPECT_DOUBLE_EQ(pat->lengthInBeats, 8.0);
    for (const auto& ev : pat->events) {
        EXPECT_FLOAT_EQ(ev.gain, 0.5f);
    }
}

TEST(PatternCompiler, SineMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".sine(0.2, 0.8, 2.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Sine);
        EXPECT_FLOAT_EQ(ev.paramCurves[0].minValue, 0.2f);
        EXPECT_FLOAT_EQ(ev.paramCurves[0].maxValue, 0.8f);
        EXPECT_DOUBLE_EQ(ev.paramCurves[0].periodBeats, 2.0);
    }
}

TEST(PatternCompiler, SawMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".saw(0.1, 0.9, 1.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Saw);
    }
}

TEST(PatternCompiler, SquareMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".square(0.0, 1.0, 4.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Square);
    }
}

TEST(PatternCompiler, TriMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".tri(0.3, 0.7, 2.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Triangle);
    }
}

TEST(PatternCompiler, PerlinMethod) {
    PatternCompiler comp;
    auto pat = comp.compile("\"bd*2\".perlin(0.0, 1.0, 8.0)", 4.0);
    ASSERT_NE(pat, nullptr);
    for (const auto& ev : pat->events) {
        ASSERT_EQ(ev.paramCurves.size(), 1u);
        EXPECT_EQ(ev.paramCurves[0].type, CurveType::Perlin);
    }
}
