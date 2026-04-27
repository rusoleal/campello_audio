#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/tone_source.hpp>

using namespace systems::leal::campello_audio;

TEST(WasmBackend, InitReturnsTrue) {
    AudioEngine engine;
    EXPECT_TRUE(engine.init({}));
    EXPECT_EQ(engine.getChannels(), 2u);
    engine.deinit();
}

TEST(WasmBackend, PlayToneProducesVoices) {
    AudioEngine engine;
    ASSERT_TRUE(engine.init({}));

    ToneSource tone;
    tone.setWaveForm(WaveForm::Sine);
    tone.setFrequency(440.0f);

    SoundHandle h = engine.play(tone);
    EXPECT_TRUE(h.isValid());
    EXPECT_EQ(engine.getActiveVoiceCount(), 1u);

    engine.stop(h);
    EXPECT_EQ(engine.getActiveVoiceCount(), 0u);

    engine.deinit();
}

TEST(WasmBackend, InitDeinitIdempotent) {
    AudioEngine engine;
    EXPECT_TRUE(engine.init({}));
    engine.deinit();
    EXPECT_TRUE(engine.init({}));
    engine.deinit();
}
