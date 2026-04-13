#include <gtest/gtest.h>
#include <campello_audio/types/sound_handle.hpp>

using namespace systems::leal::campello_audio;

TEST(SoundHandle, InvalidByDefault) {
    SoundHandle h;
    EXPECT_FALSE(h.isValid());
    EXPECT_EQ(h, SoundHandle::invalid);
}

TEST(SoundHandle, ValidHandle) {
    SoundHandle h{42};
    EXPECT_TRUE(h.isValid());
    EXPECT_NE(h, SoundHandle::invalid);
}

TEST(SoundHandle, Equality) {
    SoundHandle a{10};
    SoundHandle b{10};
    SoundHandle c{20};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
