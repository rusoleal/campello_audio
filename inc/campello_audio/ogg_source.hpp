#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <campello_audio/audio_source.hpp>

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

    /// @brief Load and decode an OGG Vorbis file from disk.
    /// @return true on success.
    bool load(const std::string& path);

    /// @brief Load and decode OGG Vorbis audio from a memory buffer.
    /// @param data   Pointer to OGG-encoded bytes.
    /// @param length Buffer length in bytes.
    /// @param copy   If true the buffer is copied.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Load and decode an OGG file on a background thread.
    /// @param onComplete Called on the calling thread's next engine tick with
    ///                   true on success or false on failure.
    void loadAsync(const std::string& path,
                   std::function<void(bool success)> onComplete);

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
