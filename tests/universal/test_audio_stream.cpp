/// @file test_audio_stream.cpp
/// @brief Universal tests for AudioStream — no audio device required.
///
/// Tests cover error paths, ring-buffer memory bounds, and open/close lifecycle.
/// Streaming with real audio playback is in tests/platform/test_streaming.cpp.

#include <gtest/gtest.h>
#include <campello_audio/audio_stream.hpp>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Lifecycle tests
// ---------------------------------------------------------------------------

TEST(AudioStream, DefaultConstructNotOpen) {
    AudioStream s;
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.getSampleRate(), 0u);
    EXPECT_DOUBLE_EQ(s.getDuration(), 0.0);
    EXPECT_EQ(s.getBufferMemoryBytes(), 0u);
}

TEST(AudioStream, OpenNonExistentFileFails) {
    AudioStream s;
    EXPECT_FALSE(s.open("/nonexistent/path/that/cannot/exist.wav"));
    EXPECT_FALSE(s.isOpen());
}

TEST(AudioStream, OpenUnsupportedExtensionFails) {
    AudioStream s;
    EXPECT_FALSE(s.open("/tmp/campello_test.flac"));
    EXPECT_FALSE(s.isOpen());
}

TEST(AudioStream, CloseOnNotOpenIsNoOp) {
    AudioStream s;
    s.close();  // should not crash
    EXPECT_FALSE(s.isOpen());
}

TEST(AudioStream, OpenFailedThenCloseIsNoOp) {
    AudioStream s;
    EXPECT_FALSE(s.open("/nonexistent/path.wav"));
    s.close();  // should not crash
    EXPECT_FALSE(s.isOpen());
}

TEST(AudioStream, DestructorClosesGracefully) {
    // Just ensure the destructor on a never-opened stream does not crash.
    {
        AudioStream s;
        (void)s;
    }
}

// ---------------------------------------------------------------------------
// Ring buffer memory bounds
// ---------------------------------------------------------------------------

TEST(AudioStream, BufferMemoryBytesZeroWhenNotOpen) {
    AudioStream s;
    EXPECT_EQ(s.getBufferMemoryBytes(), 0u);
}

// ---------------------------------------------------------------------------
// Multiple open/close cycles
// ---------------------------------------------------------------------------

TEST(AudioStream, MultipleOpenFailsDoNotLeak) {
    AudioStream s;
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(s.open("/nonexistent/path.wav"));
        EXPECT_FALSE(s.isOpen());
    }
}
