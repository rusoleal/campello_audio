/// @file test_random_source.cpp
/// @brief Universal tests for RandomSource — no audio device required.
///
/// These tests exercise the variant pool, weighted selection, avoid-repeat logic,
/// and pitch/volume variation ranges through the public RandomSource API, so
/// they run on every CI platform without hardware.

#include <gtest/gtest.h>
#include <campello_audio/random_source.hpp>
#include <campello_audio/wav_source.hpp>

using namespace systems::leal::campello_audio;

/// Create a dummy (empty, unloaded) WavSource variant.
static std::shared_ptr<WavSource> makeVariant() {
    return std::make_shared<WavSource>();
}

// ---------------------------------------------------------------------------
// Construction / default state
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, DefaultState) {
    RandomSource rs;
    EXPECT_EQ(rs.getVariantCount(), 0u);
    EXPECT_FLOAT_EQ(rs.getPitchVariation(), 0.0f);
    EXPECT_FLOAT_EQ(rs.getVolumeVariation(), 0.0f);
    EXPECT_TRUE(rs.getAvoidRepeat());
    EXPECT_EQ(rs.getLastVariantIndex(), -1);
}

// ---------------------------------------------------------------------------
// addVariant / clearVariants / getVariantCount
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, AddVariant) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    EXPECT_EQ(rs.getVariantCount(), 1u);
    rs.addVariant(makeVariant());
    rs.addVariant(makeVariant());
    EXPECT_EQ(rs.getVariantCount(), 3u);
}

TEST(RandomSourceUnit, AddNullVariantIgnored) {
    RandomSource rs;
    rs.addVariant(nullptr);
    EXPECT_EQ(rs.getVariantCount(), 0u);
}

TEST(RandomSourceUnit, ClearVariants) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    rs.addVariant(makeVariant());
    rs.clearVariants();
    EXPECT_EQ(rs.getVariantCount(), 0u);
    EXPECT_EQ(rs.getLastVariantIndex(), -1);
}

// ---------------------------------------------------------------------------
// Setter / getter round-trips
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, PitchVariation) {
    RandomSource rs;
    rs.setPitchVariation(3.0f);
    EXPECT_FLOAT_EQ(rs.getPitchVariation(), 3.0f);
}

TEST(RandomSourceUnit, PitchVariationClampsNegative) {
    RandomSource rs;
    rs.setPitchVariation(-1.0f);
    EXPECT_FLOAT_EQ(rs.getPitchVariation(), 0.0f);
}

TEST(RandomSourceUnit, VolumeVariation) {
    RandomSource rs;
    rs.setVolumeVariation(6.0f);
    EXPECT_FLOAT_EQ(rs.getVolumeVariation(), 6.0f);
}

TEST(RandomSourceUnit, AvoidRepeat) {
    RandomSource rs;
    EXPECT_TRUE(rs.getAvoidRepeat());
    rs.setAvoidRepeat(false);
    EXPECT_FALSE(rs.getAvoidRepeat());
    rs.setAvoidRepeat(true);
    EXPECT_TRUE(rs.getAvoidRepeat());
}

// ---------------------------------------------------------------------------
// selectVariant — empty pool
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, EmptyPoolReturnsNegOne) {
    RandomSource rs;
    EXPECT_EQ(rs.selectVariant(), -1);
}

// ---------------------------------------------------------------------------
// selectVariant — single variant
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, SingleVariantAlwaysPicked) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(rs.selectVariant(), 0);
    }
}

// ---------------------------------------------------------------------------
// selectVariant — avoid-repeat
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, AvoidRepeatNeverRepeatsWithTwoVariants) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    rs.addVariant(makeVariant());
    rs.setAvoidRepeat(true);

    int32_t prev = rs.selectVariant();
    for (int i = 0; i < 50; ++i) {
        const int32_t cur = rs.selectVariant();
        EXPECT_NE(cur, prev);
        prev = cur;
    }
}

TEST(RandomSourceUnit, AvoidRepeatDisabledCanRepeat) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    rs.addVariant(makeVariant());
    rs.setAvoidRepeat(false);

    bool sawRepeat = false;
    int32_t prev = rs.selectVariant();
    for (int i = 0; i < 200; ++i) {
        const int32_t cur = rs.selectVariant();
        if (cur == prev) { sawRepeat = true; break; }
        prev = cur;
    }
    EXPECT_TRUE(sawRepeat);
}

// ---------------------------------------------------------------------------
// selectVariant — weighted distribution (statistical)
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, WeightedDistributionApproximate) {
    RandomSource rs;
    // Variant 0: weight 1.0 (expected ~25%)
    // Variant 1: weight 3.0 (expected ~75%)
    rs.addVariant(makeVariant(), 1.0f);
    rs.addVariant(makeVariant(), 3.0f);
    rs.setAvoidRepeat(false);

    int counts[2] = {0, 0};
    constexpr int N = 2000;
    for (int i = 0; i < N; ++i) {
        ++counts[rs.selectVariant()];
    }

    // Variant 1 should appear roughly 3× more often than variant 0.
    const float ratio = static_cast<float>(counts[1]) / static_cast<float>(counts[0]);
    EXPECT_GT(ratio, 2.0f) << "variant 1 should be chosen ~3x more often";
    EXPECT_LT(ratio, 4.5f) << "ratio too high — selection may be biased";
}

TEST(RandomSourceUnit, EqualWeightsDistributeEvenly) {
    RandomSource rs;
    for (int i = 0; i < 4; ++i) rs.addVariant(makeVariant(), 1.0f);
    rs.setAvoidRepeat(false);

    int counts[4] = {0, 0, 0, 0};
    constexpr int N = 4000;
    for (int i = 0; i < N; ++i) {
        const int32_t idx = rs.selectVariant();
        ASSERT_GE(idx, 0);
        ASSERT_LT(idx, 4);
        ++counts[idx];
    }

    // Each variant should appear between 18% and 32% (expected 25%).
    for (int i = 0; i < 4; ++i) {
        const float pct = static_cast<float>(counts[i]) / N;
        EXPECT_GT(pct, 0.18f) << "variant " << i << " under-represented";
        EXPECT_LT(pct, 0.32f) << "variant " << i << " over-represented";
    }
}

// ---------------------------------------------------------------------------
// getLastVariantIndex tracks selections
// ---------------------------------------------------------------------------

TEST(RandomSourceUnit, LastVariantIndexUpdatesAfterSelect) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    rs.addVariant(makeVariant());

    EXPECT_EQ(rs.getLastVariantIndex(), -1);
    const int32_t idx = rs.selectVariant();
    EXPECT_EQ(rs.getLastVariantIndex(), idx);
}

TEST(RandomSourceUnit, LastVariantIndexInitiallyNegOne) {
    RandomSource rs;
    rs.addVariant(makeVariant());
    EXPECT_EQ(rs.getLastVariantIndex(), -1);
}
