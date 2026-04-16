/// @file test_audio3d.cpp
/// @brief Unit tests for the pure 3D audio math functions in audio3d.hpp.
///
/// No audio device required — all functions are stateless.

#include <gtest/gtest.h>
#include "audio3d.hpp"    // src/pi/ is on the include path for universal tests

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// computeAttenuation — None
// ---------------------------------------------------------------------------

TEST(ComputeAttenuation, NoneAlwaysReturnsOne) {
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::None,    0.0f, 1.0f, 100.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::None,   50.0f, 1.0f, 100.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::None, 1000.0f, 1.0f, 100.0f, 1.0f), 1.0f);
}

// ---------------------------------------------------------------------------
// computeAttenuation — Linear
// ---------------------------------------------------------------------------

TEST(ComputeAttenuation, LinearFullVolumeAtOrBelowMinDist) {
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Linear, 1.0f, 1.0f, 100.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Linear, 0.5f, 1.0f, 100.0f, 1.0f), 1.0f);
}

TEST(ComputeAttenuation, LinearZeroAtOrBeyondMaxDist) {
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Linear, 100.0f, 1.0f, 100.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Linear, 200.0f, 1.0f, 100.0f, 1.0f), 0.0f);
}

TEST(ComputeAttenuation, LinearDecreasesMonotonically) {
    const float v50  = computeAttenuation(AttenuationModel::Linear,  50.0f, 1.0f, 100.0f, 1.0f);
    const float v75  = computeAttenuation(AttenuationModel::Linear,  75.0f, 1.0f, 100.0f, 1.0f);
    EXPECT_GT(v50, v75);
    EXPECT_GT(v75, 0.0f);
    EXPECT_LT(v50, 1.0f);
}

TEST(ComputeAttenuation, LinearRolloffScalesFalloff) {
    // Higher rolloff → steeper falloff → lower gain at same distance
    const float low  = computeAttenuation(AttenuationModel::Linear, 50.0f, 1.0f, 100.0f, 0.5f);
    const float high = computeAttenuation(AttenuationModel::Linear, 50.0f, 1.0f, 100.0f, 2.0f);
    EXPECT_GT(low, high);
}

// ---------------------------------------------------------------------------
// computeAttenuation — Inverse
// ---------------------------------------------------------------------------

TEST(ComputeAttenuation, InverseFullVolumeAtMinDist) {
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Inverse, 1.0f, 1.0f, 1000.0f, 1.0f), 1.0f);
}

TEST(ComputeAttenuation, InverseDecreasesWithDistance) {
    const float v10  = computeAttenuation(AttenuationModel::Inverse,  10.0f, 1.0f, 1000.0f, 1.0f);
    const float v100 = computeAttenuation(AttenuationModel::Inverse, 100.0f, 1.0f, 1000.0f, 1.0f);
    EXPECT_GT(v10, v100);
    EXPECT_GT(v10,  0.0f);
    EXPECT_LT(v100, 1.0f);
}

// ---------------------------------------------------------------------------
// computeAttenuation — Exponential
// ---------------------------------------------------------------------------

TEST(ComputeAttenuation, ExponentialFullVolumeAtMinDist) {
    EXPECT_FLOAT_EQ(computeAttenuation(AttenuationModel::Exponential, 1.0f, 1.0f, 1000.0f, 1.0f), 1.0f);
}

TEST(ComputeAttenuation, ExponentialDecreasesWithDistance) {
    const float v10  = computeAttenuation(AttenuationModel::Exponential,  10.0f, 1.0f, 1000.0f, 1.0f);
    const float v100 = computeAttenuation(AttenuationModel::Exponential, 100.0f, 1.0f, 1000.0f, 1.0f);
    EXPECT_GT(v10, v100);
    EXPECT_GT(v10,  0.0f);
    EXPECT_LT(v100, 1.0f);
}

// ---------------------------------------------------------------------------
// computePan
// ---------------------------------------------------------------------------

// Source directly in front → centred (pan ≈ 0)
TEST(ComputePan, SourceInFrontIsCentred) {
    Vec3 listenerPos  = {0.0f, 0.0f,  0.0f};
    Vec3 listenerFwd  = {0.0f, 0.0f, -1.0f};
    Vec3 listenerUp   = {0.0f, 1.0f,  0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -10.0f}; // directly ahead

    const float pan = computePan(sourcePos, listenerPos, listenerFwd, listenerUp);
    EXPECT_NEAR(pan, 0.0f, 1e-4f);
}

// Source to the right of listener → positive pan
TEST(ComputePan, SourceToRightIsPositive) {
    Vec3 listenerPos  = {0.0f, 0.0f, 0.0f};
    Vec3 listenerFwd  = {0.0f, 0.0f, -1.0f};
    Vec3 listenerUp   = {0.0f, 1.0f,  0.0f};
    Vec3 sourcePos    = {10.0f, 0.0f, 0.0f}; // to the right

    const float pan = computePan(sourcePos, listenerPos, listenerFwd, listenerUp);
    EXPECT_GT(pan, 0.0f);
}

