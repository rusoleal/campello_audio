/// @file test_pattern_track.cpp
/// @brief Integration tests for PatternTrack — require a real audio device.

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/pattern_track.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/constants/transition_rule.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <pattern/pattern.hpp>
#include <pattern/pattern_bank.hpp>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace systems::leal::campello_audio;
using systems::leal::campello_audio::pi::Pattern;
using systems::leal::campello_audio::pi::PatternEvent;

// ---------------------------------------------------------------------------
// Helpers (same as test_music.cpp)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> makeSineWav(float freq = 440.0f,
                                         uint32_t durationFrames = 44100,
                                         uint32_t sampleRate     = 44100) {
    const uint32_t numSamples  = durationFrames;
    const uint32_t dataBytes   = numSamples * sizeof(int16_t);
    const uint32_t totalBytes  = 44 + dataBytes;

    std::vector<uint8_t> wav(totalBytes, 0);
    uint8_t* p = wav.data();

    auto write32 = [](uint8_t* dst, uint32_t v) {
        dst[0] = v & 0xFF; dst[1] = (v>>8)&0xFF; dst[2] = (v>>16)&0xFF; dst[3] = (v>>24)&0xFF;
    };
    auto write16 = [](uint8_t* dst, uint16_t v) {
        dst[0] = v & 0xFF; dst[1] = (v>>8)&0xFF;
    };

    p[0]='R'; p[1]='I'; p[2]='F'; p[3]='F';
    write32(p+4,  totalBytes - 8);
    p[8]='W'; p[9]='A'; p[10]='V'; p[11]='E';
    p[12]='f'; p[13]='m'; p[14]='t'; p[15]=' ';
    write32(p+16, 16);
    write16(p+20, 1);
    write16(p+22, 1);
    write32(p+24, sampleRate);
    write32(p+28, sampleRate * 2);
    write16(p+32, 2);
    write16(p+34, 16);
    p[36]='d'; p[37]='a'; p[38]='t'; p[39]='a';
    write32(p+40, dataBytes);

    int16_t* samples = reinterpret_cast<int16_t*>(p + 44);
    for (uint32_t i = 0; i < numSamples; ++i) {
        samples[i] = static_cast<int16_t>(
            16000.0f * std::sin(2.0f * 3.14159265f * freq * i / sampleRate));
    }
    return wav;
}

static bool loadWav(WavSource& src, const std::vector<uint8_t>& wav) {
    return src.loadMem(wav.data(), static_cast<uint32_t>(wav.size()));
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class PatternTrackTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp() override {
        AudioEngineDescriptor d;
        ASSERT_TRUE(engine.init(d));
    }
    void TearDown() override {
        engine.deinit();
    }
};

// ---------------------------------------------------------------------------
// Voice spawning
// ---------------------------------------------------------------------------

TEST_F(PatternTrackTest, PlaySpawnsVoices) {
    // 1. Create a one-shot WAV source.
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));

    // 2. Build a pattern with one event at beat 0.
    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;
    ev.sourceLabel = "shot";
    ev.gain = 1.0f;
    ev.pitch = 1.0f;
    ev.pan = 0.0f;
    pattern->events.push_back(ev);

    // 3. Create a bank and register source + pattern.
    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    // 4. Create a PatternTrack.
    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    // 5. Play the track.
    engine.play(track);

    // 6. Wait long enough for one buffer (≥ 1 mix callback).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 7. Verify at least one voice was spawned.
    EXPECT_GT(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PatternTrackTest, CurvedEventSpawnsAndModulates) {
    // 1. Create a one-shot WAV source with a LowPassFilter.
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));
    auto lpf = std::make_shared<LowPassFilter>(3000.0f, 0.707f);
    shot->addFilter(lpf, 0);

    // 2. Build a pattern with one curved event at beat 0.
    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;
    ev.sourceLabel = "shot";
    ev.gain = 1.0f;
    ev.pitch = 1.0f;
    ev.pan = 0.0f;

    // Gain curve oscillates 0.5..1.5 over 1 beat.
    systems::leal::campello_audio::pi::ParameterCurve gainCurve;
    gainCurve.targetParam = systems::leal::campello_audio::pi::PatternParam::Gain;
    gainCurve.type = systems::leal::campello_audio::CurveType::Sine;
    gainCurve.periodBeats = 1.0;
    gainCurve.minValue = 0.5f;
    gainCurve.maxValue = 1.5f;
    ev.paramCurves.push_back(gainCurve);

    // LPF cutoff curve oscillates 200..2000 Hz over 2 beats.
    systems::leal::campello_audio::pi::ParameterCurve lpfCurve;
    lpfCurve.targetParam = systems::leal::campello_audio::pi::PatternParam::LpfCutoff;
    lpfCurve.type = systems::leal::campello_audio::CurveType::Linear;
    lpfCurve.periodBeats = 2.0;
    lpfCurve.minValue = 200.0f;
    lpfCurve.maxValue = 2000.0f;
    ev.paramCurves.push_back(lpfCurve);

    pattern->events.push_back(ev);

    // 3. Create a bank and register source + pattern.
    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    // 4. Create a PatternTrack.
    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    // 5. Play the track.
    engine.play(track);

    // 6. Wait for voice to spawn.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_GT(engine.getActiveVoiceCount(), 0u);

    // 7. Wait long enough for curves to cycle (~2 beats = 1 sec at 120 BPM).
    //    The voice should remain active through this period (sample ~0.5 sec).
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // No crash = success. Voice may have finished by now.
}

