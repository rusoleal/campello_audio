#pragma once

#include <cstdint>
#include <string>
#include <campello_audio/audio_source.hpp>

namespace systems::leal::campello_audio {

/// @brief Streaming audio source — decodes compressed audio on the fly.
///
/// Unlike WavSource / OggSource / Mp3Source (which decode the entire file
/// into RAM), AudioStream reads and decodes small chunks at playback time.
/// Use this for long music tracks or ambient loops to keep memory usage low.
///
/// Supports WAV, OGG Vorbis, and MP3 (format detected from file extension).
///
/// @note Seeking within a stream is supported but may cause a brief stall
///       while the decoder repositions to the target frame.
///
/// @code
/// AudioStream music;
/// music.open("assets/music/theme.ogg");
/// music.setLooping(true);
/// engine.play(music);
/// @endcode
class AudioStream : public AudioSource {
public:
    AudioStream();
    ~AudioStream() override;

    /// @brief Open a compressed audio file for streaming.
    /// @return true on success (file exists and format is supported).
    bool open(const std::string& path);

    /// @brief Close the stream and release the file handle.
    void close();

    /// @return true if the stream is open and ready for playback.
    bool isOpen() const;

    /// @return Duration of the audio in seconds, or 0 if unknown.
    double getDuration() const;

    /// @return Sample rate of the source audio in Hz.
    uint32_t getSampleRate() const;

    /// @return Approximate memory used for the decode ring buffer (bytes).
    uint32_t getBufferMemoryBytes() const;
};

} // namespace systems::leal::campello_audio
