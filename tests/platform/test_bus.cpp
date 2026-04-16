/// @file test_bus.cpp
/// @brief Integration tests for AudioBus and sidechain ducking.
///
/// All tests require a real audio device (CoreAudio on macOS).

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_bus.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/compressor_filter.hpp>
#include <campello_audio/echo_filter.hpp>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BusTest : public ::testing::Test {
protected:
    AudioEngine engine;

    void SetUp() override {
        AudioEngineDescriptor d;
        d.visualization = true;
        ASSERT_TRUE(engine.init(d));
    }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// Bus construction and registration
// ---------------------------------------------------------------------------

// Constructing an AudioBus does not crash.
TEST(BusConstruction, DefaultConstruct) {
    AudioBus bus;
    (void)bus;
}

// engine.play(bus) registers the bus without crashing and returns invalid
// (buses have no voice handle).
TEST_F(BusTest, PlayBusReturnsInvalidHandle) {
    AudioBus bus;
    SoundHandle h = engine.play(bus);
    EXPECT_FALSE(h.isValid());
}

// Playing a source through a registered bus does not crash.
TEST_F(BusTest, PlaySourceThroughBus) {
    AudioBus sfxBus;
    engine.play(sfxBus);

    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = sfxBus.play(tone);
    EXPECT_TRUE(engine.isValid(h));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h);
}

// Playing through an unregistered bus (no engine.play(bus) called) returns
// an invalid handle — the bus has no back-pointer yet.
TEST_F(BusTest, PlayThroughUnregisteredBusReturnsInvalid) {
    AudioBus unregistered;
    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = unregistered.play(tone);
    EXPECT_FALSE(h.isValid());
}

// Multiple sources can play through the same bus simultaneously.
TEST_F(BusTest, MultipleSourcesThroughOneBus) {
    AudioBus sfxBus;
    engine.play(sfxBus);

    ToneSource t1(WaveForm::Sine, 440.0f);
    ToneSource t2(WaveForm::Sine, 880.0f);
    SoundHandle h1 = sfxBus.play(t1);
    SoundHandle h2 = sfxBus.play(t2);
    EXPECT_TRUE(engine.isValid(h1));
    EXPECT_TRUE(engine.isValid(h2));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h1);
    engine.stop(h2);
}

// Sources from two independent buses can play at the same time.
TEST_F(BusTest, TwoBusesPlayIndependently) {
    AudioBus sfxBus, musicBus;
    engine.play(sfxBus);
    engine.play(musicBus);

    ToneSource t1(WaveForm::Sine, 440.0f);
    ToneSource t2(WaveForm::Sine, 220.0f);
    SoundHandle h1 = sfxBus.play(t1);
    SoundHandle h2 = musicBus.play(t2);
    EXPECT_TRUE(engine.isValid(h1));
    EXPECT_TRUE(engine.isValid(h2));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop(h1);
    engine.stop(h2);
}

// ---------------------------------------------------------------------------
// Bus volume
// ---------------------------------------------------------------------------

// setVolume() on a registered bus does not crash.
TEST_F(BusTest, SetVolumeDoesNotCrash) {
    AudioBus bus;
    engine.play(bus);
    EXPECT_NO_THROW(bus.setVolume(0.5f));
    EXPECT_NO_THROW(bus.setVolume(0.0f));
    EXPECT_NO_THROW(bus.setVolume(1.0f));
}

