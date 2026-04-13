#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <campello_audio/audio_source.hpp>

namespace systems::leal::campello_audio {

/// @brief MP3 audio source (fully decoded into memory on load).
///
/// Decoded using dr_mp3. For streaming use AudioStream.
class Mp3Source : public AudioSource {
public:
    Mp3Source();
    ~Mp3Source() override;

    /// @brief Load and decode an MP3 file from disk.
    /// @return true on success.
    bool load(const std::string& path);

    /// @brief Load and decode MP3 audio from a memory buffer.
    /// @param data   Pointer to MP3-encoded bytes.
    /// @param length Buffer length in bytes.
    /// @param copy   If true the buffer is copied.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Load and decode an MP3 file on a background thread.
    /// @param onComplete Called on the calling thread's next engine tick with
    ///                   true on success or false on failure.
    void loadAsync(const std::string& path,
                   std::function<void(bool success)> onComplete);

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
