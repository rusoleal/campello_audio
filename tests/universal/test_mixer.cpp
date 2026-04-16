/// @file test_mixer.cpp
/// @brief Universal mixer tests — no audio device required.
///
/// Tests operate on MixerData directly, bypassing any audio backend.
/// Voices are configured manually (locking voiceMutex) then mixSamples()
/// is called to produce the output buffer.

#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "mixer.hpp"
#include "voice_manager.hpp"
#include "decoder.hpp"
#include <campello_audio/constants/resample_quality.hpp>

using namespace systems::leal::campello_audio::pi;
using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

/// Configure a MixerData in-place.
/// MixerData contains std::mutex and is therefore non-copyable/non-movable.
void setupMixer(MixerData& mx, uint32_t maxVoices = 8,
                uint32_t sampleRate = 44100, uint32_t channels = 2) {
    mx.config.sampleRate = sampleRate;
    mx.config.channels   = channels;
    mx.config.maxVoices  = maxVoices;
    VoiceManager::init(mx);
}

/// Build a DecodedBuffer filled with a constant value.
DecodedBuffer makeConstantBuffer(float value, uint64_t frames = 4096,
                                  uint32_t channels = 1,
                                  uint32_t sampleRate = 44100) {
    DecodedBuffer buf;
    buf.channels   = channels;
    buf.sampleRate = sampleRate;
    buf.frameCount = frames;
    buf.samples.assign(frames * channels, value);
    return buf;
}

/// RMS of a float buffer slice.
float rms(const float* p, uint32_t n) {
    if (n == 0) return 0.0f;
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; ++i) sum += p[i] * p[i];
    return std::sqrt(sum / static_cast<float>(n));
}

/// True if every sample is exactly zero.
bool isAllZero(const float* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) if (p[i] != 0.0f) return false;
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// VoiceManager — allocation and eviction
// ---------------------------------------------------------------------------

TEST(VoiceManager, AllocAndFreeBasic) {
    MixerData mx;
    setupMixer(mx, 4);

    uint64_t id1, id2;
    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v1 = VoiceManager::allocate(mx);
        Voice* v2 = VoiceManager::allocate(mx);
        ASSERT_NE(v1, nullptr);
        ASSERT_NE(v2, nullptr);
        EXPECT_NE(v1->id, v2->id);
        id1 = v1->id; id2 = v2->id;
    }
    {
        std::lock_guard lk(mx.voiceMutex);
        VoiceManager::free(mx, id1);
        EXPECT_EQ(VoiceManager::find(mx, id1), nullptr);
        EXPECT_NE(VoiceManager::find(mx, id2), nullptr);
    }
}

TEST(VoiceManager, ExhaustPoolStealsOldestUnprotected) {
    MixerData mx;
    setupMixer(mx, 2);

    uint64_t id1, id2;
    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v1 = VoiceManager::allocate(mx); ASSERT_NE(v1, nullptr); id1 = v1->id;
        Voice* v2 = VoiceManager::allocate(mx); ASSERT_NE(v2, nullptr); id2 = v2->id;
        // Both slots filled; id1 is oldest (lower lruAge). The next alloc steals it.
        Voice* v3 = VoiceManager::allocate(mx);
        ASSERT_NE(v3, nullptr);
        EXPECT_NE(v3->id, id1);
        EXPECT_NE(v3->id, id2);
        EXPECT_EQ(VoiceManager::find(mx, id1), nullptr);  // id1 was stolen
        EXPECT_NE(VoiceManager::find(mx, id2), nullptr);  // id2 still alive
    }
}

TEST(VoiceManager, ProtectedVoiceNotStolen) {
    MixerData mx;
    setupMixer(mx, 2);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v1 = VoiceManager::allocate(mx); ASSERT_NE(v1, nullptr); v1->protect = true;
        Voice* v2 = VoiceManager::allocate(mx); ASSERT_NE(v2, nullptr); v2->protect = true;
        EXPECT_EQ(VoiceManager::allocate(mx), nullptr);  // all protected — cannot steal
    }
}

