/// @file decoder_mp3.cpp
/// @brief MP3 decoder — uses dr_mp3 (single-header, public domain).
///
/// DR_MP3_IMPLEMENTATION must be defined in exactly ONE translation unit.

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include "decoder.hpp"

namespace systems::leal::campello_audio::pi {

DecodedBuffer decodeMp3(const std::string& path) {
    DecodedBuffer result;

    drmp3_config  cfg        = {};
    drmp3_uint64  frameCount = 0;

    float* data = drmp3_open_file_and_read_pcm_frames_f32(
        path.c_str(), &cfg, &frameCount, nullptr);

    if (!data) return result;

    result.channels   = cfg.channels;
    result.sampleRate = cfg.sampleRate;
    result.frameCount = frameCount;
    result.samples.assign(data, data + frameCount * cfg.channels);

    drmp3_free(data, nullptr);
    return result;
}

DecodedBuffer decodeMp3Mem(const uint8_t* data, uint32_t length) {
    DecodedBuffer result;

    drmp3_config cfg        = {};
    drmp3_uint64 frameCount = 0;

    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(
        data, length, &cfg, &frameCount, nullptr);

    if (!pcm) return result;

    result.channels   = cfg.channels;
    result.sampleRate = cfg.sampleRate;
    result.frameCount = frameCount;
    result.samples.assign(pcm, pcm + frameCount * cfg.channels);

    drmp3_free(pcm, nullptr);
    return result;
}

} // namespace systems::leal::campello_audio::pi
