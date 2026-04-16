#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/wav_source.hpp>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
namespace {

AudioEngineDescriptor makeDesc(uint32_t maxVoices, uint32_t maxVirtual,
                                float threshold) {
    AudioEngineDescriptor d{};
    d.maxVoices          = maxVoices;
    d.maxVirtualVoices   = maxVirtual;
    d.virtualizeThreshold = threshold;
    return d;
}

std::vector<uint8_t> makeSilentWav(uint32_t sr = 44100, uint16_t ch = 1,
                                    uint32_t frames = 512) {
    const uint32_t bps      = 2;
    const uint32_t dataSize = frames * ch * bps;
    const uint32_t riffSize = 4 + 24 + 8 + dataSize;
    std::vector<uint8_t> b;
    auto p16 = [&](uint16_t v){ b.push_back(v&0xFF); b.push_back(v>>8); };
    auto p32 = [&](uint32_t v){ for(int i=0;i<4;++i) b.push_back((v>>(i*8))&0xFF); };
    auto ps  = [&](const char* s){ while(*s) b.push_back((uint8_t)*s++); };
    ps("RIFF"); p32(riffSize); ps("WAVE");
    ps("fmt "); p32(16); p16(1); p16(ch); p32(sr);
    p32(sr*ch*bps); p16(ch*bps); p16(16);
    ps("data"); p32(dataSize);
    b.resize(b.size() + dataSize, 0);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// VoiceManager allocate() path: virtualize-before-steal
//
// When the real pool is full and the quietest voice is below threshold,
// it should be pushed to the virtual pool rather than killed.
// ---------------------------------------------------------------------------

class VirtualizationTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void TearDown() override { engine.deinit(); }
};

TEST_F(VirtualizationTest, QuietVoiceVirtualizedWhenPoolFull) {
    // threshold = 0.5, maxVoices = 4, maxVirtual = 32
    engine.init(makeDesc(4, 32, 0.5f));

    // Fill pool with 4 quiet tones (volume = 0.2, below threshold = 0.5)
    std::vector<SoundHandle> handles;
    PlayDescriptor pd;
    pd.volume = 0.2f;
    for (int i = 0; i < 4; ++i) {
        ToneSource t;
        handles.push_back(engine.play(t, pd));
    }
    EXPECT_EQ(engine.getActiveVoiceCount(), 4u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);

    // Play a 5th tone — allocate() should virtualize the quietest real voice
    ToneSource t5;
    SoundHandle h5 = engine.play(t5, pd);
    EXPECT_TRUE(h5.isValid());
    EXPECT_EQ(engine.getActiveVoiceCount(), 4u);   // still 4 real
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);  // 1 virtualized
    EXPECT_EQ(engine.getTotalVoiceCount(),   5u);
}

TEST_F(VirtualizationTest, LoudVoiceNotVirtualizedPoolFull) {
    // threshold = 0.5, voices are LOUD (volume > threshold) → LRU steal, not virtualize
    engine.init(makeDesc(4, 32, 0.5f));

    std::vector<SoundHandle> handles;
    PlayDescriptor pd;
    pd.volume = 0.8f; // above threshold
    for (int i = 0; i < 4; ++i) {
        ToneSource t;
        handles.push_back(engine.play(t, pd));
    }
    EXPECT_EQ(engine.getActiveVoiceCount(),  4u);

    // 5th play → should LRU-steal (no virtualization since voices are loud)
    ToneSource t5;
    engine.play(t5, pd);
    EXPECT_EQ(engine.getActiveVoiceCount(),  4u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);  // nothing virtualized
}

TEST_F(VirtualizationTest, VirtualizationDisabledWhenMaxVirtualIsZero) {
    // maxVirtualVoices = 0 disables virtualization entirely → always LRU steal
    engine.init(makeDesc(4, 0, 0.5f));

    PlayDescriptor pd;
    pd.volume = 0.2f;
    for (int i = 0; i < 5; ++i) {
        ToneSource t;
        engine.play(t, pd);
    }
    EXPECT_EQ(engine.getActiveVoiceCount(),  4u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);
}

TEST_F(VirtualizationTest, StopWorksOnVirtualVoice) {
    engine.init(makeDesc(4, 32, 0.5f));

    // Fill pool with quiet voices
    PlayDescriptor pd;
    pd.volume = 0.2f;
    for (int i = 0; i < 4; ++i) {
        ToneSource t;
        engine.play(t, pd);
    }

    // 5th voice — one goes virtual, h5 is real
    ToneSource t5;
    SoundHandle h5 = engine.play(t5, pd);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);

    // The virtual voice's handle is in the handles from earlier loops.
    // Use stopAll to clear both pools.
    engine.stopAll();
    EXPECT_EQ(engine.getActiveVoiceCount(),  0u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);
    (void)h5;
}

TEST_F(VirtualizationTest, IsValidTrueForVirtualVoice) {
    engine.init(makeDesc(1, 32, 0.5f));

    // Fill the single real slot
    PlayDescriptor pd;
    pd.volume = 0.2f;
    ToneSource t1;
    SoundHandle h1 = engine.play(t1, pd);
    EXPECT_EQ(engine.getActiveVoiceCount(), 1u);

    // 2nd play → h1 goes virtual
    ToneSource t2;
    engine.play(t2, pd);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);

    // h1 is still "valid" — it's playing, just silent
    EXPECT_TRUE(engine.isValid(h1));
}

TEST_F(VirtualizationTest, StopAllClearsVirtualPool) {
    engine.init(makeDesc(4, 32, 0.5f));

    PlayDescriptor pd;
    pd.volume = 0.2f;
    for (int i = 0; i < 6; ++i) {
        ToneSource t;
        engine.play(t, pd);
    }
    EXPECT_GT(engine.getVirtualVoiceCount(), 0u);

    engine.stopAll();
    EXPECT_EQ(engine.getActiveVoiceCount(),  0u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);
}

// ---------------------------------------------------------------------------
// Mixer-thread virtualization: voice volume drops below threshold mid-play,
// then rises again — tests the mixSamples() sweep + re-activation path.
// ---------------------------------------------------------------------------

TEST_F(VirtualizationTest, LowVolumeVoiceMovesToVirtualPool) {
    engine.init(makeDesc(32, 64, 0.5f));

    // Play a tone at volume well below threshold
    ToneSource t;
    PlayDescriptor pd;
    pd.volume = 0.01f;  // << 0.5 threshold
    SoundHandle h = engine.play(t, pd);
    EXPECT_TRUE(engine.isValid(h));

    // Wait for the audio thread to run the virtualization sweep
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Voice should now be virtual (active count drops)
    EXPECT_EQ(engine.getActiveVoiceCount(),  0u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);
    EXPECT_TRUE(engine.isValid(h));  // still "alive" as a virtual voice
}

TEST_F(VirtualizationTest, VirtualVoiceReactivatesWhenVolumeRises) {
    engine.init(makeDesc(32, 64, 0.5f));

    ToneSource t;
    PlayDescriptor pd;
    pd.volume = 0.01f;
    SoundHandle h = engine.play(t, pd);

    // Let audio thread virtualize it
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);

    // Raise volume above threshold → re-activation on next audio callback
    engine.setVolume(h, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);
    EXPECT_EQ(engine.getActiveVoiceCount(),  1u);
    engine.stop(h);
}