// Source to the left of listener → negative pan
TEST(ComputePan, SourceToLeftIsNegative) {
    Vec3 listenerPos  = {0.0f, 0.0f,  0.0f};
    Vec3 listenerFwd  = {0.0f, 0.0f, -1.0f};
    Vec3 listenerUp   = {0.0f, 1.0f,  0.0f};
    Vec3 sourcePos    = {-10.0f, 0.0f, 0.0f}; // to the left

    const float pan = computePan(sourcePos, listenerPos, listenerFwd, listenerUp);
    EXPECT_LT(pan, 0.0f);
}

// Source at same position as listener → centred (no crash)
TEST(ComputePan, ZeroDistanceReturnsCentre) {
    Vec3 pos         = {0.0f, 0.0f, 0.0f};
    Vec3 listenerFwd = {0.0f, 0.0f, -1.0f};
    Vec3 listenerUp  = {0.0f, 1.0f,  0.0f};
    EXPECT_FLOAT_EQ(computePan(pos, pos, listenerFwd, listenerUp), 0.0f);
}

// Pan is clamped to [-1, +1]
TEST(ComputePan, ResultClampedToMinusOneToOne) {
    Vec3 listenerPos  = {0.0f, 0.0f,  0.0f};
    Vec3 listenerFwd  = {0.0f, 0.0f, -1.0f};
    Vec3 listenerUp   = {0.0f, 1.0f,  0.0f};
    Vec3 sourcePos    = {10000.0f, 0.0f, 0.0f};

    const float pan = computePan(sourcePos, listenerPos, listenerFwd, listenerUp);
    EXPECT_GE(pan, -1.0f);
    EXPECT_LE(pan,  1.0f);
}

// ---------------------------------------------------------------------------
// computeDoppler
// ---------------------------------------------------------------------------

// No relative velocity → pitch = 1.0
TEST(ComputeDoppler, StaticSourceAndListenerNoPitchShift) {
    Vec3 zero = {0.0f, 0.0f, 0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -10.0f};
    const float pitch = computeDoppler(sourcePos, zero, zero, zero, 343.0f, 1.0f);
    EXPECT_NEAR(pitch, 1.0f, 1e-4f);
}

// Source moving toward listener → pitch > 1 (higher frequency)
TEST(ComputeDoppler, ApproachingSourceRaisesPitch) {
    Vec3 listenerPos  = {0.0f, 0.0f,  0.0f};
    Vec3 listenerVel  = {0.0f, 0.0f,  0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -100.0f};
    Vec3 sourceVel    = {0.0f, 0.0f,  50.0f};  // moving toward listener (+z)

    const float pitch = computeDoppler(sourcePos, sourceVel, listenerPos, listenerVel, 343.0f, 1.0f);
    EXPECT_GT(pitch, 1.0f);
}

// Source moving away from listener → pitch < 1 (lower frequency)
TEST(ComputeDoppler, RecedeingSourceLowersPitch) {
    Vec3 listenerPos  = {0.0f, 0.0f,   0.0f};
    Vec3 listenerVel  = {0.0f, 0.0f,   0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -100.0f};
    Vec3 sourceVel    = {0.0f, 0.0f, -50.0f};  // moving away

    const float pitch = computeDoppler(sourcePos, sourceVel, listenerPos, listenerVel, 343.0f, 1.0f);
    EXPECT_LT(pitch, 1.0f);
}

// dopplerFactor = 0 → no shift
TEST(ComputeDoppler, ZeroDopplerFactorReturnsOne) {
    Vec3 listenerPos  = {0.0f, 0.0f,  0.0f};
    Vec3 listenerVel  = {0.0f, 0.0f,  0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -100.0f};
    Vec3 sourceVel    = {0.0f, 0.0f,  50.0f};

    const float pitch = computeDoppler(sourcePos, sourceVel, listenerPos, listenerVel, 343.0f, 0.0f);
    EXPECT_FLOAT_EQ(pitch, 1.0f);
}

// Result is clamped to [0.1, 10]
TEST(ComputeDoppler, ResultClampedToSafeRange) {
    Vec3 listenerPos  = {0.0f, 0.0f,   0.0f};
    Vec3 listenerVel  = {0.0f, 0.0f,   0.0f};
    Vec3 sourcePos    = {0.0f, 0.0f, -10.0f};
    Vec3 sourceVel    = {0.0f, 0.0f, 500.0f}; // super-sonic approach

    const float pitch = computeDoppler(sourcePos, sourceVel, listenerPos, listenerVel, 343.0f, 1.0f);
    EXPECT_GE(pitch, 0.1f);
    EXPECT_LE(pitch, 10.0f);
}
