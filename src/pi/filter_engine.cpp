/// @file filter_engine.cpp
/// @brief Full filter DSP: bi-quad (LPF/HPF), echo, Freeverb reverb,
///        compressor, limiter, chorus, flanger, and granular-OLA pitch shift.
///
/// Bi-quad:    Audio EQ Cookbook (Direct Form II Transposed).
/// Reverb:     Freeverb — 8 LP-feedback comb + 4 all-pass filters, stereo.
/// Compressor: Feed-forward peak detector, soft-knee gain computer,
///             attack/release envelope follower.
/// Limiter:    Brickwall with lookahead ring buffer, instant attack.
/// Chorus:     3-voice LFO at 0°/120°/240°, max 25 ms delay.
/// Flanger:    Single LFO-modulated delay [0..10 ms] with feedback.
/// PitchShift: Two-head granular OLA (GRAIN_SIZE = 2048, 50% overlap,
///             Hann window).

#include "filter_engine.hpp"
#include <cmath>
#include <algorithm>
#include <numbers>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

// Freeverb tuning at 44100 Hz — scaled proportionally at init time.
static constexpr uint32_t kFvCombLens[8]  = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static constexpr uint32_t kFvApLens[4]    = {556, 441, 341, 225};
static constexpr uint32_t kFvStereoSpread = 23;
static constexpr float    kFvFixedGain    = 0.015f;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

struct BiquadCoeffs { float b0, b1, b2, a1, a2; };

static BiquadCoeffs computeLPF(float cutoffHz, float Q, uint32_t sr) {
    const double w0 = kTwoPi * cutoffHz / sr;
    const float  cw = static_cast<float>(std::cos(w0));
    const float  sw = static_cast<float>(std::sin(w0));
    const float  al = sw / (2.0f * Q);
    const float  a0 = 1.0f / (1.0f + al);
    return { (1.0f - cw) * 0.5f * a0,
             (1.0f - cw)        * a0,
             (1.0f - cw) * 0.5f * a0,
            -2.0f * cw          * a0,
             (1.0f - al)        * a0 };
}

static BiquadCoeffs computeHPF(float cutoffHz, float Q, uint32_t sr) {
    const double w0 = kTwoPi * cutoffHz / sr;
    const float  cw = static_cast<float>(std::cos(w0));
    const float  sw = static_cast<float>(std::sin(w0));
    const float  al = sw / (2.0f * Q);
    const float  a0 = 1.0f / (1.0f + al);
    return {  (1.0f + cw) * 0.5f * a0,
             -(1.0f + cw)        * a0,
              (1.0f + cw) * 0.5f * a0,
             -2.0f * cw          * a0,
              (1.0f - al)        * a0 };
}

/// Linear interpolation from a circular interleaved buffer.
/// @param readPos  Fractional frame index into the buffer.
static float readCircular(const std::vector<float>& buf, uint32_t cap,
                           double readPos, uint32_t ch, uint32_t c) {
    readPos = std::fmod(readPos, static_cast<double>(cap));
    if (readPos < 0.0) readPos += cap;
    const uint32_t i0   = static_cast<uint32_t>(readPos) % cap;
    const uint32_t i1   = (i0 + 1) % cap;
    const float    frac = static_cast<float>(readPos - std::floor(readPos));
    return buf[i0 * ch + c] + frac * (buf[i1 * ch + c] - buf[i0 * ch + c]);
}

// ---------------------------------------------------------------------------
// stepParam — advance automation, return current value
// ---------------------------------------------------------------------------

float stepParam(ParamAuto& p, double bufferDuration) {
    if (p.fadeActive) {
        p.fadeElapsed += bufferDuration;
        const double t = std::min(p.fadeElapsed / p.fadeTime, 1.0);
        p.value = p.fadeFrom + (p.fadeTo - p.fadeFrom) * static_cast<float>(t);
        if (p.fadeElapsed >= p.fadeTime) {
            p.value      = p.fadeTo;
            p.fadeActive = false;
        }
    } else if (p.lfoActive) {
        p.lfoPhase += bufferDuration / p.lfoPeriod;
        if (p.lfoPhase >= 1.0) p.lfoPhase -= 1.0;
        const float mid = (p.lfoFrom + p.lfoTo) * 0.5f;
        const float amp = (p.lfoTo   - p.lfoFrom) * 0.5f;
        p.value = mid + amp * static_cast<float>(std::sin(kTwoPi * p.lfoPhase));
    }
    return p.value;
}

