/// @file test_pattern_bank.cpp
/// @brief Universal tests for PatternBank serialization.

#include <gtest/gtest.h>
#include <pattern/pattern_bank.hpp>
#include <pattern/pattern.hpp>
#include <pattern/pattern_event.hpp>
#include <filesystem>
#include <fstream>

using namespace systems::leal::campello_audio::pi;
using systems::leal::campello_audio::CurveType;

static std::shared_ptr<Pattern> makePatternA() {
    auto p = std::make_shared<Pattern>();
    p->lengthInBeats = 4.0;
    p->events = {
        {0.0, 0.0, "kick",  1.0f, 1.0f, 0.0f, 1.0f, {}},
        {1.0, 0.0, "snare", 0.9f, 1.0f, 0.0f, 1.0f, {}},
        {2.0, 0.0, "kick",  1.0f, 1.0f, 0.0f, 1.0f, {}},
        {3.0, 0.0, "snare", 0.9f, 1.0f, 0.0f, 1.0f, {}},
    };
    return p;
}

static std::shared_ptr<Pattern> makePatternB() {
    auto p = std::make_shared<Pattern>();
    p->lengthInBeats = 4.0;
    p->events = {
        {0.0, 0.0, "hihat", 0.7f, 1.0f, -0.3f, 1.0f, {}},
        {0.5, 0.0, "hihat", 0.5f, 1.0f,  0.3f, 1.0f, {}},
    };
    // Add a parameter curve
    ParameterCurve curve;
    curve.targetParam = PatternParam::LpfCutoff;
    curve.type = CurveType::Sine;
    curve.periodBeats = 4.0;
    curve.minValue = 200.0f;
    curve.maxValue = 8000.0f;
    curve.rtpcName = "intensity";
    p->events[0].paramCurves.push_back(curve);
    return p;
}

class PatternBankTest : public ::testing::Test {
protected:
    std::string tempPath;

    void SetUp() override {
        tempPath = (std::filesystem::temp_directory_path() / "test_pattern_bank.cpbank").string();
    }

    void TearDown() override {
        std::filesystem::remove(tempPath);
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(PatternBankUnit, EmptyBank) {
    PatternBank bank;
    EXPECT_TRUE(bank.getPatternLabels().empty());
    EXPECT_TRUE(bank.getSourceLabels().empty());
    EXPECT_EQ(bank.getPattern("foo"), nullptr);
    EXPECT_EQ(bank.getSource("bar"), nullptr);
}

TEST(PatternBankUnit, AddAndRetrievePattern) {
    PatternBank bank;
    bank.addPattern("beat_a", makePatternA());
    auto p = bank.getPattern("beat_a");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->events.size(), 4u);
    EXPECT_DOUBLE_EQ(p->lengthInBeats, 4.0);
}

TEST(PatternBankUnit, AddNullPatternIgnored) {
    PatternBank bank;
    bank.addPattern("x", nullptr);
    EXPECT_TRUE(bank.getPatternLabels().empty());
}

TEST(PatternBankUnit, AddEmptyLabelIgnored) {
    PatternBank bank;
    bank.addPattern("", makePatternA());
    EXPECT_TRUE(bank.getPatternLabels().empty());
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_F(PatternBankTest, SaveAndLoadEmpty) {
    PatternBank bank;
    ASSERT_TRUE(bank.saveToFile(tempPath));

    PatternBank loaded;
    ASSERT_TRUE(loaded.loadFromFile(tempPath));
    EXPECT_TRUE(loaded.getPatternLabels().empty());
}

TEST_F(PatternBankTest, SaveAndLoadPatterns) {
    PatternBank bank;
    bank.addPattern("beat_a", makePatternA());
    bank.addPattern("beat_b", makePatternB());
    ASSERT_TRUE(bank.saveToFile(tempPath));

    PatternBank loaded;
    ASSERT_TRUE(loaded.loadFromFile(tempPath));

    auto labels = loaded.getPatternLabels();
    ASSERT_EQ(labels.size(), 2u);

    auto pa = loaded.getPattern("beat_a");
    ASSERT_NE(pa, nullptr);
    ASSERT_EQ(pa->events.size(), 4u);
    EXPECT_EQ(pa->events[0].sourceLabel, "kick");
    EXPECT_FLOAT_EQ(pa->events[0].gain, 1.0f);
    EXPECT_EQ(pa->events[1].sourceLabel, "snare");
    EXPECT_FLOAT_EQ(pa->events[1].gain, 0.9f);

    auto pb = loaded.getPattern("beat_b");
    ASSERT_NE(pb, nullptr);
    ASSERT_EQ(pb->events.size(), 2u);
    EXPECT_FLOAT_EQ(pb->events[0].pan, -0.3f);
    EXPECT_FLOAT_EQ(pb->events[1].pan, 0.3f);

    // Verify parameter curve survived round-trip
    ASSERT_EQ(pb->events[0].paramCurves.size(), 1u);
    const auto& curve = pb->events[0].paramCurves[0];
    EXPECT_EQ(curve.targetParam, PatternParam::LpfCutoff);
    EXPECT_EQ(curve.type, CurveType::Sine);
    EXPECT_DOUBLE_EQ(curve.periodBeats, 4.0);
    EXPECT_FLOAT_EQ(curve.minValue, 200.0f);
    EXPECT_FLOAT_EQ(curve.maxValue, 8000.0f);
    EXPECT_EQ(curve.rtpcName, "intensity");
}

TEST_F(PatternBankTest, LoadBadMagicFails) {
    std::ofstream f(tempPath, std::ios::binary);
    f.write("BAD\0", 4);
    f.close();

    PatternBank loaded;
    EXPECT_FALSE(loaded.loadFromFile(tempPath));
}

TEST_F(PatternBankTest, LoadMissingFileFails) {
    PatternBank loaded;
    EXPECT_FALSE(loaded.loadFromFile("/nonexistent/path/file.cpbank"));
}
