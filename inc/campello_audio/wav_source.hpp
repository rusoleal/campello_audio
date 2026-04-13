#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <campello_audio/audio_source.hpp>

namespace systems::leal::campello_audio {

/// @brief PCM/WAV audio source loaded from a file or memory buffer.
///
/// The entire audio data is decoded and held in memory after load().
/// Use AudioStream for large files that should be decoded on the fly.
///
/// @code
/// WavSource shot;
/// shot.load("assets/sounds/gunshot.wav");
/// engine.play(shot);
/// @endcode
class WavSource : public AudioSource {
public:
    WavSource();
    ~WavSource() override;

    /// @brief Load and decode a WAV file from disk.
    /// @return true on success, false if the file could not be read/decoded.
    bool load(const std::string& path);

    /// @brief Load and decode WAV audio from a memory buffer.
    /// @param data   Pointer to WAV-encoded bytes.
    /// @param length Buffer length in bytes.
    /// @param copy   If true the buffer is copied; if false the caller must
    ///               keep @p data alive for the lifetime of this source.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Load and decode a WAV file on a background thread.
    /// @param path       Path to the WAV file.
    /// @param onComplete Called on the calling thread's next engine tick with
    ///                   true on success or false on failure. Safe to call
    ///                   engine.play() from this callback.
    void loadAsync(const std::string& path,
                   std::function<void(bool success)> onComplete);

    /// @brief Sample rate of the decoded audio in Hz.
    uint32_t getSampleRate() const;

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
