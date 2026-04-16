#pragma once

/// @file filter_engine.hpp
/// @brief Internal filter DSP types and processing functions.
///
/// Filter::native  →  FilterData*   (params + automation, owned by Filter)
/// Voice::filterStates[i]  →  FilterState  (per-voice DSP history)

#include <cstdint>
#include <cstring>
#include <vector>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// FilterType
// ---------------------------------------------------------------------------
enum class FilterType {
    None,
    LowPass, HighPass,
    Echo,
    Reverb,
    Compressor, Limiter,
    Chorus, Flanger,
    PitchShift
};

// ---------------------------------------------------------------------------
// ParamAuto — one automatable parameter
// ---------------------------------------------------------------------------
struct ParamAuto {
    float  value      = 0.0f;
    float  fadeFrom   = 0.0f;  float  fadeTo    = 0.0f;
    double fadeTime   = 0.0;   double fadeElapsed = 0.0;
    bool   fadeActive = false;
    float  lfoFrom    = 0.0f;  float  lfoTo     = 0.0f;
    double lfoPeriod  = 1.0;   double lfoPhase  = 0.0;
    bool   lfoActive  = false;
};

// ---------------------------------------------------------------------------
// FilterData — stored as Filter::native
// ---------------------------------------------------------------------------
struct FilterData {
    FilterType type                      = FilterType::None;
    static constexpr uint32_t kMaxParams = 8;
    ParamAuto  params[kMaxParams]        = {};

    // Metering output (written from audio thread, read from game thread).
    // Benign data-race: acceptable for metering use cases.
    float gainReductionDb = 0.0f;
};

// ---------------------------------------------------------------------------
// FilterState — per-voice DSP state for one filter slot
// ---------------------------------------------------------------------------
struct FilterState {
    // ---- Bi-quad (LPF / HPF) ----
    float z1[2] = {0.0f, 0.0f};
    float z2[2] = {0.0f, 0.0f};

    // ---- Echo ----
    std::vector<float> echoBuffer;
    uint32_t           echoWritePos = 0;
    float              echoPrev[2]  = {0.0f, 0.0f};
    uint32_t           echoCapacity = 0;

    // ---- Reverb (Freeverb) ----
    // 8 comb + 4 all-pass, 2 channels = 24 delay lines.
    // All delay-line samples are concatenated in revBufs.
    std::vector<float>    revBufs;          // concatenated delay lines
    std::vector<uint32_t> revBufOffset;     // [24] start offset in revBufs
    std::vector<uint32_t> revPos;           // [24] current write pos per line
    std::vector<uint32_t> revLens;          // [24] capacity per line (frames)
    std::vector<float>    revFilt;          // [16] LP filter stores: 8 combs × 2 ch

    // ---- Dynamics (Compressor / Limiter) ----
    double             dynEnvDb        = 0.0;   // current gain envelope in dB
    std::vector<float> dynBuf;                   // lookahead delay ring buffer
    uint32_t           dynWritePos     = 0;
    uint32_t           dynCapacity     = 0;      // frames

    // ---- Modulation (Chorus / Flanger) ----
    std::vector<float> modBuf;
    uint32_t           modWritePos = 0;
    uint32_t           modCapacity = 0;          // frames
    double             modPhase    = 0.0;        // LFO phase [0..1]
    float              modFbk[2]   = {0.0f, 0.0f}; // flanger feedback state

    // ---- Pitch Shift (granular OLA) ----
    static constexpr uint32_t GRAIN_SIZE = 2048;
    std::vector<float> pitchBuf;
    uint32_t           pitchWritePos    = 0;
    uint32_t           pitchCapacity    = 0;     // frames
    double             pitchReadPos0    = 0.0;
    double             pitchReadPos1    = 0.0;
    double             pitchGrainPhase0 = 0.0;   // [0..1]
    double             pitchGrainPhase1 = 0.5;   // [0..1], offset by 0.5
};

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

/// Step param automation for @p bufferDuration seconds; return current value.
float stepParam(ParamAuto& p, double bufferDuration);

/// Pre-allocate per-voice DSP state (echo buffers, reverb delay lines, etc.).
/// Call from the game thread in play() before the audio thread first fires.
void initFilterState(FilterState& state, FilterType type,
                     uint32_t sampleRate, uint32_t channels);

/// Apply one filter to @p samples in place.
void processFilter(FilterData* data, FilterState& state,
                   float* samples, uint32_t frameCount,
                   uint32_t channels, uint32_t sampleRate,
                   double bufferDuration);

} // namespace systems::leal::campello_audio::pi
