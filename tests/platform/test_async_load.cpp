#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/ogg_source.hpp>
#include <campello_audio/types/loader.hpp>
#include <future>
#include <thread>
#include <chrono>

using namespace systems::leal::campello_audio;

// ---------------------------------------------------------------------------
// Minimal silent WAV helper (same shape as test_playback.cpp)
// ---------------------------------------------------------------------------
namespace {
std::vector<uint8_t> makeSilentWav(uint32_t sr = 44100, uint16_t ch = 1,
                                    uint32_t frames = 512) {
    const uint32_t bps      = 2;
    const uint32_t dataSize = frames * ch * bps;
    const uint32_t riffSize = 4 + 24 + 8 + dataSize;
    std::vector<uint8_t> b;
    auto p16 = [&](uint16_t v){ b.push_back(v & 0xFF); b.push_back(v >> 8); };
    auto p32 = [&](uint32_t v){ for(int i=0;i<4;++i){b.push_back((v>>(i*8))&0xFF);} };
    auto ps  = [&](const char* s){ while(*s) b.push_back((uint8_t)*s++); };
    ps("RIFF"); p32(riffSize); ps("WAVE");
    ps("fmt "); p32(16); p16(1); p16(ch); p32(sr);
    p32(sr*ch*bps); p16(ch*bps); p16(16);
    ps("data"); p32(dataSize);
    b.resize(b.size() + dataSize, 0);
    return b;
}
} // namespace

class AsyncLoadTest : public ::testing::Test {
protected:
    AudioEngine engine;
    void SetUp()    override { engine.init({}); }
    void TearDown() override { engine.deinit(); }
};

// ---------------------------------------------------------------------------
// Sync ByteLoader — primary load overload
// ---------------------------------------------------------------------------

TEST_F(AsyncLoadTest, SyncByteLoaderWav) {
    WavSource src;
    auto wav = makeSilentWav();
    bool ok = src.load([&]() -> std::vector<uint8_t> { return wav; });
    EXPECT_TRUE(ok);
    auto h = engine.play(src);
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

TEST_F(AsyncLoadTest, SyncByteLoaderEmptyBytesReturnsFalse) {
    WavSource src;
    bool ok = src.load([]() -> std::vector<uint8_t> { return {}; });
    EXPECT_FALSE(ok);
    EXPECT_FALSE(engine.play(src).isValid());
}

// ---------------------------------------------------------------------------
// Convenience wrappers still work after refactor
// ---------------------------------------------------------------------------

TEST_F(AsyncLoadTest, LoadMemConvenienceWrapperWorks) {
    WavSource src;
    auto wav = makeSilentWav();
    EXPECT_TRUE(src.loadMem(wav.data(), static_cast<uint32_t>(wav.size())));
    auto h = engine.play(src);
    EXPECT_TRUE(engine.isValid(h));
    engine.stop(h);
}

// ---------------------------------------------------------------------------
// engine.loadAsync — async path
// ---------------------------------------------------------------------------

TEST_F(AsyncLoadTest, AsyncLoadFiresCallbackWithSuccess) {
    WavSource src;
    auto wav = makeSilentWav();

    bool callbackFired  = false;
    bool callbackResult = false;

    engine.loadAsync(src,
        [wav]() -> std::future<std::vector<uint8_t>> {
            return std::async(std::launch::async, [wav]{ return wav; });
        },
        [&](bool ok) {
            callbackFired  = true;
            callbackResult = ok;
        }
    );

    // Give the async task time to finish, then drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.tick();

    EXPECT_TRUE(callbackFired);
    EXPECT_TRUE(callbackResult);
}

TEST_F(AsyncLoadTest, AsyncLoadSourcePlayableAfterTick) {
    WavSource src;
    auto wav = makeSilentWav();

    engine.loadAsync(src,
        [wav]() -> std::future<std::vector<uint8_t>> {
            return std::async(std::launch::async, [wav]{ return wav; });
        },
        [&](bool ok) {
            ASSERT_TRUE(ok);
            SoundHandle h = engine.play(src);
            EXPECT_TRUE(engine.isValid(h));
            engine.stop(h);
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.tick();
}

TEST_F(AsyncLoadTest, CallbackNotFiredBeforeTick) {
    WavSource src;
    auto wav = makeSilentWav();
    bool callbackFired = false;

    engine.loadAsync(src,
        [wav]() -> std::future<std::vector<uint8_t>> {
            return std::async(std::launch::async, [wav]{ return wav; });
        },
        [&](bool) { callbackFired = true; }
    );

    // Deliberately do NOT call tick().
    EXPECT_FALSE(callbackFired);
}

TEST_F(AsyncLoadTest, AsyncLoadEmptyBytesCallbackFails) {
    WavSource src;
    bool callbackResult = true;

    engine.loadAsync(src,
        []() -> std::future<std::vector<uint8_t>> {
            return std::async(std::launch::async, []{ return std::vector<uint8_t>{}; });
        },
        [&](bool ok) { callbackResult = ok; }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.tick();
    EXPECT_FALSE(callbackResult);
}

TEST_F(AsyncLoadTest, DeferredFutureResolvesInsideTick) {
    // std::launch::deferred: the work runs synchronously when .get() is called,
    // which happens inside tick(). No sleep needed.
    WavSource src;
    auto wav = makeSilentWav();
    bool callbackFired = false;

    engine.loadAsync(src,
        [wav]() -> std::future<std::vector<uint8_t>> {
            return std::async(std::launch::deferred, [wav]{ return wav; });
        },
        [&](bool ok) { callbackFired = ok; }
    );

    engine.tick();  // deferred work runs here
    EXPECT_TRUE(callbackFired);
}

TEST_F(AsyncLoadTest, MultipleAsyncLoadsAllFire) {
    WavSource src1, src2, src3;
    auto wav = makeSilentWav();
    int fired = 0;

    auto loader = [wav]() -> std::future<std::vector<uint8_t>> {
        return std::async(std::launch::async, [wav]{ return wav; });
    };
    auto cb = [&](bool ok) { if (ok) ++fired; };

    engine.loadAsync(src1, loader, cb);
    engine.loadAsync(src2, loader, cb);
    engine.loadAsync(src3, loader, cb);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    engine.tick();

    EXPECT_EQ(fired, 3);
}
