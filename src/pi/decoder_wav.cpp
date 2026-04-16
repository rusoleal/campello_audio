/// @file decoder_wav.cpp
/// @brief WAV decoder — uses dr_wav (single-header, public domain).
///
/// DR_WAV_IMPLEMENTATION must be defined in exactly ONE translation unit.

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "decoder.hpp"

namespace systems::leal::campello_audio::pi {

DecodedBuffer decodeWav(const std::string& path) {
    DecodedBuffer result;

    drwav_uint32  channels   = 0;
    drwav_uint32  sampleRate = 0;
    drwav_uint64  frameCount = 0;

    float* data = drwav_open_file_and_read_pcm_frames_f32(
        path.c_str(), &channels, &sampleRate, &frameCount, nullptr);

    if (!data) return result;

    result.channels   = channels;
    result.sampleRate = sampleRate;
    result.frameCount = frameCount;
    result.samples.assign(data, data + frameCount * channels);

    drwav_free(data, nullptr);
    return result;
}

DecodedBuffer decodeWavMem(const uint8_t* data, uint32_t length) {
    DecodedBuffer result;

    drwav_uint32 channels   = 0;
    drwav_uint32 sampleRate = 0;
    drwav_uint64 frameCount = 0;

    float* pcm = drwav_open_memory_and_read_pcm_frames_f32(
        data, length, &channels, &sampleRate, &frameCount, nullptr);

    if (!pcm) return result;

    result.channels   = channels;
    result.sampleRate = sampleRate;
    result.frameCount = frameCount;
    result.samples.assign(pcm, pcm + frameCount * channels);

    drwav_free(pcm, nullptr);
    return result;
}

} // namespace systems::leal::campello_audio::pi
