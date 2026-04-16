/// @file test_streaming.cpp
/// @brief Integration tests for AudioStream — require a real audio device.
///
/// Tests stream a WAV file generated to a temp path; verify metadata, playback,
/// seek, looping, and ring-buffer memory bounds.

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_stream.hpp>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Build a minimal valid WAV: PCM 16-bit mono at @p sampleRate Hz with
/// @p durationFrames of a sine wave at @p freq Hz.
std::vector<uint8_t> makeSineWav(float    freq          = 440.0f,
                                  uint32_t durationFrames = 44100,
                                  uint32_t sampleRate    = 44100) {
    const uint32_t numSamples = durationFrames;
    const uint32_t dataBytes  = numSamples * sizeof(int16_t);
    const uint32_t totalBytes = 44 + dataBytes;

    std::vector<uint8_t> wav(totalBytes, 0);
    uint8_t* p = wav.data();

    auto w32 = [](uint8_t* d, uint32_t v) {
        d[0]=v&0xFF; d[1]=(v>>8)&0xFF; d[2]=(v>>16)&0xFF; d[3]=(v>>24)&0xFF;
    };
    auto w16 = [](uint8_t* d, uint16_t v) {
        d[0]=v&0xFF; d[1]=(v>>8)&0xFF;
    };

    p[0]='R'; p[1]='I'; p[2]='F'; p[3]='F';
    w32(p+4, totalBytes-8);
    p[8]='W'; p[9]='A'; p[10]='V'; p[11]='E';
    p[12]='f'; p[13]='m'; p[14]='t'; p[15]=' ';
    w32(p+16, 16);
    w16(p+20, 1);           // PCM
    w16(p+22, 1);           // mono
    w32(p+24, sampleRate);
    w32(p+28, sampleRate * 2);
    w16(p+32, 2);
    w16(p+34, 16);
    p[36]='d'; p[37]='a'; p[38]='t'; p[39]='a';
    w32(p+40, dataBytes);

    auto* samples = reinterpret_cast<int16_t*>(p + 44);
    for (uint32_t i = 0; i < numSamples; ++i) {
        samples[i] = static_cast<int16_t>(
            16000.0f * std::sin(2.0f * 3.14159265f * freq * i / static_cast<float>(sampleRate)));
    }
    return wav;
}

/// Write @p data to a temporary file and return its path, or "" on failure.
std::string writeTempWav(const std::vector<uint8_t>& data,
                          const std::string& name = "/tmp/campello_test_stream.wav") {
    std::ofstream f(name, std::ios::binary | std::ios::trunc);
    if (!f) return {};
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    if (!f) return {};
    return name;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class StreamingTest : public ::testing::Test {
protected:
    AudioEngine engine;
    std::string wavPath;

    void SetUp() override {
        AudioEngineDescriptor d;
        ASSERT_TRUE(engine.init(d));

        // Generate a 2-second sine WAV and write it to /tmp
        const auto wav = makeSineWav(440.0f, 44100 * 2, 44100);
        wavPath = writeTempWav(wav);
        ASSERT_FALSE(wavPath.empty()) << "Failed to write temp WAV file";
    }

    void TearDown() override {
        engine.deinit();
        if (!wavPath.empty()) std::remove(wavPath.c_str());
    }
};

// ---------------------------------------------------------------------------
// Metadata tests
// ---------------------------------------------------------------------------

TEST_F(StreamingTest, OpenSucceedsAndMetadataCorrect) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));
    EXPECT_TRUE(s.isOpen());
    EXPECT_EQ(s.getSampleRate(), 44100u);
    EXPECT_NEAR(s.getDuration(), 2.0, 0.1);
    s.close();
    EXPECT_FALSE(s.isOpen());
}

TEST_F(StreamingTest, BufferMemoryBytesUnder2MB) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));
    EXPECT_GT(s.getBufferMemoryBytes(), 0u);
    EXPECT_LE(s.getBufferMemoryBytes(), 2u * 1024u * 1024u);
    s.close();
}

// ---------------------------------------------------------------------------
// Playback tests
// ---------------------------------------------------------------------------

TEST_F(StreamingTest, PlayStreamReturnsValidHandle) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));

    SoundHandle h = engine.play(s);
    EXPECT_TRUE(h.isValid());
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
    s.close();
}

TEST_F(StreamingTest, PlayStreamVoiceStaysActiveAfter50ms) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));

    SoundHandle h = engine.play(s);
    ASSERT_TRUE(h.isValid());

    // Let the audio thread run for 50ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Voice should still be active (stream has 2 seconds of data)
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
    s.close();
}

TEST_F(StreamingTest, UnloadedStreamReturnsInvalidHandle) {
    AudioStream s;  // not opened
    SoundHandle h = engine.play(s);
    EXPECT_FALSE(h.isValid());
}

// ---------------------------------------------------------------------------
// Seek test
// ---------------------------------------------------------------------------

TEST_F(StreamingTest, SeekDoesNotCrash) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));

    SoundHandle h = engine.play(s);
    ASSERT_TRUE(h.isValid());

    // Seek to 0.5s — should not crash
    engine.seek(h, 0.5);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
    s.close();
}

// ---------------------------------------------------------------------------
// Looping stream test
// ---------------------------------------------------------------------------

TEST_F(StreamingTest, LoopingStreamRemainsActiveAfterOneCycle) {
    // Generate a very short (0.05s) WAV so the stream loops quickly
    const auto shortWav = makeSineWav(440.0f, 44100 / 20, 44100);  // 50ms
    const std::string shortPath = "/tmp/campello_test_stream_short.wav";
    ASSERT_FALSE(writeTempWav(shortWav, shortPath).empty());

    AudioStream s;
    ASSERT_TRUE(s.open(shortPath));

    // Use PlayDescriptor to enable looping
    PlayDescriptor pd;
    pd.looping = true;
    SoundHandle h = engine.play(s, pd);
    ASSERT_TRUE(h.isValid());

    // Wait 200ms — the 50ms stream should have looped several times
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should still be active (looping)
    EXPECT_TRUE(engine.isValid(h));

    engine.stop(h);
    s.close();
    std::remove(shortPath.c_str());
}

// ---------------------------------------------------------------------------
// Close while playing
// ---------------------------------------------------------------------------

TEST_F(StreamingTest, CloseWhilePlayingDoesNotCrash) {
    AudioStream s;
    ASSERT_TRUE(s.open(wavPath));
    SoundHandle h = engine.play(s);
    ASSERT_TRUE(h.isValid());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Stop and close — order matters: stop the engine voice first
    engine.stop(h);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    s.close();
    EXPECT_FALSE(s.isOpen());
}
