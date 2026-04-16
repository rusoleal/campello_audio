/// @file test_decoders.cpp
/// @brief Universal decoder tests — no audio device and no external files needed.
///
/// WAV tests use a synthetically generated minimal WAV buffer constructed
/// in-memory. OGG and MP3 tests cover error paths only (generating valid
/// compressed streams in-memory requires encoding libraries that we do not
/// carry in this repo; those tests live in tests/platform/ where real
/// asset files can be provided).

#include <gtest/gtest.h>

// Pull in the decoder implementations directly (header-only inclusion).
// The DR_WAV / DR_MP3 macros must NOT be re-defined here — the TUs
// decoder_wav.cpp and decoder_mp3.cpp own those definitions. For the
// universal tests we test via the public source headers, not the decoder
// functions directly.
#include <campello_audio/wav_source.hpp>
#include <campello_audio/ogg_source.hpp>
#include <campello_audio/mp3_source.hpp>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Helper: build a minimal valid WAV buffer in memory.
//
// Format: PCM 16-bit, configurable channel count and sample rate.
// Useful for verifying the decoder without any files on disk.
// ---------------------------------------------------------------------------
namespace {

std::vector<uint8_t> makeWavBuffer(uint32_t sampleRate  = 44100,
                                   uint16_t channels    = 2,
                                   uint32_t frameCount  = 512) {
    const uint32_t bytesPerSample = 2;                           // 16-bit
    const uint32_t dataSize       = frameCount * channels * bytesPerSample;
    const uint32_t riffSize       = 4 + 24 + 8 + dataSize;      // after "RIFF"

    std::vector<uint8_t> buf;
    buf.reserve(8 + riffSize);

    auto push16 = [&](uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };
    auto push32 = [&](uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    auto pushStr = [&](const char* s) {
        while (*s) buf.push_back(static_cast<uint8_t>(*s++));
    };

    // RIFF header
    pushStr("RIFF");  push32(riffSize);  pushStr("WAVE");
    // fmt  chunk
    pushStr("fmt ");  push32(16);
    push16(1);                                                   // PCM
    push16(channels);
    push32(sampleRate);
    push32(sampleRate * channels * bytesPerSample);              // byte rate
    push16(static_cast<uint16_t>(channels * bytesPerSample));    // block align
    push16(16);                                                  // bits/sample
    // data chunk — silence
    pushStr("data");  push32(dataSize);
    buf.resize(buf.size() + dataSize, 0);

    return buf;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// WavSource — loadMem
// ---------------------------------------------------------------------------

TEST(WavSource, DefaultConstructInvalid) {
    WavSource src;
    EXPECT_FLOAT_EQ(src.getVolume(), 1.0f);
    EXPECT_DOUBLE_EQ(src.getDuration(), 0.0);
    EXPECT_EQ(src.getSampleRate(), 0u);
}

TEST(WavSource, LoadMemStereo44100) {
    auto wav = makeWavBuffer(44100, 2, 512);

    WavSource src;
    EXPECT_TRUE(src.loadMem(wav.data(), static_cast<uint32_t>(wav.size())));
    EXPECT_EQ(src.getSampleRate(), 44100u);
    EXPECT_GT(src.getDuration(), 0.0);
}

TEST(WavSource, LoadMemMono22050) {
    auto wav = makeWavBuffer(22050, 1, 1024);

    WavSource src;
    EXPECT_TRUE(src.loadMem(wav.data(), static_cast<uint32_t>(wav.size())));
    EXPECT_EQ(src.getSampleRate(), 22050u);
    // 1024 frames / 22050 Hz ≈ 0.0464 s
    EXPECT_NEAR(src.getDuration(), 1024.0 / 22050.0, 1e-4);
}

TEST(WavSource, LoadMemInvalidDataFails) {
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE};
    WavSource src;
    EXPECT_FALSE(src.loadMem(garbage, sizeof(garbage)));
    EXPECT_EQ(src.getSampleRate(), 0u);
    EXPECT_DOUBLE_EQ(src.getDuration(), 0.0);
}

TEST(WavSource, LoadNonExistentFileFails) {
    WavSource src;
    EXPECT_FALSE(src.load("/nonexistent/path/that/cannot/exist.wav"));
}

TEST(WavSource, ReloadReplacesBuffer) {
    auto wav1 = makeWavBuffer(44100, 2, 256);
    auto wav2 = makeWavBuffer(22050, 1, 512);

    WavSource src;
    ASSERT_TRUE(src.loadMem(wav1.data(), static_cast<uint32_t>(wav1.size())));
    EXPECT_EQ(src.getSampleRate(), 44100u);

    ASSERT_TRUE(src.loadMem(wav2.data(), static_cast<uint32_t>(wav2.size())));
    EXPECT_EQ(src.getSampleRate(), 22050u);
}

TEST(WavSource, VolumeDefaultIsOne) {
    WavSource src;
    EXPECT_FLOAT_EQ(src.getVolume(), 1.0f);
    src.setVolume(0.5f);
    EXPECT_FLOAT_EQ(src.getVolume(), 0.5f);
}

// ---------------------------------------------------------------------------
// OggSource — error paths only (encoding not available in universal tests)
// ---------------------------------------------------------------------------

TEST(OggSource, DefaultConstructInvalid) {
    OggSource src;
    EXPECT_DOUBLE_EQ(src.getDuration(), 0.0);
}

TEST(OggSource, LoadNonExistentFileFails) {
    OggSource src;
    EXPECT_FALSE(src.load("/nonexistent/path/that/cannot/exist.ogg"));
}

TEST(OggSource, LoadMemGarbageFails) {
    const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
    OggSource src;
    EXPECT_FALSE(src.loadMem(garbage, sizeof(garbage)));
}

// ---------------------------------------------------------------------------
// Mp3Source — error paths only
// ---------------------------------------------------------------------------

TEST(Mp3Source, DefaultConstructInvalid) {
    Mp3Source src;
    EXPECT_DOUBLE_EQ(src.getDuration(), 0.0);
}

TEST(Mp3Source, LoadNonExistentFileFails) {
    Mp3Source src;
    EXPECT_FALSE(src.load("/nonexistent/path/that/cannot/exist.mp3"));
}

TEST(Mp3Source, LoadMemGarbageFails) {
    const uint8_t garbage[] = {0x00, 0xFF, 0x12, 0x34};
    Mp3Source src;
    EXPECT_FALSE(src.loadMem(garbage, sizeof(garbage)));
}
