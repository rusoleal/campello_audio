/// @file test_music.cpp
/// @brief Integration tests for MusicTrack — require a real audio device.

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/music_track.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/constants/transition_rule.hpp>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a minimal valid WAV PCM buffer: 1-second mono 44100Hz sine wave.
static std::vector<uint8_t> makeSineWav(float freq = 440.0f,
                                         uint32_t durationFrames = 44100,
                                         uint32_t sampleRate     = 44100) {
    // WAV header + PCM data (16-bit mono).
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

    // RIFF header
    p[0]='R'; p[1]='I'; p[2]='F'; p[3]='F';
    write32(p+4,  totalBytes - 8);
    p[8]='W'; p[9]='A'; p[10]='V'; p[11]='E';
    // fmt chunk
    p[12]='f'; p[13]='m'; p[14]='t'; p[15]=' ';
    write32(p+16, 16);      // chunk size
    write16(p+20, 1);       // PCM
    write16(p+22, 1);       // mono
    write32(p+24, sampleRate);
    write32(p+28, sampleRate * 2);  // byte rate
    write16(p+32, 2);       // block align
    write16(p+34, 16);      // bits per sample
    // data chunk
    p[36]='d'; p[37]='a'; p[38]='t'; p[39]='a';
    write32(p+40, dataBytes);

    // Fill PCM with a sine wave.
    int16_t* samples = reinterpret_cast<int16_t*>(p + 44);
    for (uint32_t i = 0; i < numSamples; ++i) {
        samples[i] = static_cast<int16_t>(
            16000.0f * std::sin(2.0f * 3.14159265f * freq * i / sampleRate));
    }
    return wav;
}

/// Load a WavSource from an in-memory WAV buffer.
static bool loadWav(WavSource& src, const std::vector<uint8_t>& wav) {
    return src.loadMem(wav.data(), static_cast<uint32_t>(wav.size()));
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class MusicTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp() override {
        AudioEngineDescriptor d;
        ASSERT_TRUE(engine.init(d));
    }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// Basic play (no crash)
// ---------------------------------------------------------------------------

TEST_F(MusicTest, PlayMusicTrackDoesNotCrash) {
    auto sectionA = std::make_shared<WavSource>();
    auto wav      = makeSineWav(440.0f, 44100);
    ASSERT_TRUE(loadWav(*sectionA, wav));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(sectionA, "main");
    engine.play(track);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(track.getCurrentSection(), "main");
}

TEST_F(MusicTest, EmptyMusicTrackPlayIsNoop) {
    MusicTrack track;
    engine.play(track);   // should not crash
}

// ---------------------------------------------------------------------------
// Immediate transition
// ---------------------------------------------------------------------------

TEST_F(MusicTest, ImmediateTransitionSwitchesSectionQuickly) {
    auto wavA = makeSineWav(220.0f);
    auto wavB = makeSineWav(880.0f);
    auto srcA = std::make_shared<WavSource>();
    auto srcB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));
    ASSERT_TRUE(loadWav(*srcB, wavB));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(srcA, "a");
    track.addSection(srcB, "b");
    track.addTransition("a", "b", TransitionRule::Immediate, 0.0);
    engine.play(track);
    EXPECT_EQ(track.getCurrentSection(), "a");

    engine.requestMusicTransition("b");

    // One audio buffer at 44100Hz / 512 frames ≈ 11ms; wait 50ms to be safe.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(track.getCurrentSection(), "b");
}

// ---------------------------------------------------------------------------
// OnBar transition — sample-accurate timing check
// ---------------------------------------------------------------------------

TEST_F(MusicTest, OnBarTransitionFiresAtBarBoundary) {
    // BPM=6000, 4/4 → bar = 60/6000 * 4 = 0.04s = 40ms
    // Request the transition, wait 200ms → at least 5 bars should have passed.
    const float fastBpm = 6000.0f;

    auto wavA = makeSineWav(220.0f);
    auto wavB = makeSineWav(440.0f);
    auto srcA = std::make_shared<WavSource>();
    auto srcB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));
    ASSERT_TRUE(loadWav(*srcB, wavB));

    MusicTrack track;
    track.setBpm(fastBpm);
    track.setTimeSignature(4, 4);
    track.addSection(srcA, "a");
    track.addSection(srcB, "b");
    track.addTransition("a", "b", TransitionRule::OnBar, 0.0);
    engine.play(track);
    EXPECT_EQ(track.getCurrentSection(), "a");

    engine.requestMusicTransition("b");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(track.getCurrentSection(), "b");
}

