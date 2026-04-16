#pragma once

#include <cstdint>
#include <string>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/types/loader.hpp>

namespace systems::leal::campello_audio {

/// @brief OGG Vorbis audio source (fully decoded into memory on load).
///
/// Decoded using stb_vorbis. For streaming large OGG files use AudioStream.
///
/// @code
/// OggSource music;
/// music.load("assets/music/theme.ogg");
/// music.setLooping(true);
/// engine.play(music);
/// @endcode
class OggSource : public AudioSource {
public:
    OggSource();
    ~OggSource() override;

    /// @brief Load and decode OGG Vorbis audio using a caller-supplied loader.
    /// @return true on success.
    bool load(ByteLoader loader);

    /// @brief Load and decode an OGG Vorbis file from disk.
    /// @return true on success.
    bool load(const std::string& path);

    /// @brief Load and decode OGG Vorbis audio from a memory buffer.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