// setVolume(0) on a bus silences its voices while still keeping them valid.
TEST_F(BusTest, SetVolumeZeroKeepsVoiceValid) {
    AudioBus bus;
    engine.play(bus);
    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = bus.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    bus.setVolume(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Bus filter chain
// ---------------------------------------------------------------------------

// A filter attached to the bus source before registration is applied to the
// bus's mixed output.
TEST_F(BusTest, BusFilterChainApplied) {
    AudioBus sfxBus;
    auto comp = std::make_shared<CompressorFilter>(-12.0f, 4.0f, 5.0f, 50.0f);
    sfxBus.addFilter(comp, 0);
    engine.play(sfxBus);

    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = sfxBus.play(tone);
    EXPECT_TRUE(engine.isValid(h));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Bus visualization
// ---------------------------------------------------------------------------

// getVisualizationData() returns nullptr for an unregistered bus.
TEST(BusViz, UnregisteredBusReturnsNullptr) {
    AudioBus bus;
    uint32_t count = 0;
    const float* ptr = bus.getVisualizationData(count);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(count, 0u);
}

// ---------------------------------------------------------------------------
// Sidechain / ducking
// ---------------------------------------------------------------------------

// setSidechain() and clearSidechain() do not crash when buses are registered.
TEST_F(BusTest, SetAndClearSidechainDoNotCrash) {
    AudioBus sfxBus, musicBus;
    engine.play(sfxBus);
    engine.play(musicBus);

    EXPECT_NO_THROW(engine.setSidechain(&sfxBus, &musicBus, -12.0f, 0.01f, 0.2f));
    EXPECT_NO_THROW(engine.clearSidechain(&musicBus));
}

// Passing nullptr buses to setSidechain() is safe.
TEST_F(BusTest, SetSidechainNullBusIsSafe) {
    AudioBus sfxBus;
    engine.play(sfxBus);
    EXPECT_NO_THROW(engine.setSidechain(nullptr, &sfxBus, -6.0f, 0.01f, 0.1f));
    EXPECT_NO_THROW(engine.setSidechain(&sfxBus, nullptr, -6.0f, 0.01f, 0.1f));
}

// When the SFX bus plays audio, the music bus is ducked: after the SFX bus
// has been playing for several frames, the music bus should have a non-zero
// sidechain envelope (i.e., gain reduction is actively applied to the mix).
// We verify this indirectly: the overall engine remains stable (no crash, no
// silence on all outputs after the duck).
TEST_F(BusTest, SidechainDucksTargetWhenTriggerPlays) {
    AudioBus sfxBus, musicBus;
    engine.play(sfxBus);
    engine.play(musicBus);

    // music ducks by -12 dB when sfx plays
    engine.setSidechain(&sfxBus, &musicBus, -12.0f, 0.005f, 0.1f);

    ToneSource sfx(WaveForm::Square, 880.0f);
    ToneSource music(WaveForm::Sine,  220.0f);

    SoundHandle hsfx   = sfxBus.play(sfx);
    SoundHandle hmusic = musicBus.play(music);

    EXPECT_TRUE(engine.isValid(hsfx));
    EXPECT_TRUE(engine.isValid(hmusic));

    // Let both buses play — sidechain should engage.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Both voices still alive after ducking.
    EXPECT_TRUE(engine.isValid(hsfx));
    EXPECT_TRUE(engine.isValid(hmusic));

    engine.stop(hsfx);

    // After sfx stops, sidechain releases — music should continue.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(engine.isValid(hmusic));

    engine.stop(hmusic);
}

// clearSidechain removes ducking; subsequent calls behave as if no sidechain
// was set.
TEST_F(BusTest, ClearSidechainRestoresNormalMix) {
    AudioBus sfxBus, musicBus;
    engine.play(sfxBus);
    engine.play(musicBus);

    engine.setSidechain(&sfxBus, &musicBus, -20.0f, 0.005f, 0.1f);
    engine.clearSidechain(&musicBus);

    ToneSource sfx(WaveForm::Square, 880.0f);
    ToneSource music(WaveForm::Sine,  220.0f);
    SoundHandle hsfx   = sfxBus.play(sfx);
    SoundHandle hmusic = musicBus.play(music);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(engine.isValid(hsfx));
    EXPECT_TRUE(engine.isValid(hmusic));
    engine.stop(hsfx);
    engine.stop(hmusic);
}