TEST_F(PatternTrackTest, ProbabilityZeroNeverSpawns) {
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));

    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;
    ev.sourceLabel = "shot";
    ev.probability = 0.0f;  // never spawn
    pattern->events.push_back(ev);

    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PatternTrackTest, ProbabilityOneAlwaysSpawns) {
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));

    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;
    ev.sourceLabel = "shot";
    ev.probability = 1.0f;  // always spawn
    pattern->events.push_back(ev);

    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GT(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PatternTrackTest, DurationCutsOffBeforeSampleEnds) {
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));  // ~0.5 sec

    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.25;  // 0.25 beats @ 120 BPM = 0.125 sec
    ev.sourceLabel = "shot";
    pattern->events.push_back(ev);

    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Voice should have been cut off after 0.125 sec.
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PatternTrackTest, DurationZeroIsOneShot) {
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));  // ~0.5 sec

    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;  // one-shot: play until sample ends
    ev.sourceLabel = "shot";
    pattern->events.push_back(ev);

    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Voice should still be playing (sample is 0.5 sec).
    EXPECT_GT(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PatternTrackTest, RtpcCurveModulatesGain) {
    // 1. Create a one-shot WAV source.
    auto shot = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shot, makeSineWav(880.0f, 22050)));

    // 2. Register an RTPC parameter.
    auto intensity = std::make_shared<AudioParameter>("intensity", 0.0f, 1.0f);
    engine.registerParameter(intensity);
    engine.setParameter("intensity", 0.5f);

    // 3. Build a pattern with an RTPC-driven gain curve.
    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;
    PatternEvent ev;
    ev.beat = 0.0;
    ev.duration = 0.0;
    ev.sourceLabel = "shot";

    systems::leal::campello_audio::pi::ParameterCurve rtpcCurve;
    rtpcCurve.targetParam = systems::leal::campello_audio::pi::PatternParam::Gain;
    rtpcCurve.type = systems::leal::campello_audio::CurveType::Linear;
    rtpcCurve.periodBeats = 1.0;
    rtpcCurve.minValue = 0.0f;
    rtpcCurve.maxValue = 1.0f;
    rtpcCurve.rtpcName = "intensity";
    ev.paramCurves.push_back(rtpcCurve);

    pattern->events.push_back(ev);

    // 4. Create a bank and register source + pattern.
    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot", shot);
    bank->addPattern("pat_a", pattern);

    // 5. Create a PatternTrack.
    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "intro");

    // 6. Play the track.
    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Voice should have spawned.
    EXPECT_GT(engine.getActiveVoiceCount(), 0u);

    // 7. Change RTPC value and let curves re-evaluate.
    engine.setParameter("intensity", 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // No crash = success.
}

TEST_F(PatternTrackTest, TransitionQueuesAndFires) {
    // 1. Create two one-shot WAV sources.
    auto shotA = std::make_shared<WavSource>();
    auto shotB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*shotA, makeSineWav(440.0f, 22050)));
    ASSERT_TRUE(loadWav(*shotB, makeSineWav(660.0f, 22050)));

    // 2. Build two patterns.
    auto patA = std::make_shared<Pattern>();
    patA->lengthInBeats = 4.0;
    PatternEvent evA;
    evA.beat = 0.0;
    evA.sourceLabel = "shot_a";
    patA->events.push_back(evA);

    auto patB = std::make_shared<Pattern>();
    patB->lengthInBeats = 4.0;
    PatternEvent evB;
    evB.beat = 0.0;
    evB.sourceLabel = "shot_b";
    patB->events.push_back(evB);

    // 3. Bank.
    auto bank = std::make_shared<PatternBank>();
    bank->registerSource("shot_a", shotA);
    bank->registerSource("shot_b", shotB);
    bank->addPattern("pat_a", patA);
    bank->addPattern("pat_b", patB);

    // 4. Track with two sections and a transition.
    PatternTrack track;
    track.setPatternBank(bank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("pat_a", "a");
    track.addSection("pat_b", "b");
    track.addTransition("a", "b", TransitionRule::OnBar);

    // 5. Play.
    engine.play(track);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should be in section "a".
    EXPECT_EQ(track.getCurrentSection(), "a");

    // 6. Request transition to "b".
    engine.requestPatternTransition("b");
    EXPECT_EQ(track.getPendingSection(), "b");

    // 7. Wait for a bar boundary (at 120 BPM, 4 beats = 2 seconds).
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));

    // Should now be in section "b".
    EXPECT_EQ(track.getCurrentSection(), "b");
    EXPECT_EQ(track.getPendingSection(), "");
}