TEST(VoiceManager, FindInactiveReturnsNull) {
    MixerData mx;
    setupMixer(mx, 4);
    std::lock_guard lk(mx.voiceMutex);
    EXPECT_EQ(VoiceManager::find(mx, 9999u), nullptr);
}

// ---------------------------------------------------------------------------
// Mixer — silence / global volume
// ---------------------------------------------------------------------------

TEST(Mixer, EmptyPoolProducesSilence) {
    MixerData mx;
    setupMixer(mx, 4);
    constexpr uint32_t frames = 512;
    std::vector<float> out(frames * 2, 0.5f);
    mx.mixSamples(out.data(), frames);
    EXPECT_TRUE(isAllZero(out.data(), static_cast<uint32_t>(out.size())));
}

TEST(Mixer, GlobalVolumeZeroMutesSingleVoice) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 4096);
    mx.globalVolume   = 0.0f;

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v     = VoiceManager::allocate(mx);
        v->buffer    = &buf;
        v->loopMode  = LoopMode::Loop;
    }

    constexpr uint32_t frames = 512;
    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);
    EXPECT_TRUE(isAllZero(out.data(), static_cast<uint32_t>(out.size())));
}

// ---------------------------------------------------------------------------
// Mixing output — single voice, known signal
// ---------------------------------------------------------------------------

TEST(Mixer, SingleVoiceMonoCentreOutput) {
    // Mono constant buffer at 0.5; voice panned centre (pan = 0).
    // Constant-power pan at centre: theta = π/4, gainL = gainR = cos(π/4).
    // Expected per channel: 0.5 × cos(π/4) ≈ 0.35355.
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(0.5f, 4096, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->volume   = 1.0f;
        v->pan      = 0.0f;
        v->pitch    = 1.0f;
    }

    constexpr uint32_t frames = 256;
    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    const float expected = 0.5f * std::cos(static_cast<float>(std::numbers::pi / 4.0));
    for (uint32_t f = 0; f < frames; ++f) {
        EXPECT_NEAR(out[f * 2 + 0], expected, 1e-5f) << "frame " << f << " left";
        EXPECT_NEAR(out[f * 2 + 1], expected, 1e-5f) << "frame " << f << " right";
    }
}

TEST(Mixer, VolumeScaling) {
    // Two separate mixers: vol=1.0 should produce double the amplitude of vol=0.5.
    DecodedBuffer buf = makeConstantBuffer(0.8f, 4096, 1, 44100);

    auto addVoice = [&](MixerData& mx, float vol) {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->volume   = vol;
        v->pan      = 0.0f;
    };

    MixerData mx1, mx2;
    setupMixer(mx1, 4);
    setupMixer(mx2, 4);
    addVoice(mx1, 1.0f);
    addVoice(mx2, 0.5f);

    constexpr uint32_t frames = 256;
    std::vector<float> out1(frames * 2), out2(frames * 2);
    mx1.mixSamples(out1.data(), frames);
    mx2.mixSamples(out2.data(), frames);

    for (uint32_t i = 0; i < frames * 2; ++i) {
        EXPECT_NEAR(out1[i], out2[i] * 2.0f, 1e-5f) << "sample " << i;
    }
}

TEST(Mixer, PanFullLeftSilencesRightChannel) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 4096, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->pan      = -1.0f;  // theta = 0 → gainR = sin(0) = 0
    }

    constexpr uint32_t frames = 256;
    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    for (uint32_t f = 0; f < frames; ++f) {
        EXPECT_GT(out[f * 2 + 0], 0.0f)       << "left should be audible at frame "  << f;
        EXPECT_NEAR(out[f * 2 + 1], 0.0f, 1e-6f) << "right should be silent at frame " << f;
    }
}

// ---------------------------------------------------------------------------
// Pause / resume
// ---------------------------------------------------------------------------

TEST(Mixer, PausedVoiceProducesSilence) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 4096, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->paused   = true;
    }

    constexpr uint32_t frames = 256;
    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);
    EXPECT_TRUE(isAllZero(out.data(), static_cast<uint32_t>(out.size())));
}

// ---------------------------------------------------------------------------
// Loop modes
// ---------------------------------------------------------------------------

