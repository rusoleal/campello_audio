#pragma once

#include <cstdint>
#include <string>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/types/loader.hpp>

namespace systems::leal::campello_audio {

/// @brief MP3 audio source (fully decoded into memory on load).
///
/// Decoded using dr_mp3. For streaming use AudioStream.
class Mp3Source : public AudioSource {
public:
    Mp3Source();
    ~Mp3Source() override;

    /// @brief Load and decode MP3 audio using a caller-supplied loader.
    /// @return true on success.
    bool load(ByteLoader loader);

    /// @brief Load and decode an MP3 file from disk.
    /// @return true on success.
    bool load(const std::string& path);

    /// @brief Load and decode MP3 audio from a memory buffer.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
