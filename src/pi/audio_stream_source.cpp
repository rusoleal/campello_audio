/// @file audio_stream_source.cpp
/// @brief AudioStream — public streaming audio source implementation.

#include <campello_audio/audio_stream.hpp>
#include "source_handle.hpp"
#include "audio_stream.hpp"

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::AudioStreamHandle;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

AudioStream::AudioStream() : AudioSource() {
    native = new Handle();
}

AudioStream::~AudioStream() {
    // Close the stream before destroying the handle.
    auto* h = static_cast<Handle*>(native);
    if (h && h->data) h->data->close();
    delete h;
    native = nullptr;
}

// ---------------------------------------------------------------------------
// open / close / isOpen
// ---------------------------------------------------------------------------

bool AudioStream::open(const std::string& path) {
    auto* h = static_cast<Handle*>(native);
    if (!h) return false;

    // Create (or reuse) the AudioStreamData
    if (!h->data) {
        h->data = std::make_shared<pi::AudioStreamData>();
    } else {
        h->data->close();
    }

    const bool ok = h->data->open(path);
    if (!ok) h->data.reset();
    return ok;
}

void AudioStream::close() {
    auto* h = static_cast<Handle*>(native);
    if (h && h->data) {
        h->data->close();
        h->data.reset();
    }
}

bool AudioStream::isOpen() const {
    const auto* h = static_cast<const Handle*>(native);
    return h && h->data && h->data->isOpen;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

double AudioStream::getDuration() const {
    const auto* h = static_cast<const Handle*>(native);
    if (!h || !h->data || !h->data->isOpen || h->data->sampleRate == 0) return 0.0;
    if (h->data->totalFrames == 0) return 0.0;
    return static_cast<double>(h->data->totalFrames) /
           static_cast<double>(h->data->sampleRate);
}

uint32_t AudioStream::getSampleRate() const {
    const auto* h = static_cast<const Handle*>(native);
    return (h && h->data) ? h->data->sampleRate : 0u;
}

uint32_t AudioStream::getBufferMemoryBytes() const {
    const auto* h = static_cast<const Handle*>(native);
    return (h && h->data) ? h->data->getBufferMemoryBytes() : 0u;
}
