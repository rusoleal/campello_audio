#pragma once

/// @file audio_stream.hpp
/// @brief Internal ring-buffer streaming data for AudioStream.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// StreamDecoder — format-agnostic streaming handle.
//
// The internal 'handle' field points to a heap-allocated dr_wav / stb_vorbis /
// dr_mp3 context; the actual type is known only to stream_decoder.cpp.
// All other code uses the free functions below.
// ---------------------------------------------------------------------------
struct StreamDecoder {
    enum class Format { WAV, OGG, MP3 } format;
    void*    handle      = nullptr;
    uint32_t channels    = 0;
    uint32_t sampleRate  = 0;
    uint64_t totalFrames = 0;
};

StreamDecoder* streamDecoderOpen(const std::string& path);
uint32_t       streamDecoderRead(StreamDecoder* d, float* out, uint32_t frames);
bool           streamDecoderSeek(StreamDecoder* d, uint64_t frame);
void           streamDecoderClose(StreamDecoder* d);

// ---------------------------------------------------------------------------
// AudioStreamData — owns the ring buffer and the background prefetch thread.
// ---------------------------------------------------------------------------
struct AudioStreamData {
    static constexpr uint32_t kRingFrames = 88200;  // 2 seconds at 44100 Hz

    // Ring buffer — kRingFrames × channels floats.
    // ringRead  : next frame to consume (in [0, kRingFrames))
    // ringWrite : next frame to write   (in [0, kRingFrames))
    // ringFill  : number of valid frames currently buffered
    std::vector<float>      ring;
    std::mutex              ringMutex;
    uint32_t                ringRead   = 0;
    uint32_t                ringWrite  = 0;
    uint32_t                ringFill   = 0;
    std::condition_variable ringNotFull;

    // Background prefetch
    std::thread             prefetchThread;
    std::atomic<bool>       running{false};

    // Seek request — game thread writes, prefetch thread services
    std::atomic<bool>       seekPending{false};
    std::atomic<uint64_t>   seekTargetFrame{0};

    // EOF — set by prefetch thread when a non-looping stream exhausts its data
    std::atomic<bool>       eof{false};

    // Source metadata
    bool     looping     = false;
    bool     isOpen      = false;
    uint32_t channels    = 0;
    uint32_t sampleRate  = 0;
    uint64_t totalFrames = 0;   ///< 0 if unknown

    StreamDecoder* decoder = nullptr;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    AudioStreamData()  = default;
    ~AudioStreamData() { close(); }

    // Non-copyable / non-movable (thread safety)
    AudioStreamData(const AudioStreamData&)            = delete;
    AudioStreamData& operator=(const AudioStreamData&) = delete;

    /// @brief Open the file at @p path and start the prefetch thread.
    /// @return true on success.
    bool open(const std::string& path);

    /// @brief Stop the prefetch thread and release all resources.
    void close();

    /// @brief Request a seek to @p seconds from the game thread.
    void requestSeek(double seconds);

    // -----------------------------------------------------------------------
    // Mixer-thread interface
    // -----------------------------------------------------------------------

    /// @brief Consume up to @p maxFrames from the ring buffer.
    ///
    /// Called from the audio thread. Acquires ringMutex briefly.
    /// Channel up-mix / down-mix to @p outCh is handled here.
    ///
    /// @return Number of frames actually copied to @p out.
    uint32_t consumeFrames(float* out, uint32_t maxFrames, uint32_t outCh);

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    /// @brief Approximate memory consumed by the ring buffer.
    uint32_t getBufferMemoryBytes() const {
        return kRingFrames * channels * sizeof(float);
    }

private:
    void prefetchLoop();
};

} // namespace systems::leal::campello_audio::pi
