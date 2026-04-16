/// @file wav_source.cpp
/// @brief WavSource — fully decoded WAV stored in memory.

#include <campello_audio/wav_source.hpp>
#include "source_handle.hpp"
#include "decoder.hpp"
#include <fstream>
#include <iterator>

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::WavSourceHandle;

WavSource::WavSource() : AudioSource() {
    native = new Handle();
}

WavSource::~WavSource() {
    delete static_cast<Handle*>(native);
    native = nullptr;
}

bool WavSource::load(ByteLoader loader) {
    if (!loader) return false;
    auto bytes = loader();
    if (bytes.empty()) return false;
    auto* h = static_cast<Handle*>(native);
    return h->decodeBytes(bytes);
}

bool WavSource::load(const std::string& path) {
    return load([&path]() -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return {std::istreambuf_iterator<char>(f), {}};
    });
}

bool WavSource::loadMem(const uint8_t* data, uint32_t length, bool /*copy*/) {
    return load([data, length]() -> std::vector<uint8_t> {
        return {data, data + length};
    });
}

uint32_t WavSource::getSampleRate() const {
    return static_cast<Handle*>(native)->buffer.sampleRate;
}

double WavSource::getDuration() const {
    return static_cast<Handle*>(native)->buffer.getDuration();
}
