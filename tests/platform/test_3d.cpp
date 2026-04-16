#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/descriptors/listener_descriptor.hpp>
#include <campello_audio/constants/attenuation_model.hpp>
#include <campello_audio/tone_source.hpp>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

class Audio3DTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp()    override { engine.init({}); }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// Basic API smoke tests
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, SetListenerDoesNotCrash) {
    ListenerDescriptor listener;
    listener.position = {10.0f, 0.0f, 0.0f};
    listener.forward  = {0.0f, 0.0f, -1.0f};
    listener.up       = {0.0f, 1.0f,  0.0f};
    EXPECT_NO_THROW(engine.set3dListenerParameters(listener));
}

TEST_F(Audio3DTest, Update3dWithNoVoicesDoesNotCrash) {
    EXPECT_NO_THROW(engine.update3d());
}

TEST_F(Audio3DTest, Play3dDoesNotCrash) {
    ToneSource tone(WaveForm::Sine, 220.0f);
    EXPECT_NO_THROW(engine.play3d(tone, 5.0f, 0.0f, -10.0f));
}

TEST_F(Audio3DTest, Update3dWithActive3dVoiceDoesNotCrash) {
    ToneSource tone;
    SoundHandle h = engine.play3d(tone, 0.0f, 0.0f, -5.0f);
    EXPECT_TRUE(h.isValid());
    EXPECT_NO_THROW(engine.update3d());
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// 3D voice stays valid after update3d()
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, VoiceRemainsValidAfterUpdate3d) {
    ToneSource tone;
    PlayDescriptor pd;
    pd.enable3d    = true;
    pd.position    = {0.0f, 0.0f, -5.0f};  // within default minDist
    pd.minDistance = 1.0f;
    pd.maxDistance = 100.0f;

    SoundHandle h = engine.play(tone, pd);
    EXPECT_TRUE(engine.isValid(h));

    engine.update3d();

    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Distance attenuation: voice far beyond maxDist gets virtualized
// (Linear model with rolloff = 1.0 → gain = 0 at maxDist)
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, VoiceBeyondMaxDistGetsVirtualized) {
    // Use a low virtualization threshold so attenuation alone can trigger it.
    AudioEngineDescriptor desc;
    desc.maxVoices           = 32;
    desc.maxVirtualVoices    = 32;
    desc.virtualizeThreshold = 0.1f;
    engine.deinit();
    engine.init(desc);

    ToneSource tone;
    PlayDescriptor pd;
    pd.enable3d       = true;
    pd.position       = {0.0f, 0.0f, -500.0f}; // way beyond maxDist
    pd.minDistance    = 1.0f;
    pd.maxDistance    = 100.0f;
    pd.rolloff        = 1.0f;
    pd.volume         = 1.0f;

    SoundHandle h = engine.play(tone, pd);
    EXPECT_TRUE(engine.isValid(h));

    engine.set3dSourceAttenuation(h, AttenuationModel::Linear, 1.0f);

    // Listener at origin — distance to source is ~500
    ListenerDescriptor ld;
    ld.position = {0.0f, 0.0f, 0.0f};
    ld.forward  = {0.0f, 0.0f, -1.0f};
    ld.up       = {0.0f, 1.0f,  0.0f};
    engine.set3dListenerParameters(ld);

    engine.update3d();

    // Wait for the audio thread to run the virtualization sweep
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Voice should be virtualized (real pool empty, virtual pool has 1)
    EXPECT_EQ(engine.getActiveVoiceCount(),  0u);
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);
    EXPECT_TRUE(engine.isValid(h));         // still logically alive
}

// ---------------------------------------------------------------------------
// Bring a far-away voice back into range → it re-activates
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, VirtualizedVoiceReactivatesWhenMovedClose) {
    AudioEngineDescriptor desc;
    desc.maxVoices           = 32;
    desc.maxVirtualVoices    = 32;
    desc.virtualizeThreshold = 0.1f;
    engine.deinit();
    engine.init(desc);

    ToneSource tone;
    PlayDescriptor pd;
    pd.enable3d    = true;
    pd.position    = {0.0f, 0.0f, -500.0f};
    pd.minDistance = 1.0f;
    pd.maxDistance = 100.0f;
    pd.rolloff     = 1.0f;
    pd.volume      = 1.0f;

    SoundHandle h = engine.play(tone, pd);
    engine.set3dSourceAttenuation(h, AttenuationModel::Linear, 1.0f);

    ListenerDescriptor ld;
    ld.position = {0.0f, 0.0f, 0.0f};
    ld.forward  = {0.0f, 0.0f, -1.0f};
    ld.up       = {0.0f, 1.0f,  0.0f};
    engine.set3dListenerParameters(ld);
    engine.update3d();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(engine.getVirtualVoiceCount(), 1u);

    // Move source to minDist → attenuation = 1, effective volume = 1 → re-activate
    engine.set3dSourceParameters(h, 0.0f, 0.0f, -1.0f);
    engine.update3d();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(engine.getVirtualVoiceCount(), 0u);
    EXPECT_EQ(engine.getActiveVoiceCount(),  1u);
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// Multi-listener index 0 is equivalent to single-listener overload
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, SetListenerIndex0EquivalentToDefault) {
    ListenerDescriptor ld;
    ld.position = {5.0f, 0.0f, 0.0f};
    ld.forward  = {0.0f, 0.0f, -1.0f};
    ld.up       = {0.0f, 1.0f,  0.0f};
    EXPECT_NO_THROW(engine.set3dListenerParameters(0, ld));
}

// ---------------------------------------------------------------------------
// set3dSoundSpeed and dopplerFactor APIs don't crash
// ---------------------------------------------------------------------------

TEST_F(Audio3DTest, SoundSpeedAndDopplerFactor) {
    engine.set3dSoundSpeed(340.0f);

    ToneSource tone;
    SoundHandle h = engine.play3d(tone, 0.0f, 0.0f, -10.0f);
    EXPECT_NO_THROW(engine.set3dSourceDopplerFactor(h, 2.0f));
    EXPECT_NO_THROW(engine.update3d());
    engine.stop(h);
}
