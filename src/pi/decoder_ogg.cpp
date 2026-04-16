/// @file decoder_ogg.cpp
/// @brief OGG Vorbis decoder — uses stb_vorbis (single-file, public domain).
///
/// stb_vorbis.c is included directly (it serves as its own implementation
/// unit). Including it here defines all stb_vorbis symbols in this TU.

// Suppress stb_vorbis warnings on MSVC and GCC/Clang
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#include "stb_vorbis.c"

#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include "decoder.hpp"
#include <vector>

namespace systems::leal::campello_audio::pi {

DecodedBuffer decodeOgg(const std::string& path) {
    DecodedBuffer result;

    int      channels   = 0;
    int      sampleRate = 0;
    short*   pcmShort   = nullptr;

    // Returns total sample count (frames × channels), or -1 on error.
    int totalSamples = stb_vorbis_decode_filename(
        path.c_str(), &channels, &sampleRate, &pcmShort);

    if (totalSamples < 0 || !pcmShort) return result;

    result.channels   = static_cast<uint32_t>(channels);
    result.sampleRate = static_cast<uint32_t>(sampleRate);
    result.frameCount = static_cast<uint64_t>(totalSamples) / static_cast<uint64_t>(channels);

    // Convert short [-32768, 32767] → float [-1, 1]
    result.samples.resize(static_cast<size_t>(totalSamples));
    for (int i = 0; i < totalSamples; ++i)
        result.samples[i] = static_cast<float>(pcmShort[i]) / 32768.0f;

    free(pcmShort);
    return result;
}

DecodedBuffer decodeOggMem(const uint8_t* data, uint32_t length) {
    DecodedBuffer result;

    int    channels   = 0;
    int    sampleRate = 0;
    short* pcmShort   = nullptr;

    int totalSamples = stb_vorbis_decode_memory(
        data, static_cast<int>(length), &channels, &sampleRate, &pcmShort);

    if (totalSamples < 0 || !pcmShort) return result;

    result.channels   = static_cast<uint32_t>(channels);
    result.sampleRate = static_cast<uint32_t>(sampleRate);
    result.frameCount = static_cast<uint64_t>(totalSamples) / static_cast<uint64_t>(channels);

    result.samples.resize(static_cast<size_t>(totalSamples));
    for (int i = 0; i < totalSamples; ++i)
        result.samples[i] = static_cast<float>(pcmShort[i]) / 32768.0f;

    free(pcmShort);
    return result;
}

} // namespace systems::leal::campello_audio::pi
