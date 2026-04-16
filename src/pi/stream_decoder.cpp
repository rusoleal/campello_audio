/// @file stream_decoder.cpp
/// @brief Format-agnostic streaming decoder wrapping dr_wav, stb_vorbis, dr_mp3.
///
/// The implementation macros (DR_WAV_IMPLEMENTATION, DR_MP3_IMPLEMENTATION,
/// stb_vorbis full implementation) are already defined in decoder_wav.cpp,
/// decoder_mp3.cpp, and decoder_ogg.cpp respectively. This TU only uses the
/// header declarations, relying on the linker to resolve symbols.

// --- dr_wav header only (no implementation macro here) ---
#include "dr_wav.h"

// --- stb_vorbis header only ---
#define STB_VORBIS_HEADER_ONLY
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_vorbis.c"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

// --- dr_mp3 header only (no implementation macro here) ---
#include "dr_mp3.h"

#include "audio_stream.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

StreamDecoder* streamDecoderOpen(const std::string& path) {
    // Detect format by file extension (case-insensitive suffix match)
    const auto ext = [&]() -> std::string {
        const auto pos = path.rfind('.');
        if (pos == std::string::npos) return {};
        std::string e = path.substr(pos + 1);
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e;
    }();

    auto* d = new StreamDecoder{};

    if (ext == "wav") {
        d->format = StreamDecoder::Format::WAV;
        auto* wav = new drwav{};
        if (!drwav_init_file(wav, path.c_str(), nullptr)) {
            delete wav;
            delete d;
            return nullptr;
        }
        d->handle      = wav;
        d->channels    = wav->channels;
        d->sampleRate  = wav->sampleRate;
        d->totalFrames = wav->totalPCMFrameCount;

    } else if (ext == "ogg" || ext == "oga") {
        d->format = StreamDecoder::Format::OGG;
        int err = 0;
        auto* v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
        if (!v) {
            delete d;
            return nullptr;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(v);
        d->handle      = v;
        d->channels    = static_cast<uint32_t>(info.channels);
        d->sampleRate  = static_cast<uint32_t>(info.sample_rate);
        d->totalFrames = static_cast<uint64_t>(stb_vorbis_stream_length_in_samples(v));

    } else if (ext == "mp3") {
        d->format = StreamDecoder::Format::MP3;
        auto* mp3 = new drmp3{};
        if (!drmp3_init_file(mp3, path.c_str(), nullptr)) {
            delete mp3;
            delete d;
            return nullptr;
        }
        d->handle      = mp3;
        d->channels    = mp3->channels;
        d->sampleRate  = mp3->sampleRate;
        d->totalFrames = drmp3_get_pcm_frame_count(mp3);

    } else {
        // Unsupported extension
        delete d;
        return nullptr;
    }

    return d;
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

uint32_t streamDecoderRead(StreamDecoder* d, float* out, uint32_t frames) {
    if (!d || !d->handle || frames == 0) return 0;

    switch (d->format) {
    case StreamDecoder::Format::WAV: {
        auto* wav = static_cast<drwav*>(d->handle);
        const drwav_uint64 got = drwav_read_pcm_frames_f32(wav, frames, out);
        return static_cast<uint32_t>(got);
    }
    case StreamDecoder::Format::OGG: {
        auto* v  = static_cast<stb_vorbis*>(d->handle);
        const int got = stb_vorbis_get_samples_float_interleaved(
            v, static_cast<int>(d->channels),
            out, static_cast<int>(frames * d->channels));
        return static_cast<uint32_t>(std::max(got, 0));
    }
    case StreamDecoder::Format::MP3: {
        auto* mp3 = static_cast<drmp3*>(d->handle);
        const drmp3_uint64 got = drmp3_read_pcm_frames_f32(mp3, frames, out);
        return static_cast<uint32_t>(got);
    }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Seek
// ---------------------------------------------------------------------------

bool streamDecoderSeek(StreamDecoder* d, uint64_t frame) {
    if (!d || !d->handle) return false;

    switch (d->format) {
    case StreamDecoder::Format::WAV:
        return drwav_seek_to_pcm_frame(static_cast<drwav*>(d->handle), frame) == DRWAV_TRUE;
    case StreamDecoder::Format::OGG:
        stb_vorbis_seek(static_cast<stb_vorbis*>(d->handle),
                        static_cast<unsigned int>(frame));
        return true;
    case StreamDecoder::Format::MP3:
        return drmp3_seek_to_pcm_frame(static_cast<drmp3*>(d->handle), frame) == DRMP3_TRUE;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

void streamDecoderClose(StreamDecoder* d) {
    if (!d) return;

    if (d->handle) {
        switch (d->format) {
        case StreamDecoder::Format::WAV:
            drwav_uninit(static_cast<drwav*>(d->handle));
            delete static_cast<drwav*>(d->handle);
            break;
        case StreamDecoder::Format::OGG:
            stb_vorbis_close(static_cast<stb_vorbis*>(d->handle));
            break;
        case StreamDecoder::Format::MP3:
            drmp3_uninit(static_cast<drmp3*>(d->handle));
            delete static_cast<drmp3*>(d->handle);
            break;
        }
        d->handle = nullptr;
    }
    delete d;
}

} // namespace systems::leal::campello_audio::pi
