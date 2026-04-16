/// @file mp3_source.cpp
/// @brief Mp3Source — fully decoded MP3 stored in memory.

#include <campello_audio/mp3_source.hpp>
#include "source_handle.hpp"
#include "decoder.hpp"
#include <fstream>
#include <iterator>

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::Mp3SourceHandle;

Mp3Source::Mp3Source() : AudioSource() {
    native = new Handle();
}

Mp3Source::~Mp3Source() {
    delete static_cast<Handle*>(native);
    native = nullptr;
}

bool Mp3Source::load(ByteLoader loader) {
    if (!loader) return false;
    auto bytes = loader();
    if (bytes.empty()) return false;
    auto* h = static_cast<Handle*>(native);
    return h->decodeBytes(bytes);
}

bool Mp3Source::load(const std::string& path) {
    return load([&path]() -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return {std::istreambuf_iterator<char>(f), {}};
    });
}

bool Mp3Source::loadMem(const uint8_t* data, uint32_t length, bool /*copy*/) {
    return load([data, length]() -> std::vector<uint8_t> {
        return {data, data + length};
    });
}

double Mp3Source::getDuration() const {
    return static_cast<Handle*>(native)->buffer.getDuration();
}