// ---------------------------------------------------------------------------
// initFilterState — pre-allocate per-voice DSP buffers
// ---------------------------------------------------------------------------

void initFilterState(FilterState& state, FilterType type,
                     uint32_t sampleRate, uint32_t channels) {
    const uint32_t ch = std::min(channels, 2u);

    state.z1[0] = state.z1[1] = 0.0f;
    state.z2[0] = state.z2[1] = 0.0f;

    switch (type) {

    case FilterType::Echo: {
        const uint32_t cap = 2 * sampleRate;
        state.echoCapacity = cap;
        state.echoBuffer.assign(static_cast<size_t>(cap) * ch, 0.0f);
        state.echoWritePos   = 0;
        state.echoPrev[0]    = state.echoPrev[1] = 0.0f;
        break;
    }

    case FilterType::Reverb: {
        // 24 single-channel delay lines:
        //   [0..7]   8 comb L
        //   [8..15]  8 comb R  (length + stereoSpread)
        //   [16..19] 4 all-pass L
        //   [20..23] 4 all-pass R (length + stereoSpread)
        state.revBufOffset.resize(24);
        state.revPos.resize(24, 0u);
        state.revLens.resize(24);
        state.revFilt.assign(16, 0.0f);   // LP filter store: 8 combs × 2 ch

        const float scale = static_cast<float>(sampleRate) / 44100.0f;
        uint32_t total = 0u;

        for (int i = 0; i < 8; ++i) {
            state.revBufOffset[i] = total;
            state.revLens[i]      = std::max(1u, static_cast<uint32_t>(kFvCombLens[i] * scale));
            total += state.revLens[i];
        }
        for (int i = 0; i < 8; ++i) {
            state.revBufOffset[8 + i] = total;
            state.revLens[8 + i]      = std::max(1u, static_cast<uint32_t>((kFvCombLens[i] + kFvStereoSpread) * scale));
            total += state.revLens[8 + i];
        }
        for (int i = 0; i < 4; ++i) {
            state.revBufOffset[16 + i] = total;
            state.revLens[16 + i]      = std::max(1u, static_cast<uint32_t>(kFvApLens[i] * scale));
            total += state.revLens[16 + i];
        }
        for (int i = 0; i < 4; ++i) {
            state.revBufOffset[20 + i] = total;
            state.revLens[20 + i]      = std::max(1u, static_cast<uint32_t>((kFvApLens[i] + kFvStereoSpread) * scale));
            total += state.revLens[20 + i];
        }
        state.revBufs.assign(total, 0.0f);
        break;
    }

    case FilterType::Compressor: {
        state.dynEnvDb    = 0.0;
        state.dynCapacity = 0;
        break;
    }

    case FilterType::Limiter: {
        // Pre-allocate for maximum lookahead (20 ms).
        const uint32_t cap = static_cast<uint32_t>(0.020 * sampleRate) + 1;
        state.dynEnvDb    = 0.0;
        state.dynCapacity = cap;
        state.dynWritePos = 0;
        state.dynBuf.assign(static_cast<size_t>(cap) * ch, 0.0f);
        break;
    }

    case FilterType::Chorus:
    case FilterType::Flanger: {
        // Max delay: 25 ms chorus, 10 ms flanger.
        const float    maxSecs = (type == FilterType::Chorus) ? 0.025f : 0.010f;
        const uint32_t cap     = static_cast<uint32_t>(maxSecs * sampleRate) + 2;
        state.modCapacity = cap;
        state.modWritePos = 0;
        state.modPhase    = 0.0;
        state.modFbk[0]   = state.modFbk[1] = 0.0f;
        state.modBuf.assign(static_cast<size_t>(cap) * ch, 0.0f);
        break;
    }

    case FilterType::PitchShift: {
        const uint32_t cap      = 2 * FilterState::GRAIN_SIZE;
        state.pitchCapacity     = cap;
        state.pitchWritePos     = 0;
        state.pitchReadPos0     = 0.0;
        state.pitchReadPos1     = static_cast<double>(FilterState::GRAIN_SIZE / 2);
        state.pitchGrainPhase0  = 0.0;
        state.pitchGrainPhase1  = 0.5;
        state.pitchBuf.assign(static_cast<size_t>(cap) * ch, 0.0f);
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// processFilter — apply one filter in-place
// ---------------------------------------------------------------------------

void processFilter(FilterData* data, FilterState& state,
                   float* samples, uint32_t frameCount,
                   uint32_t channels, uint32_t sampleRate,
                   double bufferDuration) {
    if (!data || data->type == FilterType::None) return;

    const uint32_t ch = std::min(channels, 2u);

    switch (data->type) {

    // -----------------------------------------------------------------------
    // Bi-quad IIR — LPF / HPF
    // -----------------------------------------------------------------------
    case FilterType::LowPass:
    case FilterType::HighPass: {
        const float cutoff    = std::clamp(stepParam(data->params[0], bufferDuration),
                                           10.0f, static_cast<float>(sampleRate) * 0.49f);
        const float resonance = std::clamp(stepParam(data->params[1], bufferDuration),
                                           0.01f, 40.0f);
        const BiquadCoeffs c  = (data->type == FilterType::LowPass)
                                 ? computeLPF(cutoff, resonance, sampleRate)
                                 : computeHPF(cutoff, resonance, sampleRate);

        for (uint32_t f = 0; f < frameCount; ++f) {
            for (uint32_t c2 = 0; c2 < ch; ++c2) {
                float& x      = samples[f * ch + c2];
                const float y = c.b0 * x + state.z1[c2];
                state.z1[c2]  = c.b1 * x - c.a1 * y + state.z2[c2];
                state.z2[c2]  = c.b2 * x - c.a2 * y;
                x = y;
            }
        }
        break;
    }

    // -----------------------------------------------------------------------
    // Echo — circular delay with decay and LP smoothing on feedback
    // -----------------------------------------------------------------------
    case FilterType::Echo: {
        const float delaySecs = std::clamp(stepParam(data->params[0], bufferDuration),
                                           0.001f, 2.0f);
        const float decay     = std::clamp(stepParam(data->params[1], bufferDuration),
                                           0.0f, 0.99f);
        const float filterVal = std::clamp(stepParam(data->params[2], bufferDuration),
                                           0.0f, 1.0f);

        if (state.echoBuffer.empty())
            initFilterState(state, FilterType::Echo, sampleRate, ch);

        const uint32_t delayFrames = static_cast<uint32_t>(delaySecs * sampleRate);
        const uint32_t cap         = state.echoCapacity;
        if (delayFrames == 0 || cap == 0) break;

        for (uint32_t f = 0; f < frameCount; ++f) {
            const uint32_t readPos =
                (state.echoWritePos + cap - std::min(delayFrames, cap)) % cap;

            for (uint32_t c2 = 0; c2 < ch; ++c2) {
                const float delayed  = state.echoBuffer[readPos * ch + c2];
                const float smoothed = delayed + filterVal * (state.echoPrev[c2] - delayed);
                state.echoPrev[c2]   = smoothed;
                state.echoBuffer[state.echoWritePos * ch + c2] =
                    samples[f * ch + c2] + smoothed * decay;
                samples[f * ch + c2] += delayed;
            }
            state.echoWritePos = (state.echoWritePos + 1) % cap;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // Reverb — Freeverb (8 LP-feedback comb + 4 all-pass filters, stereo)
    // -----------------------------------------------------------------------
    case FilterType::Reverb: {
        if (state.revBufs.empty())
            initFilterState(state, FilterType::Reverb, sampleRate, ch);

        const float roomSize = std::clamp(stepParam(data->params[0], bufferDuration), 0.0f, 1.0f);
        const float damping  = std::clamp(stepParam(data->params[1], bufferDuration), 0.0f, 1.0f);
        const float wet      = std::clamp(stepParam(data->params[2], bufferDuration), 0.0f, 1.0f);
        const float dry      = std::clamp(stepParam(data->params[3], bufferDuration), 0.0f, 1.0f);
        const float width    = std::clamp(stepParam(data->params[4], bufferDuration), 0.0f, 1.0f);

        // Freeverb parameter scaling
        const float scaledRoom = roomSize * 0.28f + 0.70f;
        const float scaledDamp = damping  * 0.40f;
        const float wet1       = wet * (width * 0.5f + 0.5f);
        const float wet2       = wet * ((1.0f - width) * 0.5f);

        for (uint32_t f = 0; f < frameCount; ++f) {
            const float inL = samples[f * ch];
            const float inR = (ch > 1) ? samples[f * ch + 1] : inL;

            // Mono feed into all comb filters
            const float input = (inL + inR) * kFvFixedGain;

            float outL = 0.0f, outR = 0.0f;

            // 8 LP-feedback comb filters: L (lines 0..7) and R (lines 8..15)
            for (int i = 0; i < 8; ++i) {
                // Left
                {
                    const uint32_t off = state.revBufOffset[i];
                    const uint32_t len = state.revLens[i];
                    const uint32_t pos = state.revPos[i];
                    const float    buf = state.revBufs[off + pos];
                    float& flt         = state.revFilt[i];
                    flt = buf * (1.0f - scaledDamp) + flt * scaledDamp;
                    state.revBufs[off + pos] = input + flt * scaledRoom;
                    state.revPos[i] = (pos + 1) % len;
                    outL += buf;
                }
                // Right
                {
                    const uint32_t off = state.revBufOffset[8 + i];
                    const uint32_t len = state.revLens[8 + i];
                    const uint32_t pos = state.revPos[8 + i];
                    const float    buf = state.revBufs[off + pos];
                    float& flt         = state.revFilt[8 + i];
                    flt = buf * (1.0f - scaledDamp) + flt * scaledDamp;
                    state.revBufs[off + pos] = input + flt * scaledRoom;
                    state.revPos[8 + i] = (pos + 1) % len;
                    outR += buf;
                }
            }

            // 4 all-pass filters: L (lines 16..19) and R (lines 20..23)
            for (int i = 0; i < 4; ++i) {
                // Left
                {
                    const uint32_t off    = state.revBufOffset[16 + i];
                    const uint32_t len    = state.revLens[16 + i];
                    const uint32_t pos    = state.revPos[16 + i];
                    const float    bufOut = state.revBufs[off + pos];
                    state.revBufs[off + pos] = outL + bufOut * 0.5f;
                    state.revPos[16 + i] = (pos + 1) % len;
                    outL = bufOut - outL;
                }
                // Right
                {
                    const uint32_t off    = state.revBufOffset[20 + i];
                    const uint32_t len    = state.revLens[20 + i];
                    const uint32_t pos    = state.revPos[20 + i];
                    const float    bufOut = state.revBufs[off + pos];
                    state.revBufs[off + pos] = outR + bufOut * 0.5f;
                    state.revPos[20 + i] = (pos + 1) % len;
                    outR = bufOut - outR;
                }
            }

            // Wet/dry mix with stereo width
            if (ch > 1) {
                samples[f * ch]     = outL * wet1 + outR * wet2 + inL * dry;
                samples[f * ch + 1] = outR * wet1 + outL * wet2 + inR * dry;
            } else {
                samples[f] = (outL + outR) * 0.5f * wet + inL * dry;
            }
        }
        break;
    }

    // -----------------------------------------------------------------------
    // Compressor — feed-forward peak compressor with soft knee
    // -----------------------------------------------------------------------
    case FilterType::Compressor: {
        const float threshDb  = std::clamp(stepParam(data->params[0], bufferDuration), -60.0f,   0.0f);
        const float ratio     = std::max (stepParam(data->params[1], bufferDuration),   1.0f);
        const float attackMs  = std::clamp(stepParam(data->params[2], bufferDuration),  0.1f,  500.0f);
        const float releaseMs = std::clamp(stepParam(data->params[3], bufferDuration),  1.0f, 5000.0f);
        const float kneeDb    = std::clamp(stepParam(data->params[4], bufferDuration),  0.0f,   40.0f);
        const float makeupDb  = std::clamp(stepParam(data->params[5], bufferDuration),  0.0f,   40.0f);

        const float sr         = static_cast<float>(sampleRate);
        const float attackCoef = std::exp(-1.0f / (attackMs  * 0.001f * sr));
        const float relCoef    = std::exp(-1.0f / (releaseMs * 0.001f * sr));
        const float halfKnee   = kneeDb * 0.5f;

        for (uint32_t f = 0; f < frameCount; ++f) {
            float peak = 0.0f;
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                peak = std::max(peak, std::abs(samples[f * ch + c2]));

            const float xDb = (peak > 1e-6f)
                               ? 20.0f * static_cast<float>(std::log10(peak))
                               : -120.0f;

            // Gain computer with soft knee
            float gainDb;
            if (kneeDb > 0.0f && xDb > threshDb - halfKnee && xDb < threshDb + halfKnee) {
                const float x = xDb - threshDb + halfKnee;
                gainDb = (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
            } else if (xDb >= threshDb + halfKnee) {
                gainDb = (1.0f / ratio - 1.0f) * (xDb - threshDb);
            } else {
                gainDb = 0.0f;
            }

            // Envelope follower: attack when more reduction needed, release otherwise
            if (gainDb < static_cast<float>(state.dynEnvDb)) {
                state.dynEnvDb = attackCoef * state.dynEnvDb + (1.0 - attackCoef) * gainDb;
            } else {
                state.dynEnvDb = relCoef * state.dynEnvDb + (1.0 - relCoef) * gainDb;
            }

            const float gain = std::pow(10.0f,
                static_cast<float>(state.dynEnvDb + makeupDb) / 20.0f);
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                samples[f * ch + c2] *= gain;
        }

        data->gainReductionDb = static_cast<float>(-state.dynEnvDb);
        break;
    }

    // -----------------------------------------------------------------------
    // Limiter — brickwall with lookahead delay, instant attack, smooth release
    // -----------------------------------------------------------------------
    case FilterType::Limiter: {
        if (state.dynBuf.empty())
            initFilterState(state, FilterType::Limiter, sampleRate, ch);

        const float ceilingDb   = std::clamp(stepParam(data->params[0], bufferDuration), -12.0f,    0.0f);
        const float lookaheadMs = std::clamp(stepParam(data->params[1], bufferDuration),   0.0f,   20.0f);
        const float releaseMs   = std::clamp(stepParam(data->params[2], bufferDuration),   1.0f, 1000.0f);

        const uint32_t cap             = state.dynCapacity;
        const uint32_t lookaheadFrames = static_cast<uint32_t>(lookaheadMs * 0.001f * sampleRate);
        if (cap == 0) break;

        const float ceiling = std::pow(10.0f, ceilingDb / 20.0f);
        const float relCoef = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));

        for (uint32_t f = 0; f < frameCount; ++f) {
            // Write current input into lookahead delay
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                state.dynBuf[state.dynWritePos * ch + c2] = samples[f * ch + c2];

            // Read delayed output
            const uint32_t readPos =
                (state.dynWritePos + cap - std::min(lookaheadFrames, cap)) % cap;

            // Peak of *current* (non-delayed) input for gain computation
            float peak = 0.0f;
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                peak = std::max(peak, std::abs(samples[f * ch + c2]));

            const float targetGainDb = (peak > ceiling && peak > 1e-6f)
                ? ceilingDb - 20.0f * static_cast<float>(std::log10(peak))
                : 0.0f;

            // Instant attack, smooth release
            if (targetGainDb < static_cast<float>(state.dynEnvDb)) {
                state.dynEnvDb = targetGainDb;
            } else {
                state.dynEnvDb = relCoef * state.dynEnvDb + (1.0 - relCoef) * targetGainDb;
            }

            const float gain = std::pow(10.0f, static_cast<float>(state.dynEnvDb) / 20.0f);
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                samples[f * ch + c2] = state.dynBuf[readPos * ch + c2] * gain;

            state.dynWritePos = (state.dynWritePos + 1) % cap;
        }

        data->gainReductionDb = static_cast<float>(-state.dynEnvDb);
        break;
    }

    // -----------------------------------------------------------------------
    // Chorus — 3 voices at 0°/120°/240°, max 25 ms delay, per-sample LFO
    // -----------------------------------------------------------------------
    case FilterType::Chorus: {
        if (state.modBuf.empty())
            initFilterState(state, FilterType::Chorus, sampleRate, ch);

        const float depth  = std::clamp(stepParam(data->params[0], bufferDuration), 0.0f,  1.0f);
        const float rateHz = std::clamp(stepParam(data->params[1], bufferDuration), 0.01f, 10.0f);
        const float wet    = std::clamp(stepParam(data->params[2], bufferDuration), 0.0f,  1.0f);
        const float dry    = std::clamp(stepParam(data->params[3], bufferDuration), 0.0f,  1.0f);

        static constexpr float kMaxDelaySecs = 0.025f;
        static constexpr int   kVoices       = 3;
        const uint32_t cap   = state.modCapacity;
        const float    phInc = static_cast<float>(rateHz / sampleRate);
        if (cap == 0) break;

        for (uint32_t f = 0; f < frameCount; ++f) {
            // Cache input, write to delay buffer
            float inSamp[2];
            for (uint32_t c2 = 0; c2 < ch; ++c2) {
                inSamp[c2] = samples[f * ch + c2];
                state.modBuf[state.modWritePos * ch + c2] = inSamp[c2];
            }

            float accum[2] = {0.0f, 0.0f};

            for (int v = 0; v < kVoices; ++v) {
                const double phaseV  = std::fmod(state.modPhase + static_cast<double>(v) / kVoices, 1.0);
                const float  lfoVal  = static_cast<float>(std::sin(kTwoPi * phaseV));
                const float  delSecs = std::max(1.0f / sampleRate,
                    kMaxDelaySecs * 0.5f * (1.0f + depth * lfoVal));
                double readPos = static_cast<double>(state.modWritePos)
                                 + cap - delSecs * sampleRate;
                for (uint32_t c2 = 0; c2 < ch; ++c2)
                    accum[c2] += readCircular(state.modBuf, cap, readPos, ch, c2);
            }

            const float scale = 1.0f / kVoices;
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                samples[f * ch + c2] = dry * inSamp[c2] + wet * accum[c2] * scale;

            state.modWritePos = (state.modWritePos + 1) % cap;
            state.modPhase   += phInc;
            if (state.modPhase >= 1.0) state.modPhase -= 1.0;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // Flanger — single LFO-modulated delay [0..10 ms] with feedback
    // -----------------------------------------------------------------------
    case FilterType::Flanger: {
        if (state.modBuf.empty())
            initFilterState(state, FilterType::Flanger, sampleRate, ch);

        const float depth    = std::clamp(stepParam(data->params[0], bufferDuration),  0.0f,  1.0f);
        const float rateHz   = std::clamp(stepParam(data->params[1], bufferDuration), 0.01f,  5.0f);
        const float feedback = std::clamp(stepParam(data->params[2], bufferDuration), -1.0f,  1.0f);
        const float wet      = std::clamp(stepParam(data->params[3], bufferDuration),  0.0f,  1.0f);
        const float dry      = std::clamp(stepParam(data->params[4], bufferDuration),  0.0f,  1.0f);

        static constexpr float kMaxDelaySecs = 0.010f;
        const uint32_t cap   = state.modCapacity;
        const float    phInc = static_cast<float>(rateHz / sampleRate);
        if (cap == 0) break;

        for (uint32_t f = 0; f < frameCount; ++f) {
            const float lfoVal   = static_cast<float>(std::sin(kTwoPi * state.modPhase));
            const float delSecs  = std::max(1.0f / sampleRate,
                kMaxDelaySecs * depth * (lfoVal + 1.0f) * 0.5f);
            const double readPos = static_cast<double>(state.modWritePos)
                                   + cap - delSecs * sampleRate;

            for (uint32_t c2 = 0; c2 < ch; ++c2) {
                const float delayed = readCircular(state.modBuf, cap, readPos, ch, c2);
                const float orig    = samples[f * ch + c2];
                state.modBuf[state.modWritePos * ch + c2] = orig + feedback * state.modFbk[c2];
                state.modFbk[c2]        = delayed;
                samples[f * ch + c2]    = dry * orig + wet * delayed;
            }

            state.modWritePos = (state.modWritePos + 1) % cap;
            state.modPhase   += phInc;
            if (state.modPhase >= 1.0) state.modPhase -= 1.0;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // PitchShift — granular OLA: two read heads, Hann window, 50% overlap
    //
    // Both read heads advance at pitchRatio samples per input frame.
    // Head 1 is offset by GRAIN_SIZE/2 (50% overlap).
    // At each grain boundary, each head jumps by GRAIN_SIZE*(1-pitchRatio)
    // to maintain original tempo.
    // Two Hann windows at 50% overlap sum to 1.0, so no gain normalisation needed.
    // -----------------------------------------------------------------------
    case FilterType::PitchShift: {
        if (state.pitchBuf.empty())
            initFilterState(state, FilterType::PitchShift, sampleRate, ch);

        const float semitones  = std::clamp(stepParam(data->params[0], bufferDuration), -24.0f, 24.0f);
        const double pitchRatio = std::pow(2.0, static_cast<double>(semitones) / 12.0);

        const uint32_t cap = state.pitchCapacity;
        const uint32_t gs  = FilterState::GRAIN_SIZE;
        if (cap == 0) break;

        const double capD = static_cast<double>(cap);
        const double gsD  = static_cast<double>(gs);

        for (uint32_t f = 0; f < frameCount; ++f) {
            // Write current input to ring buffer
            for (uint32_t c2 = 0; c2 < ch; ++c2)
                state.pitchBuf[state.pitchWritePos * ch + c2] = samples[f * ch + c2];

            // Hann window weights — two windows sum to 1.0 at 50% overlap
            const float w0 = 0.5f * (1.0f - static_cast<float>(
                std::cos(kTwoPi * state.pitchGrainPhase0)));
            const float w1 = 0.5f * (1.0f - static_cast<float>(
                std::cos(kTwoPi * state.pitchGrainPhase1)));

            for (uint32_t c2 = 0; c2 < ch; ++c2) {
                const float s0 = readCircular(state.pitchBuf, cap, state.pitchReadPos0, ch, c2);
                const float s1 = readCircular(state.pitchBuf, cap, state.pitchReadPos1, ch, c2);
                samples[f * ch + c2] = w0 * s0 + w1 * s1;
            }

            state.pitchWritePos = (state.pitchWritePos + 1) % cap;

            state.pitchReadPos0 = std::fmod(state.pitchReadPos0 + pitchRatio, capD);
            state.pitchReadPos1 = std::fmod(state.pitchReadPos1 + pitchRatio, capD);

            state.pitchGrainPhase0 += 1.0 / gsD;
            state.pitchGrainPhase1 += 1.0 / gsD;

            // At grain boundary: jump read head to correct for time drift
            if (state.pitchGrainPhase0 >= 1.0) {
                state.pitchGrainPhase0 -= 1.0;
                state.pitchReadPos0 = std::fmod(
                    state.pitchReadPos0 + gsD * (1.0 - pitchRatio), capD);
                if (state.pitchReadPos0 < 0.0) state.pitchReadPos0 += capD;
            }
            if (state.pitchGrainPhase1 >= 1.0) {
                state.pitchGrainPhase1 -= 1.0;
                state.pitchReadPos1 = std::fmod(
                    state.pitchReadPos1 + gsD * (1.0 - pitchRatio), capD);
                if (state.pitchReadPos1 < 0.0) state.pitchReadPos1 += capD;
            }
        }
        break;
    }

    default:
        break;
    }
}

} // namespace systems::leal::campello_audio::pi
