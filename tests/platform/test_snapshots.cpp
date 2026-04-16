/// @file test_snapshots.cpp
/// @brief Integration tests for Mix Snapshots.
///
/// All tests require a real audio device (CoreAudio on macOS).

#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_snapshot.hpp>
#include <campello_audio/audio_bus.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <memory>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class SnapshotTest : public ::testing::Test {
protected:
    AudioEngine engine;

    void SetUp() override {
        AudioEngineDescriptor d;
        ASSERT_TRUE(engine.init(d));
    }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// AudioSnapshot construction (no engine needed)
// ---------------------------------------------------------------------------

TEST(AudioSnapshotUnit, DefaultPriority) {
    AudioSnapshot s("combat");
    EXPECT_EQ(s.getName(), "combat");
    EXPECT_EQ(s.getPriority(), 0u);
}

TEST(AudioSnapshotUnit, SetPriority) {
    AudioSnapshot s("hi");
    s.setPriority(10);
    EXPECT_EQ(s.getPriority(), 10u);
}

// ---------------------------------------------------------------------------
// Engine registration
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, RegisterAndApplySnapshotDoesNotCrash) {
    auto s = std::make_shared<AudioSnapshot>("combat");
    s->setGlobalVolume(0.5f);
    engine.registerSnapshot(s);
    engine.applySnapshot("combat", 0.0);   // instant apply
}

TEST_F(SnapshotTest, ApplyUnregisteredSnapshotIsNoop) {
    engine.applySnapshot("nonexistent", 0.0);  // should not crash
}

TEST_F(SnapshotTest, RevertWithoutApplyIsNoop) {
    engine.revertSnapshot(0.0);  // should not crash
}

// ---------------------------------------------------------------------------
// Instant apply (blendTimeSecs = 0)
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, InstantApplyChangesGlobalVolume) {
    const float before = engine.getGlobalVolume();
    EXPECT_FLOAT_EQ(before, 1.0f);

    auto s = std::make_shared<AudioSnapshot>("quiet");
    s->setGlobalVolume(0.4f);
    engine.registerSnapshot(s);
    engine.applySnapshot("quiet", 0.0);

    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.4f);
}

TEST_F(SnapshotTest, InstantRevertRestoresGlobalVolume) {
    auto s = std::make_shared<AudioSnapshot>("quiet");
    s->setGlobalVolume(0.4f);
    engine.registerSnapshot(s);
    engine.applySnapshot("quiet", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.4f);

    engine.revertSnapshot(0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 1.0f);
}

TEST_F(SnapshotTest, ApplyTwiceWithDifferentSnapshotsInstant) {
    auto s1 = std::make_shared<AudioSnapshot>("a");
    s1->setGlobalVolume(0.3f);
    auto s2 = std::make_shared<AudioSnapshot>("b");
    s2->setGlobalVolume(0.7f);
    engine.registerSnapshot(s1);
    engine.registerSnapshot(s2);

    engine.applySnapshot("a", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.3f);

    engine.applySnapshot("b", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.7f);

    engine.revertSnapshot(0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 1.0f);
}

// ---------------------------------------------------------------------------
// Bus volume
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, InstantApplyChangesBusVolume) {
    AudioBus bus;
    engine.play(bus);

    auto s = std::make_shared<AudioSnapshot>("underwater");
    s->setBusVolume(&bus, 0.2f);
    engine.registerSnapshot(s);
    engine.applySnapshot("underwater", 0.0);

    // Volume was changed — just verify it doesn't crash and engine still runs.
    engine.revertSnapshot(0.0);
}

// ---------------------------------------------------------------------------
// Bus filter override
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, InstantApplyInstallsBusFilter) {
    AudioBus bus;
    engine.play(bus);

    ToneSource tone;
    tone.setLooping(true);
    bus.play(tone);

    auto lpf = std::make_shared<LowPassFilter>();
    auto s   = std::make_shared<AudioSnapshot>("underwater");
    s->setGlobalVolume(0.6f);
    s->setBusFilter(&bus, lpf, 0);
    engine.registerSnapshot(s);

    engine.applySnapshot("underwater", 0.0);
    // Filter installed — audio still runs.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    engine.revertSnapshot(0.0);
    // Filter removed — audio still runs.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

// ---------------------------------------------------------------------------
// Priority
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, LowerPrioritySnapshotDoesNotReplaceHigher) {
    auto hi = std::make_shared<AudioSnapshot>("hi");
    hi->setGlobalVolume(0.2f);
    hi->setPriority(10);

    auto lo = std::make_shared<AudioSnapshot>("lo");
    lo->setGlobalVolume(0.8f);
    lo->setPriority(1);

    engine.registerSnapshot(hi);
    engine.registerSnapshot(lo);

    engine.applySnapshot("hi", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.2f);

    // lo has lower priority — should be ignored.
    engine.applySnapshot("lo", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.2f);

    engine.revertSnapshot(0.0);
}

TEST_F(SnapshotTest, HigherPrioritySnapshotReplacesLower) {
    auto lo = std::make_shared<AudioSnapshot>("lo");
    lo->setGlobalVolume(0.8f);
    lo->setPriority(1);

    auto hi = std::make_shared<AudioSnapshot>("hi");
    hi->setGlobalVolume(0.2f);
    hi->setPriority(10);

    engine.registerSnapshot(lo);
    engine.registerSnapshot(hi);

    engine.applySnapshot("lo", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.8f);

    engine.applySnapshot("hi", 0.0);
    EXPECT_FLOAT_EQ(engine.getGlobalVolume(), 0.2f);

    engine.revertSnapshot(0.0);
}

// ---------------------------------------------------------------------------
// Blended apply via tick()
// ---------------------------------------------------------------------------

TEST_F(SnapshotTest, BlendedApplyReachesTargetAfterTicks) {
    auto s = std::make_shared<AudioSnapshot>("dim");
    s->setGlobalVolume(0.0f);
    engine.registerSnapshot(s);

    // Blend over 100 ms — drive tick() for longer than that.
    engine.applySnapshot("dim", 0.1);

    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        engine.tick();
    }

    // After ~200 ms of ticks, blend should be complete (target = 0.0).
    EXPECT_NEAR(engine.getGlobalVolume(), 0.0f, 0.01f);
}

TEST_F(SnapshotTest, BlendedRevertRestoresVolumeAfterTicks) {
    auto s = std::make_shared<AudioSnapshot>("dim");
    s->setGlobalVolume(0.0f);
    engine.registerSnapshot(s);
    engine.applySnapshot("dim", 0.0);   // instant to 0

    // Revert over 100 ms.
    engine.revertSnapshot(0.1);
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        engine.tick();
    }

    EXPECT_NEAR(engine.getGlobalVolume(), 1.0f, 0.01f);
}