TEST(Mixer, LoopNoneDeactivatesVoiceAfterBuffer) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 64, 1, 44100);

    uint64_t voiceId;
    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::None;
        voiceId     = v->id;
    }

    std::vector<float> out(256 * 2);
    mx.mixSamples(out.data(), 256);

    EXPECT_EQ(mx.activeVoiceCount.load(), 0u);
    {
        std::lock_guard lk(mx.voiceMutex);
        EXPECT_EQ(VoiceManager::find(mx, voiceId), nullptr);
    }
    // First frames carry signal; later frames (after the 64-frame source) are silent.
    EXPECT_GT(std::abs(out[0]), 0.0f);
    EXPECT_FLOAT_EQ(out[255 * 2], 0.0f);
}

TEST(Mixer, LoopLoopKeepsVoiceActiveAcrossBufferEnd) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 64, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
    }

    std::vector<float> out(512 * 2);
    mx.mixSamples(out.data(), 512);

    EXPECT_EQ(mx.activeVoiceCount.load(), 1u);
    EXPECT_FALSE(isAllZero(out.data(), static_cast<uint32_t>(out.size())));
}

TEST(Mixer, PingPongProducesNonZeroSignal) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 32, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::PingPong;
    }

    std::vector<float> out(512 * 2);
    mx.mixSamples(out.data(), 512);

    EXPECT_EQ(mx.activeVoiceCount.load(), 1u);
    EXPECT_FALSE(isAllZero(out.data(), static_cast<uint32_t>(out.size())));
}

// ---------------------------------------------------------------------------
// Fade automation
// ---------------------------------------------------------------------------

TEST(Mixer, FadeVolumeFromOneToZeroDecreases) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 1 << 20, 1, 44100);

    constexpr uint32_t frames = 4096;
    const double fadeSecs = static_cast<double>(frames) / 44100.0;

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v         = VoiceManager::allocate(mx);
        v->buffer        = &buf;
        v->loopMode      = LoopMode::Loop;
        v->volume        = 1.0f;
        v->fadeVolFrom   = 1.0f;
        v->fadeVolTo     = 0.0f;
        v->fadeVolTime   = fadeSecs;
        v->fadeVolActive = true;
    }

    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    const float rmsFirst  = rms(out.data(),                    frames / 2 * 2);
    const float rmsSecond = rms(out.data() + frames / 2 * 2,   frames / 2 * 2);
    EXPECT_GT(rmsFirst, rmsSecond);
    EXPECT_NEAR(out[(frames - 1) * 2], 0.0f, 0.01f);
}

TEST(Mixer, FadeVolStopOnDoneDeactivatesVoice) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 1 << 20, 1, 44100);

    constexpr uint32_t frames = 512;
    const double fadeSecs = static_cast<double>(frames / 2) / 44100.0;

    uint64_t voiceId;
    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v             = VoiceManager::allocate(mx);
        v->buffer            = &buf;
        v->loopMode          = LoopMode::Loop;
        v->fadeVolFrom       = 1.0f;
        v->fadeVolTo         = 0.0f;
        v->fadeVolTime       = fadeSecs;
        v->fadeVolActive     = true;
        v->fadeVolStopOnDone = true;
        voiceId              = v->id;
    }

    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    EXPECT_EQ(mx.activeVoiceCount.load(), 0u);
    {
        std::lock_guard lk(mx.voiceMutex);
        EXPECT_EQ(VoiceManager::find(mx, voiceId), nullptr);
    }
}

// ---------------------------------------------------------------------------
// LFO automation
// ---------------------------------------------------------------------------

TEST(Mixer, LFOVolumeCreatesAmplitudeVariation) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 1 << 20, 1, 44100);

    constexpr uint32_t frames = 2048;
    const double periodSecs = 100.0 / 44100.0;  // ~100 samples per cycle

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v        = VoiceManager::allocate(mx);
        v->buffer       = &buf;
        v->loopMode     = LoopMode::Loop;
        v->volume       = 0.0f;   // base volume replaced entirely by LFO
        v->lfoVolFrom   = 0.0f;
        v->lfoVolTo     = 1.0f;
        v->lfoVolPeriod = periodSecs;
        v->lfoVolActive = true;
    }

    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    float minVal = out[0], maxVal = out[0];
    for (uint32_t i = 0; i < frames * 2; ++i) {
        minVal = std::min(minVal, out[i]);
        maxVal = std::max(maxVal, out[i]);
    }
    EXPECT_GT(maxVal - minVal, 0.1f);
}