// ---------------------------------------------------------------------------
// OnBeat transition
// ---------------------------------------------------------------------------

TEST_F(MusicTest, OnBeatTransitionFires) {
    // BPM=6000 → beat = 60/6000 = 0.01s = 10ms
    auto wavA = makeSineWav(220.0f);
    auto wavB = makeSineWav(440.0f);
    auto srcA = std::make_shared<WavSource>();
    auto srcB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));
    ASSERT_TRUE(loadWav(*srcB, wavB));

    MusicTrack track;
    track.setBpm(6000.0f);
    track.addSection(srcA, "a");
    track.addSection(srcB, "b");
    track.addTransition("a", "b", TransitionRule::OnBeat, 0.0);
    engine.play(track);

    engine.requestMusicTransition("b");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(track.getCurrentSection(), "b");
}

// ---------------------------------------------------------------------------
// OnNextSection transition
// ---------------------------------------------------------------------------

TEST_F(MusicTest, OnNextSectionTransitionFires) {
    // Section A is a very short WAV (441 frames = 10ms at 44100Hz).
    auto wavA = makeSineWav(220.0f, 441);   // 10ms section
    auto wavB = makeSineWav(440.0f);
    auto srcA = std::make_shared<WavSource>();
    auto srcB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));
    ASSERT_TRUE(loadWav(*srcB, wavB));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(srcA, "a");
    track.addSection(srcB, "b");
    track.addTransition("a", "b", TransitionRule::OnNextSection, 0.0);
    engine.play(track);

    engine.requestMusicTransition("b");
    // Section A is 10ms; wait 200ms — it should have looped at least once.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(track.getCurrentSection(), "b");
}

// ---------------------------------------------------------------------------
// CrossFade transition
// ---------------------------------------------------------------------------

TEST_F(MusicTest, CrossFadeTransitionCompletesCleanly) {
    auto wavA = makeSineWav(220.0f);
    auto wavB = makeSineWav(440.0f);
    auto srcA = std::make_shared<WavSource>();
    auto srcB = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));
    ASSERT_TRUE(loadWav(*srcB, wavB));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(srcA, "a");
    track.addSection(srcB, "b");
    track.addTransition("a", "b", TransitionRule::CrossFade, 0.05); // 50ms fade
    engine.play(track);

    engine.requestMusicTransition("b");
    // Wait for fade to complete (50ms) + buffer (50ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(track.getCurrentSection(), "b");
}

// ---------------------------------------------------------------------------
// getCurrentBeat stays in range
// ---------------------------------------------------------------------------

TEST_F(MusicTest, CurrentBeatStaysInRange) {
    auto wavA = makeSineWav();
    auto srcA = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));

    MusicTrack track;
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection(srcA, "main");
    engine.play(track);

    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const float beat = track.getCurrentBeat();
        EXPECT_GE(beat, 0.0f);
        EXPECT_LT(beat, 4.0f);
    }
}

// ---------------------------------------------------------------------------
// requestMusicTransition on non-existent section is a no-op
// ---------------------------------------------------------------------------

TEST_F(MusicTest, RequestNonexistentSectionIsNoop) {
    auto wavA = makeSineWav();
    auto srcA = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(srcA, "main");
    engine.play(track);

    engine.requestMusicTransition("nonexistent");  // should not crash
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(track.getCurrentSection(), "main");
}

// ---------------------------------------------------------------------------
// requestMusicTransition to same section is a no-op
// ---------------------------------------------------------------------------

TEST_F(MusicTest, RequestCurrentSectionIsNoop) {
    auto wavA = makeSineWav();
    auto srcA = std::make_shared<WavSource>();
    ASSERT_TRUE(loadWav(*srcA, wavA));

    MusicTrack track;
    track.setBpm(120.0f);
    track.addSection(srcA, "main");
    engine.play(track);

    engine.requestMusicTransition("main");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(track.getCurrentSection(), "main");
    EXPECT_EQ(track.getPendingSection(), "");
}
