/// @file ogg_source.cpp
/// @brief OggSource — fully decoded OGG Vorbis stored in memory.

#include <campello_audio/ogg_source.hpp>
#include "source_handle.hpp"
#include "decoder.hpp"
#include <fstream>
#include <iterator>

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::OggSourceHandle;

OggSource::OggSource() : AudioSource() {
    native = new Handle();
}

OggSource::~OggSource() {
    delete static_cast<Handle*>(native);
    native = nullptr;
}

bool OggSource::load(ByteLoader loader) {
    if (!loader) return false;
    auto bytes = loader();
    if (bytes.empty()) return false;
    auto* h = static_cast<Handle*>(native);
    return h->decodeBytes(bytes);
}

bool OggSource::load(const std::string& path) {
    return load([&path]() -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return {std::istreambuf_iterator<char>(f), {}};
    });
}

bool OggSource::loadMem(const uint8_t* data, uint32_t length, bool /*copy*/) {
    return load([data, length]() -> std::vector<uint8_t> {
        return {data, data + length};
    });
}

double OggSource::getDuration() const {
    return static_cast<Handle*>(native)->buffer.getDuration();
}
