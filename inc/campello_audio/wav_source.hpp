#pragma once

#include <cstdint>
#include <string>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/types/loader.hpp>

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

    /// @brief Load and decode WAV audio using a caller-supplied loader.
    ///
    /// The loader is called immediately on the calling thread and must return
    /// the complete WAV-encoded bytes. This is the primary load overload;
    /// load(path) and loadMem() are convenience wrappers around it.
    ///
    /// @code
    /// src.load([]{ return myAssets.read("shot.wav"); });
    /// @endcode
    /// @return true on success.
    bool load(ByteLoader loader);

    /// @brief Load and decode a WAV file from disk.
    /// @return true on success.
    bool load(const std::string& path);

    /// @brief Load and decode WAV audio from a raw memory buffer.
    /// @return true on success.
    bool loadMem(const uint8_t* data, uint32_t length, bool copy = true);

    /// @brief Sample rate of the decoded audio in Hz.
    uint32_t getSampleRate() const;

    /// @brief Duration of the audio in seconds.
    double getDuration() const;
};

} // namespace systems::leal::campello_audio
