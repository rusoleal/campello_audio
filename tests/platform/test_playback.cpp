#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/tone_source.hpp>

using namespace systems::leal::campello_audio;

class PlaybackTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp()    override { engine.init({}); }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// Unloaded sources must not produce a valid handle.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, InvalidHandleWhenSourceEmpty) {
    WavSource src;   // not loaded
    SoundHandle h = engine.play(src);
    EXPECT_FALSE(h.isValid());
    EXPECT_FALSE(engine.isValid(h));
}

// ---------------------------------------------------------------------------
// ToneSource — procedural; always ready without loading.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, ToneSourcePlaysAndIsValid) {
    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(h.isValid());
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
    EXPECT_FALSE(engine.isValid(h));
}

TEST_F(PlaybackTest, ToneActiveVoiceCountIncrements) {
    ToneSource tone;
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
    SoundHandle h = engine.play(tone);
    EXPECT_EQ(engine.getActiveVoiceCount(), 1u);
    engine.stop(h);
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}

TEST_F(PlaybackTest, MultipleSimultaneousTones) {
    ToneSource t1(WaveForm::Sine,   440.0f);
    ToneSource t2(WaveForm::Square, 880.0f);
    SoundHandle h1 = engine.play(t1);
    SoundHandle h2 = engine.play(t2);
    EXPECT_TRUE(engine.isValid(h1));
    EXPECT_TRUE(engine.isValid(h2));
    EXPECT_EQ(engine.getActiveVoiceCount(), 2u);
    engine.stopAll();
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}

// ---------------------------------------------------------------------------
// WavSource loaded from memory.
// ---------------------------------------------------------------------------

namespace {
// Minimal valid WAV: PCM 16-bit mono 44100 Hz, 512 frames of silence.
std::vector<uint8_t> makeSilentWav(uint32_t sr = 44100, uint16_t ch = 1,
                                    uint32_t frames = 512) {
    const uint32_t bps      = 2;
    const uint32_t dataSize = frames * ch * bps;
    const uint32_t riffSize = 4 + 24 + 8 + dataSize;
    std::vector<uint8_t> b;
    auto p16 = [&](uint16_t v){ b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto p32 = [&](uint32_t v){ for(int i=0;i<4;++i){b.push_back((v>>(i*8))&0xFF);} };
    auto ps  = [&](const char* s){ while(*s) b.push_back((uint8_t)*s++); };
    ps("RIFF"); p32(riffSize); ps("WAVE");
    ps("fmt "); p32(16); p16(1); p16(ch); p32(sr);
    p32(sr*ch*bps); p16(ch*bps); p16(16);
    ps("data"); p32(dataSize);
    b.resize(b.size() + dataSize, 0);
    return b;
}
}

TEST_F(PlaybackTest, WavSourcePlaysAfterLoad) {
    WavSource src;
    auto wav = makeSilentWav();
    ASSERT_TRUE(src.loadMem(wav.data(), static_cast<uint32_t>(wav.size())));

    SoundHandle h = engine.play(src);
    EXPECT_TRUE(h.isValid());
    EXPECT_TRUE(engine.isValid(h));
    EXPECT_EQ(engine.getActiveVoiceCount(), 1u);
    engine.stop(h);
    EXPECT_FALSE(engine.isValid(h));
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}

// ---------------------------------------------------------------------------
// Pause / resume.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, PauseAndResume) {
    ToneSource tone;
    SoundHandle h = engine.play(tone);
    EXPECT_FALSE(engine.isPaused(h));

    engine.pause(h);
    EXPECT_TRUE(engine.isPaused(h));

    engine.resume(h);
    EXPECT_FALSE(engine.isPaused(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Per-voice parameter setters do not crash and voice remains valid.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, ParameterSettersWork) {
    ToneSource tone;
    SoundHandle h = engine.play(tone);
    ASSERT_TRUE(engine.isValid(h));

    engine.setVolume(h, 0.5f);
    engine.setPan(h, -0.3f);
    engine.setPitch(h, 1.5f);
    engine.setProtect(h, true);
    engine.setLooping(h, true);
    EXPECT_TRUE(engine.isValid(h));  // still alive after parameter changes

    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Seek moves playback position.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, SeekChangesPosition) {
    WavSource src;
    auto wav = makeSilentWav(44100, 1, 44100);  // 1 second
    ASSERT_TRUE(src.loadMem(wav.data(), static_cast<uint32_t>(wav.size())));

    SoundHandle h = engine.play(src);
    ASSERT_TRUE(engine.isValid(h));

    engine.seek(h, 0.5);
    const double pos = engine.getPosition(h);
    EXPECT_NEAR(pos, 0.5, 0.01);
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// PlayDescriptor overrides.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, PlayDescriptorVolumeAndPan) {
    ToneSource tone;
    PlayDescriptor pd;
    pd.volume = 0.3f;
    pd.pan    = 0.7f;
    SoundHandle h = engine.play(tone, pd);
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

TEST_F(PlaybackTest, PlayDescriptorPausedStartsPaused) {
    ToneSource tone;
    PlayDescriptor pd;
    pd.paused = true;
    SoundHandle h = engine.play(tone, pd);
    EXPECT_TRUE(engine.isValid(h));
    EXPECT_TRUE(engine.isPaused(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// stopAll clears all voices.
// ---------------------------------------------------------------------------

TEST_F(PlaybackTest, StopAllClearsVoices) {
    ToneSource t1, t2, t3;
    engine.play(t1); engine.play(t2); engine.play(t3);
    EXPECT_EQ(engine.getActiveVoiceCount(), 3u);
    engine.stopAll();
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
}
