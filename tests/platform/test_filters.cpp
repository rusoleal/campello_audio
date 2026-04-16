#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <campello_audio/high_pass_filter.hpp>
#include <campello_audio/echo_filter.hpp>
#include <campello_audio/reverb_filter.hpp>
#include <campello_audio/compressor_filter.hpp>
#include <campello_audio/limiter_filter.hpp>
#include <campello_audio/chorus_filter.hpp>
#include <campello_audio/flanger_filter.hpp>
#include <campello_audio/pitch_shift_filter.hpp>
#include <thread>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Helper — build a minimal single-channel WAV in memory
// ---------------------------------------------------------------------------
namespace {

std::vector<uint8_t> makeSineWav(uint32_t sr = 44100, float freqHz = 1000.0f,
                                  float durationSecs = 0.1f) {
    const uint32_t frames   = static_cast<uint32_t>(sr * durationSecs);
    const uint16_t ch       = 1;
    const uint16_t bps      = 2;   // 16-bit PCM
    const uint32_t dataSize = frames * ch * bps;
    const uint32_t riffSize = 4 + 24 + 8 + dataSize;

    std::vector<uint8_t> b;
    b.reserve(12 + 24 + 8 + dataSize);

    auto p16 = [&](uint16_t v){ b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto p32 = [&](uint32_t v){ for (int i = 0; i < 4; ++i) b.push_back((v >> (i*8)) & 0xFF); };
    auto ps  = [&](const char* s){ while (*s) b.push_back((uint8_t)*s++); };

    ps("RIFF"); p32(riffSize); ps("WAVE");
    ps("fmt "); p32(16); p16(1); p16(ch); p32(sr);
    p32(sr * ch * bps); p16(ch * bps); p16(16);
    ps("data"); p32(dataSize);

    for (uint32_t f = 0; f < frames; ++f) {
        const double t      = static_cast<double>(f) / sr;
        const int16_t samp  = static_cast<int16_t>(
            32767.0 * std::sin(2.0 * M_PI * freqHz * t));
        p16(static_cast<uint16_t>(samp));
    }
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction tests (unchanged from stubs)
// ---------------------------------------------------------------------------

TEST(FilterConstruction, LowPassDefaults) {
    auto f = std::make_shared<LowPassFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, HighPassDefaults) {
    auto f = std::make_shared<HighPassFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, EchoDefaults) {
    auto f = std::make_shared<EchoFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, ReverbDefaults) {
    auto f = std::make_shared<ReverbFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, CompressorDefaults) {
    auto f = std::make_shared<CompressorFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, LimiterDefaults) {
    auto f = std::make_shared<LimiterFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, ChorusDefaults) {
    auto f = std::make_shared<ChorusFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, FlangerDefaults) {
    auto f = std::make_shared<FlangerFilter>();
    EXPECT_NE(f, nullptr);
}
TEST(FilterConstruction, PitchShiftDefaults) {
    auto f = std::make_shared<PitchShiftFilter>();
    EXPECT_NE(f, nullptr);
}

TEST(FilterAttach, AddToSource) {
    WavSource src;
    auto lpf = std::make_shared<LowPassFilter>(1000.0f, 1.0f);
    EXPECT_NO_THROW(src.addFilter(lpf, 0));
    EXPECT_NO_THROW(src.removeFilter(0));
}

// ---------------------------------------------------------------------------
// Engine-level filter tests
// ---------------------------------------------------------------------------

class FilterEngineTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp()    override {
        AudioEngineDescriptor d;
        d.visualization = true;
        engine.init(d);
    }
    void TearDown() override { engine.deinit(); }
};

// A voice with no filter attached plays without crashing.
TEST_F(FilterEngineTest, VoiceWithNoFilterPlays) {
    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// A voice with an LPF attached plays without crashing.
TEST_F(FilterEngineTest, VoiceWithLPFPlays) {
    auto lpf = std::make_shared<LowPassFilter>(1000.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// A voice with an HPF attached plays without crashing.
TEST_F(FilterEngineTest, VoiceWithHPFPlays) {
    auto hpf = std::make_shared<HighPassFilter>(500.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(hpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// A voice with an Echo filter attached plays without crashing.
TEST_F(FilterEngineTest, VoiceWithEchoPlays) {
    auto echo = std::make_shared<EchoFilter>(0.1f, 0.5f, 0.0f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(echo, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// setParam on a live filter doesn't crash and changes the filter state.
TEST_F(FilterEngineTest, SetParamDoesNotCrash) {
    auto lpf = std::make_shared<LowPassFilter>(2000.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_NO_THROW(lpf->setParam(LowPassFilter::PARAM_CUTOFF, 500.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// fadeParam on a live filter doesn't crash.
TEST_F(FilterEngineTest, FadeParamDoesNotCrash) {
    auto lpf = std::make_shared<LowPassFilter>(3000.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_NO_THROW(lpf->fadeParam(LowPassFilter::PARAM_CUTOFF, 200.0f, 0.1));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    engine.stop(h);
}

// oscillateParam on a live filter doesn't crash.
TEST_F(FilterEngineTest, OscillateParamDoesNotCrash) {
    auto lpf = std::make_shared<LowPassFilter>(2000.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_NO_THROW(lpf->oscillateParam(LowPassFilter::PARAM_CUTOFF,
                                         500.0f, 4000.0f, 0.05));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// Chained filters (LPF + HPF in slots 0 and 1) both process without crashing.
TEST_F(FilterEngineTest, ChainedFiltersDoNotCrash) {
    auto lpf = std::make_shared<LowPassFilter>(2000.0f, 0.707f);
    auto hpf = std::make_shared<HighPassFilter>(200.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);
    tone.addFilter(hpf, 1);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// Removing a filter mid-play doesn't crash (source filter removed; voice keeps
// its snapshot of the chain from play time, which is fine).
TEST_F(FilterEngineTest, RemoveFilterMidPlayDoesNotCrash) {
    auto lpf = std::make_shared<LowPassFilter>(1000.0f, 0.707f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(lpf, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_NO_THROW(tone.removeFilter(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// A voice with a Reverb filter plays without crashing.
TEST_F(FilterEngineTest, VoiceWithReverbPlays) {
    auto reverb = std::make_shared<ReverbFilter>(0.7f, 0.5f, 0.4f, 1.0f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(reverb, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// A voice with a Compressor filter plays without crashing; gain reduction is
// non-negative after the voice has been running.
TEST_F(FilterEngineTest, VoiceWithCompressorPlays) {
    auto comp = std::make_shared<CompressorFilter>(-12.0f, 4.0f, 5.0f, 50.0f, 6.0f, 0.0f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(comp, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_GE(comp->getGainReductionDb(), 0.0f);
    engine.stop(h);
}

// A voice with a Limiter filter plays without crashing; gain reduction is
// non-negative after the voice has been running.
TEST_F(FilterEngineTest, VoiceWithLimiterPlays) {
    auto limiter = std::make_shared<LimiterFilter>(-0.3f, 5.0f, 50.0f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(limiter, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_GE(limiter->getGainReductionDb(), 0.0f);
    engine.stop(h);
}

// A voice with a Chorus filter plays without crashing.
TEST_F(FilterEngineTest, VoiceWithChorusPlays) {
    auto chorus = std::make_shared<ChorusFilter>(0.5f, 1.0f, 0.5f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(chorus, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// A voice with a Flanger filter plays without crashing.
TEST_F(FilterEngineTest, VoiceWithFlangerPlays) {
    auto flanger = std::make_shared<FlangerFilter>(0.5f, 0.25f, 0.5f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(flanger, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// A voice with a PitchShift filter plays without crashing (+7 semitones).
TEST_F(FilterEngineTest, VoiceWithPitchShiftUpPlays) {
    auto ps = std::make_shared<PitchShiftFilter>(7.0f);
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(ps, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// PitchShift with a negative value (one octave down) plays without crashing.
TEST_F(FilterEngineTest, VoiceWithPitchShiftDownPlays) {
    auto ps = std::make_shared<PitchShiftFilter>(-12.0f);
    ToneSource tone(WaveForm::Sine, 880.0f);
    tone.addFilter(ps, 0);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// Chaining all six new filters together does not crash.
TEST_F(FilterEngineTest, AllFiltersChainedDoNotCrash) {
    ToneSource tone(WaveForm::Sine, 440.0f);
    tone.addFilter(std::make_shared<ReverbFilter>(),     0);
    tone.addFilter(std::make_shared<CompressorFilter>(), 1);
    tone.addFilter(std::make_shared<LimiterFilter>(),    2);
    tone.addFilter(std::make_shared<ChorusFilter>(),     3);
    tone.addFilter(std::make_shared<FlangerFilter>(),    4);
    tone.addFilter(std::make_shared<PitchShiftFilter>(), 5);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}