// ---------------------------------------------------------------------------
// Global volume fade
// ---------------------------------------------------------------------------

TEST(Mixer, GlobalVolumeFadeReducesOutput) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 1 << 20, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
    }

    constexpr uint32_t frames = 512;
    std::vector<float> outFull(frames * 2);
    mx.mixSamples(outFull.data(), frames);   // first pass: full volume

    // Trigger a global fade to zero over the next buffer.
    mx.gFadeFrom    = 1.0f;
    mx.gFadeTo      = 0.0f;
    mx.gFadeTime    = static_cast<double>(frames) / 44100.0;
    mx.gFadeElapsed = 0.0;
    mx.gFadeActive  = true;

    std::vector<float> outFaded(frames * 2);
    mx.mixSamples(outFaded.data(), frames);

    EXPECT_NEAR(mx.globalVolume, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(outFaded.back(), 0.0f);
}

// ---------------------------------------------------------------------------
// Stereo source
// ---------------------------------------------------------------------------

TEST(Mixer, StereoSourceBothChannelsMixed) {
    // L=0.8, R=0.2 interleaved stereo buffer.
    // With centre pan, L output ≈ 0.8 × gain, R output ≈ 0.2 × gain.
    MixerData mx;
    setupMixer(mx, 4);

    DecodedBuffer buf;
    buf.channels   = 2;
    buf.sampleRate = 44100;
    buf.frameCount = 1024;
    buf.samples.resize(1024 * 2);
    for (uint64_t f = 0; f < 1024; ++f) {
        buf.samples[f * 2 + 0] = 0.8f;
        buf.samples[f * 2 + 1] = 0.2f;
    }

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->pan      = 0.0f;
        v->volume   = 1.0f;
    }

    constexpr uint32_t frames = 256;
    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    const float gain = std::cos(static_cast<float>(std::numbers::pi / 4.0));
    for (uint32_t f = 0; f < frames; ++f) {
        EXPECT_NEAR(out[f * 2 + 0], 0.8f * gain, 1e-5f) << "frame " << f;
        EXPECT_NEAR(out[f * 2 + 1], 0.2f * gain, 1e-5f) << "frame " << f;
    }
}

// ---------------------------------------------------------------------------
// Pitch shift via linear resampling
// ---------------------------------------------------------------------------

TEST(Mixer, PitchTwoDoublesSampleConsumption) {
    // pitch=2.0 advances through the source at twice the normal rate;
    // a 64-frame source at 44100 Hz should be exhausted in ~32 output frames.
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 64, 1, 44100);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::None;
        v->pitch    = 2.0f;
    }

    std::vector<float> out(48 * 2, 0.0f);
    mx.mixSamples(out.data(), 48);

    EXPECT_EQ(mx.activeVoiceCount.load(), 0u);
    EXPECT_FLOAT_EQ(out[47 * 2], 0.0f);  // silence after source exhausted
}

// ---------------------------------------------------------------------------
// Visualization buffer
// ---------------------------------------------------------------------------

TEST(Mixer, VisualizationCapturesOutput) {
    MixerData mx;
    setupMixer(mx, 4);
    DecodedBuffer buf = makeConstantBuffer(1.0f, 4096, 1, 44100);

    constexpr uint32_t frames = 256;
    mx.vizEnabled = true;
    mx.vizBuffer.assign(frames * 2, 0.0f);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
    }

    std::vector<float> out(frames * 2);
    mx.mixSamples(out.data(), frames);

    for (uint32_t i = 0; i < frames * 2; ++i) {
        EXPECT_FLOAT_EQ(mx.vizBuffer[i], out[i]) << "viz sample " << i;
    }
}

