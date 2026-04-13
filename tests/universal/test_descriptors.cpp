#include <gtest/gtest.h>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <campello_audio/descriptors/play_descriptor.hpp>
#include <campello_audio/descriptors/listener_descriptor.hpp>

using namespace systems::leal::campello_audio;

TEST(AudioEngineDescriptor, Defaults) {
    AudioEngineDescriptor d;
    EXPECT_EQ(d.sampleRate,    44100u);
    EXPECT_EQ(d.bufferSize,    512u);
    EXPECT_EQ(d.maxVoices,     32u);
    EXPECT_EQ(d.channels,      2u);
    EXPECT_FALSE(d.visualization);
}

TEST(PlayDescriptor, Defaults) {
    PlayDescriptor pd;
    EXPECT_FLOAT_EQ(pd.volume, 1.0f);
    EXPECT_FLOAT_EQ(pd.pan,    0.0f);
    EXPECT_FLOAT_EQ(pd.pitch,  1.0f);
    EXPECT_FALSE(pd.looping);
    EXPECT_FALSE(pd.paused);
    EXPECT_FALSE(pd.protect);
    EXPECT_FALSE(pd.enable3d);
    EXPECT_EQ(pd.bus, nullptr);
}

TEST(ListenerDescriptor, Defaults) {
    ListenerDescriptor l;
    EXPECT_FLOAT_EQ(l.position.x, 0.0f);
    EXPECT_FLOAT_EQ(l.forward.z, -1.0f);
    EXPECT_FLOAT_EQ(l.up.y,       1.0f);
}
