/// @file tone_source.cpp
/// @brief ToneSource — procedural waveform generator.
///
/// Samples are generated once per play() call (one waveform period stored in
/// Voice::ownedBuffer and looped) rather than per-callback streaming.
/// Dynamic frequency changes after play() are not reflected until the next
/// play() call; real-time per-callback generation is deferred to a later phase.

#include <campello_audio/tone_source.hpp>
#include "source_handle.hpp"
#include <cmath>
#include <cstdlib>
#include <numbers>

using namespace systems::leal::campello_audio;
using Handle = systems::leal::campello_audio::pi::ToneSourceHandle;

ToneSource::ToneSource(WaveForm waveform, float frequency)
    : AudioSource() {
    auto* h     = new Handle();
    h->waveform  = waveform;
    h->frequency = frequency;
    native = h;
}

ToneSource::~ToneSource() {
    delete static_cast<Handle*>(native);
    native = nullptr;
}

void ToneSource::setFrequency(float hz) {
    static_cast<Handle*>(native)->frequency = hz;
}

float ToneSource::getFrequency() const {
    return static_cast<Handle*>(native)->frequency;
}

void ToneSource::setWaveForm(WaveForm waveform) {
    static_cast<Handle*>(native)->waveform = waveform;
}

WaveForm ToneSource::getWaveForm() const {
    return static_cast<Handle*>(native)->waveform;
}

// ---------------------------------------------------------------------------
// Tone buffer generation — called by AudioEngine::play() for each new voice.
// ---------------------------------------------------------------------------

namespace systems::leal::campello_audio::pi {

/// @brief Generate one period of a waveform into a loopable DecodedBuffer.
///
/// For Noise, a 100ms buffer is generated so the loop point is not obviously
/// audible.  All other waveforms use exactly one period.
///
/// @param waveform   Shape of the waveform.
/// @param frequency  Oscillator frequency in Hz (> 0).
/// @param sampleRate Engine output sample rate.
/// @param channels   Output channel count (samples are identical across channels).
DecodedBuffer generateToneBuffer(WaveForm  waveform,
                                  float     frequency,
                                  uint32_t  sampleRate,
                                  uint32_t  channels) {
    const float  safeFreq = std::max(frequency, 0.1f);
    const double period   = 1.0 / static_cast<double>(safeFreq);

    // For Noise use 100ms to break up the repeating pattern.
    const uint32_t frameCount = (waveform == WaveForm::Noise)
        ? (sampleRate / 10)
        : std::max(1u, static_cast<uint32_t>(std::round(period * sampleRate)));

    DecodedBuffer buf;
    buf.sampleRate = sampleRate;
    buf.channels   = channels;
    buf.frameCount = frameCount;
    buf.samples.resize(frameCount * channels);

    for (uint32_t i = 0; i < frameCount; ++i) {
        const double phase = static_cast<double>(i) / static_cast<double>(frameCount);
        float sample = 0.0f;

        switch (waveform) {
        case WaveForm::Sine:
            sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
            break;
        case WaveForm::Square:
            sample = (phase < 0.5) ? 1.0f : -1.0f;
            break;
        case WaveForm::Saw:
            sample = static_cast<float>(2.0 * phase - 1.0);
            break;
        case WaveForm::Triangle:
            sample = static_cast<float>((phase < 0.5) ? (4.0 * phase - 1.0)
                                                       : (3.0 - 4.0 * phase));
            break;
        case WaveForm::Noise:
            sample = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
            break;
        }

        for (uint32_t c = 0; c < channels; ++c) {
            buf.samples[i * channels + c] = sample;
        }
    }

    return buf;
}

} // namespace systems::leal::campello_audio::pi
