/// @file audio_stream.cpp
/// @brief Ring-buffer streaming source with background prefetch thread.

#include "audio_stream.hpp"

#include <algorithm>
#include <cstring>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// AudioStreamData::open
// ---------------------------------------------------------------------------

bool AudioStreamData::open(const std::string& path) {
    if (isOpen) close();

    decoder = streamDecoderOpen(path);
    if (!decoder) return false;

    channels    = decoder->channels;
    sampleRate  = decoder->sampleRate;
    totalFrames = decoder->totalFrames;

    // Allocate ring buffer
    ring.assign(kRingFrames * channels, 0.0f);
    ringRead  = 0;
    ringWrite = 0;
    ringFill  = 0;
    eof.store(false, std::memory_order_relaxed);
    seekPending.store(false, std::memory_order_relaxed);

    isOpen  = true;
    running.store(true, std::memory_order_relaxed);
    prefetchThread = std::thread([this] { prefetchLoop(); });

    return true;
}

// ---------------------------------------------------------------------------
// AudioStreamData::close
// ---------------------------------------------------------------------------

void AudioStreamData::close() {
    if (!isOpen) return;

    // Signal thread to stop and wake it if it is waiting
    running.store(false, std::memory_order_release);
    ringNotFull.notify_all();

    if (prefetchThread.joinable()) prefetchThread.join();

    streamDecoderClose(decoder);
    decoder  = nullptr;
    isOpen   = false;
    channels = 0;
    ring.clear();
}

// ---------------------------------------------------------------------------
// AudioStreamData::requestSeek
// ---------------------------------------------------------------------------

void AudioStreamData::requestSeek(double seconds) {
    if (!isOpen || !decoder) return;
    const uint64_t frame =
        static_cast<uint64_t>(seconds * static_cast<double>(sampleRate));
    seekTargetFrame.store(frame, std::memory_order_relaxed);
    seekPending.store(true, std::memory_order_release);
    ringNotFull.notify_all();  // wake prefetch thread if it is blocked
}

// ---------------------------------------------------------------------------
// AudioStreamData::consumeFrames  (mixer thread)
// ---------------------------------------------------------------------------

uint32_t AudioStreamData::consumeFrames(float* out,
                                        uint32_t maxFrames,
                                        uint32_t outCh) {
    std::lock_guard lk(ringMutex);
    const uint32_t toRead = std::min(ringFill, maxFrames);

    for (uint32_t f = 0; f < toRead; ++f) {
        const uint32_t srcIdx = ringRead * channels;
        if (outCh >= 2 && channels == 1) {
            out[f * outCh + 0] = ring[srcIdx];
            out[f * outCh + 1] = ring[srcIdx];
        } else if (outCh >= 2 && channels >= 2) {
            out[f * outCh + 0] = ring[srcIdx + 0];
            out[f * outCh + 1] = ring[srcIdx + 1];
        } else {
            out[f] = ring[srcIdx];
        }
        ringRead = (ringRead + 1) % kRingFrames;
        --ringFill;
    }

    ringNotFull.notify_one();
    return toRead;
}

// ---------------------------------------------------------------------------
// AudioStreamData::prefetchLoop  (background thread)
// ---------------------------------------------------------------------------

void AudioStreamData::prefetchLoop() {
    constexpr uint32_t kChunk = 512;
    std::vector<float> tmp(kChunk * channels);

    while (running.load(std::memory_order_acquire)) {

        // ---- Handle pending seek ----
        if (seekPending.load(std::memory_order_acquire)) {
            const uint64_t target = seekTargetFrame.load(std::memory_order_relaxed);
            streamDecoderSeek(decoder, target);
            {
                std::lock_guard lk(ringMutex);
                ringRead  = 0;
                ringWrite = 0;
                ringFill  = 0;
                eof.store(false, std::memory_order_release);
            }
            ringNotFull.notify_all();
            seekPending.store(false, std::memory_order_release);
            continue;
        }

        // ---- Wait while ring buffer is full ----
        {
            std::unique_lock lk(ringMutex);
            ringNotFull.wait(lk, [this] {
                return ringFill < kRingFrames ||
                       !running.load(std::memory_order_relaxed) ||
                       seekPending.load(std::memory_order_relaxed);
            });
            if (!running.load(std::memory_order_relaxed)) break;
        }

        // Re-check seek in case we were woken for that reason
        if (seekPending.load(std::memory_order_acquire)) continue;

        // ---- How many frames can we write? ----
        uint32_t freeFrames = 0;
        {
            std::lock_guard lk(ringMutex);
            freeFrames = kRingFrames - ringFill;
        }
        if (freeFrames == 0) continue;

        const uint32_t toRead = std::min(kChunk, freeFrames);

        // ---- Decode outside the lock ----
        uint32_t got = streamDecoderRead(decoder, tmp.data(), toRead);

        if (got == 0) {
            if (looping) {
                streamDecoderSeek(decoder, 0);
                continue;
            } else {
                eof.store(true, std::memory_order_release);
                break;
            }
        }

        // ---- Copy decoded frames into ring buffer ----
        {
            std::lock_guard lk(ringMutex);
            for (uint32_t f = 0; f < got; ++f) {
                const uint32_t idx = ringWrite * channels;
                for (uint32_t c = 0; c < channels; ++c)
                    ring[idx + c] = tmp[f * channels + c];
                ringWrite = (ringWrite + 1) % kRingFrames;
                ++ringFill;
            }
        }
    }
}

} // namespace systems::leal::campello_audio::pi