// ---------------------------------------------------------------------------
// Phase 16 — Surround / VBAP panning
//
// Speaker channel order (standard WAV/ITU):
//   5.1 (outCh=6): [FL=0, FR=1, FC=2, LFE=3, BL=4, BR=5]
//   7.1 (outCh=8): [FL=0, FR=1, FC=2, LFE=3, BL=4, BR=5, SL=6, SR=7]
//
// Speaker azimuths (degrees, 0=front, positive=right):
//   5.1: BL=-110, FL=-30, FC=0, FR=30, BR=110
//   7.1: BL=-150, SL=-90, FL=-30, FC=0, FR=30, SR=90, BR=150
// ---------------------------------------------------------------------------

namespace {

/// Run mixSamples() with the given output channel count and return a single
/// output buffer frame. The voice uses a constant 0.5 mono buffer, pitch=1,
/// volume=1, LoopMode::Loop.
std::vector<float> surroundFrame(uint32_t outCh, float pan) {
    MixerData mx;
    mx.config.sampleRate = 44100;
    mx.config.channels   = outCh;
    mx.config.maxVoices  = 4;
    VoiceManager::init(mx);

    DecodedBuffer buf = makeConstantBuffer(0.5f, 4096, 1, 44100);
    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->pan      = pan;
        v->volume   = 1.0f;
    }

    constexpr uint32_t frames = 64;
    std::vector<float> out(frames * outCh, 0.0f);
    mx.mixSamples(out.data(), frames);
    // All frames are identical; return the first.
    return std::vector<float>(out.begin(), out.begin() + outCh);
}

} // anonymous namespace

TEST(Surround51, PanCenter_GoesToFC) {
    // pan=0 → azimuth=0° → exactly at FC (ch2). All other channels silent.
    const auto g = surroundFrame(6, 0.0f);
    // mono fold = (0.5+0.5)/2 = 0.5; FC gain = 1.0
    EXPECT_NEAR(g[0], 0.0f, 1e-5f);  // FL
    EXPECT_NEAR(g[1], 0.0f, 1e-5f);  // FR
    EXPECT_NEAR(g[2], 0.5f, 1e-5f);  // FC ← all signal
    EXPECT_NEAR(g[3], 0.0f, 1e-5f);  // LFE always silent
    EXPECT_NEAR(g[4], 0.0f, 1e-5f);  // BL
    EXPECT_NEAR(g[5], 0.0f, 1e-5f);  // BR
}

TEST(Surround51, PanLeft_SplitsBL_FL) {
    // pan=-1 → azimuth=-90°, bracketed by BL(-110°) and FL(-30°).
    // alpha = 20/80 = 0.25 → gain BL=cos(π/8), FL=sin(π/8).
    const auto g = surroundFrame(6, -1.0f);
    const float cosE = std::cos(static_cast<float>(std::numbers::pi) / 8.0f);  // ≈0.924
    const float sinE = std::sin(static_cast<float>(std::numbers::pi) / 8.0f);  // ≈0.383
    // mono fold = 0.5; gains × 0.5
    EXPECT_NEAR(g[0], 0.5f * sinE, 1e-4f);  // FL
    EXPECT_NEAR(g[1], 0.0f,        1e-5f);  // FR
    EXPECT_NEAR(g[2], 0.0f,        1e-5f);  // FC
    EXPECT_NEAR(g[3], 0.0f,        1e-5f);  // LFE
    EXPECT_NEAR(g[4], 0.5f * cosE, 1e-4f);  // BL ← dominant
    EXPECT_NEAR(g[5], 0.0f,        1e-5f);  // BR
}

TEST(Surround51, PanRight_SplitsFR_BR) {
    // Symmetric to pan=-1.
    const auto g = surroundFrame(6, 1.0f);
    const float cosE = std::cos(static_cast<float>(std::numbers::pi) / 8.0f);
    const float sinE = std::sin(static_cast<float>(std::numbers::pi) / 8.0f);
    EXPECT_NEAR(g[0], 0.0f,        1e-5f);  // FL
    EXPECT_NEAR(g[1], 0.5f * sinE, 1e-4f);  // FR
    EXPECT_NEAR(g[2], 0.0f,        1e-5f);  // FC
    EXPECT_NEAR(g[3], 0.0f,        1e-5f);  // LFE
    EXPECT_NEAR(g[4], 0.0f,        1e-5f);  // BL
    EXPECT_NEAR(g[5], 0.5f * cosE, 1e-4f);  // BR ← dominant
}

