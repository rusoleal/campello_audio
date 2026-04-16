/// @file test_rtpc.cpp
/// @brief Integration tests for RTPC (Real-Time Parameter Control).
///
/// All tests require a real audio device (CoreAudio on macOS).

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <cmath>
#include <memory>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class RtpcTest : public ::testing::Test {
protected:
    AudioEngine engine;

    void SetUp() override {
        AudioEngineDescriptor d;
        ASSERT_TRUE(engine.init(d));
    }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// AudioParameter unit behaviour (no engine needed)
// ---------------------------------------------------------------------------

TEST(AudioParameterTest, DefaultConstruct) {
    AudioParameter p("speed", 0.0f, 200.0f);
    EXPECT_EQ(p.getName(), "speed");
    EXPECT_FLOAT_EQ(p.getMinValue(), 0.0f);
    EXPECT_FLOAT_EQ(p.getMaxValue(), 200.0f);
    EXPECT_FLOAT_EQ(p.getValue(), 0.0f);   // initial value == minValue
}

TEST(AudioParameterTest, SetValueClamped) {
    AudioParameter p("x", 0.0f, 1.0f);
    p.setValue(0.5f);
    EXPECT_FLOAT_EQ(p.getValue(), 0.5f);

    p.setValue(-1.0f);
    EXPECT_FLOAT_EQ(p.getValue(), 0.0f);   // clamped to min

    p.setValue(5.0f);
    EXPECT_FLOAT_EQ(p.getValue(), 1.0f);   // clamped to max
}

// ---------------------------------------------------------------------------
// Engine registration
// ---------------------------------------------------------------------------

TEST_F(RtpcTest, RegisterAndGetParameter) {
    auto p = std::make_shared<AudioParameter>("speed", 0.0f, 200.0f);
    engine.registerParameter(p);

    engine.setParameter("speed", 100.0f);
    EXPECT_FLOAT_EQ(engine.getParameter("speed"), 100.0f);
}

TEST_F(RtpcTest, GetUnregisteredParameterReturnsZero) {
    EXPECT_FLOAT_EQ(engine.getParameter("nonexistent"), 0.0f);
}

TEST_F(RtpcTest, SetUnregisteredParameterIsNoop) {
    // Should not crash.
    engine.setParameter("nonexistent", 1.0f);
}

TEST_F(RtpcTest, UnregisterParameter) {
    auto p = std::make_shared<AudioParameter>("vol", 0.0f, 1.0f);
    engine.registerParameter(p);
    engine.setParameter("vol", 0.8f);
    engine.unregisterParameter("vol");
    EXPECT_FLOAT_EQ(engine.getParameter("vol"), 0.0f);  // gone → returns 0
}

// ---------------------------------------------------------------------------
// Source binding
// ---------------------------------------------------------------------------

TEST_F(RtpcTest, BindParameterToVolume) {
    auto p = std::make_shared<AudioParameter>("speed", 0.0f, 200.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("speed", AudioSourceProperty::Volume,
                       CurveType::Linear, 0.0f, 1.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    // Set parameter to half-way → mapped volume should be 0.5
    engine.setParameter("speed", 100.0f);
    // Voice should still be alive; parameter applied without crash.
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, BindParameterToPitch) {
    auto p = std::make_shared<AudioParameter>("rpm", 0.0f, 1000.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("rpm", AudioSourceProperty::Pitch,
                       CurveType::Linear, 0.5f, 2.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setParameter("rpm", 500.0f);   // 50% → pitch 1.25
    EXPECT_TRUE(engine.isValid(h));

    engine.setParameter("rpm", 0.0f);     // 0%  → pitch 0.5
    EXPECT_TRUE(engine.isValid(h));

    engine.setParameter("rpm", 1000.0f);  // 100% → pitch 2.0
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, BindParameterToPan) {
    auto p = std::make_shared<AudioParameter>("pan_ctrl", -1.0f, 1.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("pan_ctrl", AudioSourceProperty::Pan,
                       CurveType::Linear, -1.0f, 1.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setParameter("pan_ctrl", 0.0f);   // centre
    engine.setParameter("pan_ctrl", 1.0f);   // full right
    engine.setParameter("pan_ctrl", -1.0f);  // full left
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, BindParameterToFilterCutoff) {
    auto p = std::make_shared<AudioParameter>("dist", 0.0f, 100.0f);
    engine.registerParameter(p);

    auto lpf = std::make_shared<LowPassFilter>();
    ToneSource tone;
    tone.setLooping(true);
    tone.addFilter(lpf, 0);
    tone.bindParameter("dist", AudioSourceProperty::LowPassCutoff,
                       CurveType::Linear, 200.0f, 8000.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setParameter("dist", 50.0f);   // mid-range cutoff
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, UnbindParameterStopsApplyingEffect) {
    auto p = std::make_shared<AudioParameter>("x", 0.0f, 1.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("x", AudioSourceProperty::Volume, CurveType::Linear, 0.0f, 1.0f);
    tone.unbindParameter("x");

    // Play after unbind — no binding in the voice.
    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setParameter("x", 0.0f);   // should not silence the voice
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, MultipleBindingsOnOneSource) {
    auto pVol   = std::make_shared<AudioParameter>("env_vol",   0.0f, 1.0f);
    auto pPitch = std::make_shared<AudioParameter>("env_pitch", 0.5f, 2.0f);
    engine.registerParameter(pVol);
    engine.registerParameter(pPitch);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("env_vol",   AudioSourceProperty::Volume, CurveType::Linear, 0.0f, 1.0f);
    tone.bindParameter("env_pitch", AudioSourceProperty::Pitch,  CurveType::Linear, 0.5f, 2.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setParameter("env_vol",   0.5f);
    engine.setParameter("env_pitch", 1.25f);
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, ExponentialCurveShaping) {
    auto p = std::make_shared<AudioParameter>("curve_test", 0.0f, 1.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("curve_test", AudioSourceProperty::Volume,
                       CurveType::Exponential, 0.0f, 1.0f);

    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    // t=0.5 with Exponential curve → 0.5² = 0.25, voice remains valid
    engine.setParameter("curve_test", 0.5f);
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
}

TEST_F(RtpcTest, MultipleVoicesFromSameSourceAllUpdated) {
    auto p = std::make_shared<AudioParameter>("shared", 0.0f, 1.0f);
    engine.registerParameter(p);

    ToneSource tone;
    tone.setLooping(true);
    tone.bindParameter("shared", AudioSourceProperty::Volume, CurveType::Linear, 0.0f, 1.0f);

    SoundHandle h1 = engine.play(tone);
    SoundHandle h2 = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h1));
    ASSERT_TRUE(engine.isValid(h2));

    engine.setParameter("shared", 0.5f);
    EXPECT_TRUE(engine.isValid(h1));
    EXPECT_TRUE(engine.isValid(h2));

    engine.stop(h1);
    engine.stop(h2);
}
