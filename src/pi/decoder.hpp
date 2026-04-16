#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace systems::leal::campello_audio::pi {

/// @brief Decoded PCM audio held in memory.
///
/// All audio is normalised to interleaved 32-bit float samples in the range
/// [-1, +1] regardless of the source bit depth. This is the only in-memory
/// format the mixer consumes.
struct DecodedBuffer {
    /// Interleaved float PCM samples: [ch0_f0, ch1_f0, ch0_f1, ch1_f1, ...]
    std::vector<float> samples;

    uint32_t channels   = 0;
    uint32_t sampleRate = 0;
    uint64_t frameCount = 0;  ///< Total PCM frames (samples.size() / channels).

    bool   isValid()     const { return !samples.empty() && channels > 0 && sampleRate > 0; }
    double getDuration() const {
        return sampleRate > 0 ? static_cast<double>(frameCount) / sampleRate : 0.0;
    }
};

// ---------------------------------------------------------------------------
// Decoder functions — one per format, each in its own .cpp TU so the
// single-header implementation macros (DR_WAV_IMPLEMENTATION etc.) are
// defined exactly once.
// ---------------------------------------------------------------------------

DecodedBuffer decodeWav(const std::string& path);
DecodedBuffer decodeWavMem(const uint8_t* data, uint32_t length);

DecodedBuffer decodeOgg(const std::string& path);
DecodedBuffer decodeOggMem(const uint8_t* data, uint32_t length);

DecodedBuffer decodeMp3(const std::string& path);
DecodedBuffer decodeMp3Mem(const uint8_t* data, uint32_t length);

} // namespace systems::leal::campello_audio::pi