TEST(Surround51, LFE_AlwaysZero) {
    // LFE (channel 3) must always be 0 regardless of pan.
    for (float p : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        const auto g = surroundFrame(6, p);
        EXPECT_FLOAT_EQ(g[3], 0.0f) << "LFE non-zero at pan=" << p;
    }
}

TEST(Surround71, PanCenter_GoesToFC) {
    // Same as 5.1: pan=0 → FC (ch2) only.
    const auto g = surroundFrame(8, 0.0f);
    EXPECT_NEAR(g[2], 0.5f, 1e-5f);  // FC
    EXPECT_NEAR(g[0], 0.0f, 1e-5f);  // FL
    EXPECT_NEAR(g[1], 0.0f, 1e-5f);  // FR
    EXPECT_NEAR(g[3], 0.0f, 1e-5f);  // LFE
    EXPECT_NEAR(g[4], 0.0f, 1e-5f);  // BL
    EXPECT_NEAR(g[5], 0.0f, 1e-5f);  // BR
    EXPECT_NEAR(g[6], 0.0f, 1e-5f);  // SL
    EXPECT_NEAR(g[7], 0.0f, 1e-5f);  // SR
}

TEST(Surround71, PanRight_GoesToSR) {
    // pan=+1 → azimuth=+90° → exactly at SR (ch7). gain SR=1.0.
    const auto g = surroundFrame(8, 1.0f);
    EXPECT_NEAR(g[7], 0.5f, 1e-5f);  // SR ← all signal (mono 0.5 × gain 1.0)
    EXPECT_NEAR(g[1], 0.0f, 1e-5f);  // FR
    EXPECT_NEAR(g[3], 0.0f, 1e-5f);  // LFE
}

TEST(Surround71, PanLeft_GoesToSL) {
    // pan=-1 → azimuth=-90° → exactly at SL (ch6). gain SL=1.0.
    const auto g = surroundFrame(8, -1.0f);
    EXPECT_NEAR(g[6], 0.5f, 1e-5f);  // SL ← all signal
    EXPECT_NEAR(g[0], 0.0f, 1e-5f);  // FL
    EXPECT_NEAR(g[3], 0.0f, 1e-5f);  // LFE
}

TEST(Surround71, LFE_AlwaysZero) {
    for (float p : {-1.0f, 0.0f, 1.0f}) {
        const auto g = surroundFrame(8, p);
        EXPECT_FLOAT_EQ(g[3], 0.0f) << "LFE non-zero at pan=" << p;
    }
}

// ---------------------------------------------------------------------------
// Phase 15 — Resample quality
//
// Test buffer: [0, 0.25, 0.75, 1.0] — non-linear ramp so Linear and
// CatmullRom produce different values at the same fractional position.
//
// pitch=0.5 → advance=0.5 per output frame (source and output at 44100 Hz):
//   frame 0: readPos=0.0 → all modes return samples[0]=0
//   frame 1: readPos=0.5 → frame0=0, frac=0.5
//     Point:      samples[0] = 0.0        (ignores frac)
//     Linear:     lerp(0, 0.25, 0.5)      = 0.125
//     CatmullRom: see analytical result    = 0.09375
// ---------------------------------------------------------------------------

