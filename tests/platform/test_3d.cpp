#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/descriptors/listener_descriptor.hpp>
#include <campello_audio/tone_source.hpp>

using namespace systems::leal::campello_audio;

class Audio3DTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp()    override { engine.init({}); }
    void TearDown() override { engine.deinit(); }
};

TEST_F(Audio3DTest, SetListenerDoesNotCrash) {
    ListenerDescriptor listener;
    listener.position = {10.0f, 0.0f, 0.0f};
    listener.forward  = {0.0f, 0.0f, -1.0f};
    listener.up       = {0.0f, 1.0f, 0.0f};
    EXPECT_NO_THROW(engine.set3dListenerParameters(listener));
}

TEST_F(Audio3DTest, Update3dDoesNotCrash) {
    EXPECT_NO_THROW(engine.update3d());
}

TEST_F(Audio3DTest, Play3dDoesNotCrash) {
    ToneSource tone(WaveForm::Sine, 220.0f);
    EXPECT_NO_THROW(engine.play3d(tone, 5.0f, 0.0f, -10.0f));
}
