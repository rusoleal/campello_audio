#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>
#include <campello_audio/constants/loop_mode.hpp>
#include <campello_audio/constants/wave_form.hpp>
#include <campello_audio/descriptors/play_descriptor.hpp>
#include <campello_audio/filter.hpp>
#include <campello_audio/types/sound_handle.hpp>
#include "decoder.hpp"
#include "rtpc.hpp"

namespace systems::leal::campello_audio { class AudioSource; }

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// Source type tag — lets the engine dispatch without dynamic_cast or virtuals.
// ---------------------------------------------------------------------------
enum class SourceType { Unknown, Wav, Ogg, Mp3, Tone, Bus, Random, Music, Stream };

// ---------------------------------------------------------------------------
// AudioSourceHandle — base state common to every AudioSource subclass.
//
// Each concrete source allocates its own derived handle struct so a single
// allocation holds both the base state and the format-specific data.
// The base pointer `pcmBuffer` is set in derived constructors to point at the
// embedded DecodedBuffer field, so the engine only needs to cast to
// AudioSourceHandle* to obtain a valid PCM buffer pointer.
// ---------------------------------------------------------------------------
struct AudioSourceHandle {
    SourceType type           = SourceType::Unknown;
    float      volume         = 1.0f;
    bool       looping        = false;
    LoopMode   loopMode       = LoopMode::None;
    bool       singleInstance = false;

    std::array<std::shared_ptr<systems::leal::campello_audio::Filter>,
               systems::leal::campello_audio::Filter::kMaxFiltersPerSource> filters{};

    /// @brief Pointer to the decoded PCM buffer for this source.
    ///
    /// Non-null for Wav / Ogg / Mp3 sources — points at the embedded
    /// `DecodedBuffer` field inside the derived handle struct (address is stable
    /// because the handle is heap-allocated and never moved after construction).
    ///
    /// nullptr for ToneSource — samples are generated per voice at play time.
    const DecodedBuffer* pcmBuffer = nullptr;

    /// @brief Decode raw encoded bytes into this source's internal buffer.
    ///
    /// Set by derived handle constructors. Called by AudioEngine::tick() when
    /// an async load future resolves. Returns true on successful decode.
    std::function<bool(const std::vector<uint8_t>&)> decodeBytes;

    /// @brief RTPC bindings for this source — copied into Voice at play time.
    std::vector<ParameterBinding> bindings;
};

// ---------------------------------------------------------------------------
// Derived handles — base state + format-specific data.
// ---------------------------------------------------------------------------

struct WavSourceHandle : public AudioSourceHandle {
    DecodedBuffer buffer;
    WavSourceHandle() {
        type      = SourceType::Wav;
        pcmBuffer = &buffer;
        decodeBytes = [this](const std::vector<uint8_t>& bytes) -> bool {
            buffer = decodeWavMem(bytes.data(), static_cast<uint32_t>(bytes.size()));
            return buffer.isValid();
        };
    }
};

struct OggSourceHandle : public AudioSourceHandle {
    DecodedBuffer buffer;
    OggSourceHandle() {
        type      = SourceType::Ogg;
        pcmBuffer = &buffer;
        decodeBytes = [this](const std::vector<uint8_t>& bytes) -> bool {
            buffer = decodeOggMem(bytes.data(), static_cast<uint32_t>(bytes.size()));
            return buffer.isValid();
        };
    }
};

struct Mp3SourceHandle : public AudioSourceHandle {
    DecodedBuffer buffer;
    Mp3SourceHandle() {
        type      = SourceType::Mp3;
        pcmBuffer = &buffer;
        decodeBytes = [this](const std::vector<uint8_t>& bytes) -> bool {
            buffer = decodeMp3Mem(bytes.data(), static_cast<uint32_t>(bytes.size()));
            return buffer.isValid();
        };
    }
};

struct ToneSourceHandle : public AudioSourceHandle {
    WaveForm waveform  = WaveForm::Sine;
    float    frequency = 440.0f;
    // pcmBuffer stays nullptr — samples are generated into Voice::ownedBuffer at play time.
    ToneSourceHandle() { type = SourceType::Tone; }
};

// ---------------------------------------------------------------------------
// BusSourceHandle — backing data for AudioBus.
//
// Set up by AudioEngine::play(bus) with a busId and a playFn that routes
// child sources through the bus's mixer slot. The mixerData pointer enables
// AudioBus::setVolume() and getVisualizationData() to reach the BusSlot.
// ---------------------------------------------------------------------------
struct BusSourceHandle : public AudioSourceHandle {
    uint32_t busId     = 0;
    void*    mixerData = nullptr;   // MixerData* — raw to avoid circular include

    // Called by AudioBus::play() to route a child source through this bus.
    // Receives the source's internal handle directly (AudioBus is a friend of
    // AudioSource, so can extract native without going through the public API).
    using PlayFn = std::function<SoundHandle(const AudioSourceHandle*,
                                              const PlayDescriptor&,
                                              uint32_t busId)>;
    PlayFn playFn;

    BusSourceHandle() { type = SourceType::Bus; }
};

// ---------------------------------------------------------------------------
// VariantEntry — one weighted variant inside a RandomSource.
// ---------------------------------------------------------------------------
struct VariantEntry {
    std::shared_ptr<systems::leal::campello_audio::AudioSource> source;
    float weight = 1.0f;
};

// ---------------------------------------------------------------------------
// RandomSourceHandle — backing data for RandomSource.
//
// pickVariant() selects a weighted-random variant, respecting avoidRepeat,
// updates lastIndex, and returns the chosen index (or -1 if empty).
// ---------------------------------------------------------------------------
struct RandomSourceHandle : public AudioSourceHandle {
    std::vector<VariantEntry> variants;

    float   pitchVariation  = 0.0f;   ///< Max random pitch offset in semitones.
    float   volumeVariation = 0.0f;   ///< Max random volume offset in dB.
    bool    avoidRepeat     = true;   ///< If true, never repeat the last variant.
    int32_t lastIndex       = -1;     ///< Index of the variant chosen last time.

    std::mt19937 rng;

    RandomSourceHandle();

    /// @brief Select the next variant using weighted-random + avoid-repeat.
    /// @return Chosen index in [0, variants.size()), or -1 if empty.
    int32_t pickVariant();
};

// ---------------------------------------------------------------------------
// AudioStreamHandle — backing data for AudioStream.
//
// The stream's ring buffer and prefetch thread are managed by AudioStreamData.
// The engine detects SourceType::Stream and uses data directly.
// ---------------------------------------------------------------------------
struct AudioStreamData;  // defined in audio_stream.hpp

struct AudioStreamHandle : public AudioSourceHandle {
    std::shared_ptr<AudioStreamData> data;

    AudioStreamHandle() { type = SourceType::Stream; }
};

} // namespace systems::leal::campello_audio::pi
