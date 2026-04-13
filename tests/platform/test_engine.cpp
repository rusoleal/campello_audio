#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>

using namespace systems::leal::campello_audio;

TEST(AudioEngine, InitDeinit) {
    AudioEngine engine;
    EXPECT_TRUE(engine.init({}));
    EXPECT_EQ(engine.getSampleRate(), 44100u);
    EXPECT_EQ(engine.getMaxVoices(),  32u);
    EXPECT_EQ(engine.getChannels(),   2u);
    engine.deinit();
}

TEST(AudioEngine, DefaultVolume) {
    AudioEngine engine;
    engine.init({});
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 1.0f);
    engine.setGlobalVolume(0.5f);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.5f);
    engine.deinit();
}

TEST(AudioEngine, NoActivevoicesAfterInit) {
    AudioEngine engine;
    engine.init({});
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);
    engine.deinit();
}