namespace {

DecodedBuffer makeRampBuffer() {
    DecodedBuffer buf;
    buf.channels   = 1;
    buf.sampleRate = 44100;
    buf.frameCount = 4;
    buf.samples    = {0.0f, 0.25f, 0.75f, 1.0f};
    return buf;
}

// Returns left-channel output of frame `frameIdx` after calling mixSamples
// for `totalFrames` frames.
float mixAndSample(ResampleQuality quality, const DecodedBuffer& buf,
                   float pitch, uint32_t totalFrames, uint32_t frameIdx) {
    MixerData mx;
    mx.config.sampleRate      = 44100;
    mx.config.channels        = 2;
    mx.config.maxVoices       = 4;
    mx.config.resampleQuality = quality;
    VoiceManager::init(mx);

    {
        std::lock_guard lk(mx.voiceMutex);
        Voice* v    = VoiceManager::allocate(mx);
        v->buffer   = &buf;
        v->loopMode = LoopMode::Loop;
        v->pitch    = pitch;
        v->pan      = 0.0f;
        v->volume   = 1.0f;
    }

    std::vector<float> out(totalFrames * 2, 0.0f);
    mx.mixSamples(out.data(), totalFrames);
    return out[frameIdx * 2];  // left channel
}

} // anonymous namespace

TEST(ResampleQuality, Point_IgnoresFraction) {
    // At readPos=0.5 (frac=0.5), Point returns samples[0]=0 (no interpolation).
    const DecodedBuffer buf = makeRampBuffer();
    const float gain = std::cos(static_cast<float>(std::numbers::pi / 4.0));
    EXPECT_NEAR(mixAndSample(ResampleQuality::Point, buf, 0.5f, 2, 1),
                0.0f * gain, 1e-5f);
}

TEST(ResampleQuality, Linear_InterpolatesAdjacentSamples) {
    // At readPos=0.5 (frac=0.5): lerp(samples[0], samples[1], 0.5) = 0.125.
    const DecodedBuffer buf = makeRampBuffer();
    const float gain = std::cos(static_cast<float>(std::numbers::pi / 4.0));
    EXPECT_NEAR(mixAndSample(ResampleQuality::Linear, buf, 0.5f, 2, 1),
                0.125f * gain, 1e-5f);
}

TEST(ResampleQuality, CatmullRom_DiffersFromLinear) {
    // Analytical CatmullRom at frame0=0, frac=0.5, samples=[0,0.25,0.75,1]:
    //   p0=clamp(-1)=0, p1=0, p2=0.25, p3=0.75, t=0.5
    //   = 0.5*(0 + 0.125 + (1.0-0.75)*0.25 + 0) = 0.09375
    const DecodedBuffer buf = makeRampBuffer();
    const float gain = std::cos(static_cast<float>(std::numbers::pi / 4.0));
    EXPECT_NEAR(mixAndSample(ResampleQuality::CatmullRom, buf, 0.5f, 2, 1),
                0.09375f * gain, 1e-4f);
}

TEST(ResampleQuality, AllModes_AgreeAtIntegerPositions) {
    // pitch=1.0 → advance=1.0, readPos always integer — all modes identical.
    DecodedBuffer buf;
    buf.channels   = 1;
    buf.sampleRate = 44100;
    buf.frameCount = 256;
    buf.samples.resize(256);
    for (uint32_t i = 0; i < 256; ++i)
        buf.samples[i] = static_cast<float>(i) / 255.0f;

    auto run = [&](ResampleQuality q) {
        MixerData mx;
        mx.config.sampleRate      = 44100;
        mx.config.channels        = 2;
        mx.config.maxVoices       = 4;
        mx.config.resampleQuality = q;
        VoiceManager::init(mx);
        {
            std::lock_guard lk(mx.voiceMutex);
            Voice* v    = VoiceManager::allocate(mx);
            v->buffer   = &buf;
            v->loopMode = LoopMode::None;
            v->pitch    = 1.0f;
            v->pan      = 0.0f;
            v->volume   = 1.0f;
        }
        std::vector<float> out(128 * 2, 0.0f);
        mx.mixSamples(out.data(), 128);
        return out;
    };

    const auto outP  = run(ResampleQuality::Point);
    const auto outL  = run(ResampleQuality::Linear);
    const auto outCR = run(ResampleQuality::CatmullRom);

    for (uint32_t i = 0; i < 128 * 2; ++i) {
        EXPECT_NEAR(outP[i],  outL[i],  1e-5f) << "Point vs Linear at sample "      << i;
        EXPECT_NEAR(outL[i],  outCR[i], 1e-5f) << "Linear vs CatmullRom at sample " << i;
    }
}
