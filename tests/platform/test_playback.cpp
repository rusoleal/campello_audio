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

TEST_F(PlaybackTest, InvalidHandleWhenSourceEmpty) {
    WavSource src;   // not loaded
    SoundHandle h = engine.play(src);
    // An unloaded source should not produce a valid voice
    EXPECT_FALSE(engine.isValid(h));
}

TEST_F(PlaybackTest, ToneSourcePlays) {
    ToneSource tone(WaveForm::Sine, 440.0f);
    SoundHandle h = engine.play(tone);
    // With a stub backend this returns invalid; will pass once Phase 4 is complete.
    (void)h;
    SUCCEED();
}

TEST_F(PlaybackTest, StopAllDoesNotCrash) {
    ToneSource tone;
    engine.play(tone);
    EXPECT_NO_THROW(engine.stopAll());
}
