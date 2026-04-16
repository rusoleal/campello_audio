/// @file mixer.cpp
/// @brief Platform-independent mixing core.
///
/// Implements the per-sample mix loop, constant-power pan matrix, linear
/// resampler, loop handling, fade/LFO automation, and visualization capture.
/// Called by the audio backend from its dedicated audio thread.

#include "mixer.hpp"
#include "audio_stream.hpp"
#include "voice_manager.hpp"
#include "filter_engine.hpp"
#include "surround.hpp"
#include <campello_audio/constants/resample_quality.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numbers>

namespace systems::leal::campello_audio::pi {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Nearest-neighbor sample read — frac is ignored; lowest CPU cost.
static inline float readSamplePoint(const float* samples,
                                    uint64_t     frameCount,
                                    uint32_t     channels,
                                    uint64_t     frame0,
                                    float        /*frac*/,
                                    uint32_t     channel) {
    if (frame0 >= frameCount) return 0.0f;
    return samples[frame0 * channels + channel];
}

/// Linear-interpolated sample read at a fractional frame index.
/// Returns 0 for out-of-range positions rather than clamping silently.
static inline float readSampleLinear(const float* samples,
                                     uint64_t     frameCount,
                                     uint32_t     channels,
                                     uint64_t     frame0,
                                     float        frac,
                                     uint32_t     channel) {
    if (frame0 >= frameCount) return 0.0f;
    const float s0 = samples[frame0 * channels + channel];
    const uint64_t frame1 = (frame0 + 1 < frameCount) ? frame0 + 1 : frame0;
    const float s1 = samples[frame1 * channels + channel];
    return s0 + (s1 - s0) * frac;
}

/// Catmull-Rom cubic interpolation using four surrounding frames.
/// Out-of-range indices are clamped to the nearest boundary sample.
static inline float readSampleCatmullRom(const float* samples,
                                          uint64_t     frameCount,
                                          uint32_t     channels,
                                          uint64_t     frame0,
                                          float        frac,
                                          uint32_t     channel) {
    if (frame0 >= frameCount) return 0.0f;

    const int64_t idx = static_cast<int64_t>(frame0);
    const int64_t n   = static_cast<int64_t>(frameCount);

    auto clampSample = [&](int64_t f) -> float {
        if (f < 0) f = 0;
        if (f >= n) f = n - 1;
        return samples[static_cast<uint64_t>(f) * channels + channel];
    };

    const float p0 = clampSample(idx - 1);
    const float p1 = clampSample(idx);
    const float p2 = clampSample(idx + 1);
    const float p3 = clampSample(idx + 2);
    const float t  = frac;

    // Standard Catmull-Rom formula (α = 0.5):
    // 0.5 * ( 2p1 + (-p0+p2)t + (2p0-5p1+4p2-p3)t² + (-p0+3p1-3p2+p3)t³ )
    return 0.5f * ((2.0f * p1)
                 + (-p0 + p2)                            * t
                 + (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3)   * (t * t)
                 + (-p0 + 3.0f*p1 - 3.0f*p2 + p3)        * (t * t * t));
}

/// Dispatch to the appropriate interpolator based on the engine quality setting.
static inline float readSampleQ(ResampleQuality  quality,
                                 const float*     samples,
                                 uint64_t         frameCount,
                                 uint32_t         channels,
                                 uint64_t         frame0,
                                 float            frac,
                                 uint32_t         channel) {
    switch (quality) {
    case ResampleQuality::Point:
        return readSamplePoint(samples, frameCount, channels, frame0, frac, channel);
    case ResampleQuality::CatmullRom:
        return readSampleCatmullRom(samples, frameCount, channels, frame0, frac, channel);
    case ResampleQuality::Linear:
    default:
        return readSampleLinear(samples, frameCount, channels, frame0, frac, channel);
    }
}

// ---------------------------------------------------------------------------
// mixSamples — audio thread entry point
// ---------------------------------------------------------------------------

void MixerData::mixSamples(float* outputBuffer, uint32_t frameCount) {
    const uint32_t outCh        = config.channels > 0 ? config.channels : 2u;
    const uint32_t totalSamples = frameCount * outCh;
    std::memset(outputBuffer, 0, totalSamples * sizeof(float));

    const double frameDuration  = 1.0 / static_cast<double>(config.sampleRate);

    uint32_t activeCount = 0;

    {
        std::lock_guard<std::mutex> lock(voiceMutex);

        // ----------------------------------------------------------------
        // Pre-zero all active bus mix buffers for this callback.
        // ----------------------------------------------------------------
        {
            const uint32_t needed = frameCount * outCh;
            for (auto& bus : buses) {
                if (!bus.active) continue;
                if (bus.mixBuffer.size() < needed) bus.mixBuffer.resize(needed);
                std::fill(bus.mixBuffer.begin(),
                          bus.mixBuffer.begin() + needed, 0.0f);
            }
        }

        for (auto& v : voices) {
            // ----------------------------------------------------------------
            // Phase 14: Streaming voice path
            // ----------------------------------------------------------------
            if (v.stream) {
                if (!v.active || v.paused) continue;

                // Resolve destination: main output or bus mix buffer
                float* dest = outputBuffer;
                if (v.busId != 0) {
                    for (auto& bus : buses) {
                        if (bus.busId == v.busId && bus.active) {
                            dest = bus.mixBuffer.data();
                            break;
                        }
                    }
                }

                // Ensure scratch buffer is large enough
                const uint32_t needed = frameCount * outCh;
                if (voiceTempBuffer.size() < needed) voiceTempBuffer.resize(needed);
                std::fill(voiceTempBuffer.begin(),
                          voiceTempBuffer.begin() + needed, 0.0f);

                // Pull frames from ring buffer
                const uint32_t got = v.stream->consumeFrames(
                    voiceTempBuffer.data(), frameCount, outCh);

                // VBAP pan gains — computed once per buffer for streaming voices.
                float panGains[8] = {};
                computeVbapGains(v.pan, outCh, panGains);

                for (uint32_t f = 0; f < got; ++f) {
                    const float sL = voiceTempBuffer[f * outCh + 0];
                    const float sR = (outCh >= 2) ? voiceTempBuffer[f * outCh + 1] : sL;
                    if (outCh <= 2) {
                        if (outCh == 2) {
                            dest[f * 2 + 0] += sL * panGains[0] * v.volume;
                            dest[f * 2 + 1] += sR * panGains[1] * v.volume;
                        } else {
                            dest[f] += (sL + sR) * 0.5f * panGains[0] * v.volume;
                        }
                    } else {
                        const float mono = (sL + sR) * 0.5f;
                        for (uint32_t c = 0; c < outCh; ++c)
                            dest[f * outCh + c] += mono * panGains[c] * v.volume;
                    }
                }

                if (got > 0) {
                    ++activeCount;
                } else if (v.stream->eof.load(std::memory_order_acquire)) {
                    v.active = false;
                } else {
                    ++activeCount;  // underrun but not EOF — keep voice alive
                }

                continue;  // skip buffer-based processing below
            }

            if (!v.active || v.paused) continue;
            if (!v.buffer || !v.buffer->isValid()) {
                v.active = false;
                continue;
            }

            const DecodedBuffer& buf   = *v.buffer;
            const uint32_t       srcCh = buf.channels;

            // Resolve destination: main output or bus mix buffer.
            float* dest = outputBuffer;
            if (v.busId != 0) {
                for (auto& bus : buses) {
                    if (bus.busId == v.busId && bus.active) {
                        dest = bus.mixBuffer.data();
                        break;
                    }
                }
            }

            // Determine whether this voice has an active filter chain.
            bool hasFilters = false;
            for (const auto& flt : v.filters) {
                if (flt) { hasFilters = true; break; }
            }

            // Grow scratch buffer on demand (no re-alloc after the first few callbacks).
            if (hasFilters) {
                const uint32_t needed = frameCount * outCh;
                if (voiceTempBuffer.size() < needed) voiceTempBuffer.resize(needed);
                std::fill(voiceTempBuffer.begin(),
                          voiceTempBuffer.begin() + needed, 0.0f);
            }

            float* const voiceOut = hasFilters ? voiceTempBuffer.data() : dest;

            for (uint32_t f = 0; f < frameCount; ++f) {
                if (!v.active) break;

                // --------------------------------------------------------
                // Step fade automation (linear ramp per sample)
                // --------------------------------------------------------
                if (v.fadeVolActive) {
                    v.fadeVolElapsed += frameDuration;
                    const double t = std::min(v.fadeVolElapsed / v.fadeVolTime, 1.0);
                    v.volume = v.fadeVolFrom + (v.fadeVolTo - v.fadeVolFrom) * static_cast<float>(t);
                    if (v.fadeVolElapsed >= v.fadeVolTime) {
                        v.volume        = v.fadeVolTo;
                        v.fadeVolActive = false;
                        if (v.fadeVolStopOnDone) { v.active = false; break; }
                    }
                }
                if (v.fadePanActive) {
                    v.fadePanElapsed += frameDuration;
                    const double t = std::min(v.fadePanElapsed / v.fadePanTime, 1.0);
                    v.pan = v.fadePanFrom + (v.fadePanTo - v.fadePanFrom) * static_cast<float>(t);
                    if (v.fadePanElapsed >= v.fadePanTime) {
                        v.pan          = v.fadePanTo;
                        v.fadePanActive = false;
                    }
                }
                if (v.fadePitchActive) {
                    v.fadePitchElapsed += frameDuration;
                    const double t = std::min(v.fadePitchElapsed / v.fadePitchTime, 1.0);
                    v.pitch = v.fadePitchFrom + (v.fadePitchTo - v.fadePitchFrom) * static_cast<float>(t);
                    if (v.fadePitchElapsed >= v.fadePitchTime) {
                        v.pitch           = v.fadePitchTo;
                        v.fadePitchActive = false;
                    }
                }

                // --------------------------------------------------------
                // Compute effective volume and pan
                //   3D attenuation gain is multiplied in; LFO overrides base
                // --------------------------------------------------------
                float effVol = v.volume * (v.is3d ? v.attenuationGain : 1.0f);
                float effPan = v.pan;

                if (v.lfoVolActive) {
                    v.lfoVolPhase += frameDuration / v.lfoVolPeriod;
                    if (v.lfoVolPhase >= 1.0) v.lfoVolPhase -= 1.0;
                    const float mid  = (v.lfoVolFrom + v.lfoVolTo) * 0.5f;
                    const float amp  = (v.lfoVolTo   - v.lfoVolFrom) * 0.5f;
                    effVol = mid + amp * static_cast<float>(std::sin(kTwoPi * v.lfoVolPhase));
                }
                if (v.lfoPanActive) {
                    v.lfoPanPhase += frameDuration / v.lfoPanPeriod;
                    if (v.lfoPanPhase >= 1.0) v.lfoPanPhase -= 1.0;
                    const float mid  = (v.lfoPanFrom + v.lfoPanTo) * 0.5f;
                    const float amp  = (v.lfoPanTo   - v.lfoPanFrom) * 0.5f;
                    effPan = mid + amp * static_cast<float>(std::sin(kTwoPi * v.lfoPanPhase));
                }

                // --------------------------------------------------------
                // Read source samples at readPos (linear interpolation)
                // --------------------------------------------------------
                if (v.readPos < 0.0) v.readPos = 0.0;

                const uint64_t frame0 = static_cast<uint64_t>(v.readPos);
                const float    frac   = static_cast<float>(v.readPos - static_cast<double>(frame0));

                // Up to 2 source channels; beyond stereo is unsupported until Phase 16.
                const uint32_t readCh = std::min(srcCh, 2u);
                float srcSamp[2]      = {0.0f, 0.0f};
                for (uint32_t c = 0; c < readCh; ++c) {
                    srcSamp[c] = readSampleQ(config.resampleQuality,
                                             buf.samples.data(),
                                             buf.frameCount,
                                             srcCh, frame0, frac, c);
                }
                if (srcCh == 1) srcSamp[1] = srcSamp[0]; // mono → duplicate to both channels

                // --------------------------------------------------------
                // VBAP pan matrix — stereo uses constant-power; surround
                // folds source to mono then distributes by azimuth.
                //   pan ∈ [-1, +1] → azimuth ∈ [-π/2, +π/2]
                // --------------------------------------------------------
                float panGains[8] = {};
                computeVbapGains(effPan, outCh, panGains);

                // --------------------------------------------------------
                // Accumulate into voiceOut (either outputBuffer or temp)
                // --------------------------------------------------------
                if (outCh <= 2) {
                    if (outCh == 2) {
                        voiceOut[f * 2 + 0] += srcSamp[0] * panGains[0] * effVol;
                        voiceOut[f * 2 + 1] += srcSamp[1] * panGains[1] * effVol;
                    } else {
                        voiceOut[f] += (srcSamp[0] + srcSamp[1]) * 0.5f
                                       * panGains[0] * effVol;
                    }
                } else {
                    // Surround: fold L+R to mono then distribute via VBAP gains.
                    const float mono = (srcSamp[0] + srcSamp[1]) * 0.5f;
                    for (uint32_t c = 0; c < outCh; ++c)
                        voiceOut[f * outCh + c] += mono * panGains[c] * effVol;
                }

                // --------------------------------------------------------
                // Advance read position (pitch-adjusted, linear resampling)
                // Doppler shifts the effective playback pitch for 3D voices.
                // --------------------------------------------------------
                const float  effPitch = v.pitch * (v.is3d ? v.dopplerPitch : 1.0f);
                const float  safeP   = std::max(effPitch, 0.0001f);
                const double advance = static_cast<double>(safeP)
                                     * static_cast<double>(buf.sampleRate)
                                     / static_cast<double>(config.sampleRate);

                switch (v.loopMode) {
                case LoopMode::None:
                    v.readPos += advance;
                    if (v.readPos >= static_cast<double>(buf.frameCount)) {
                        v.active = false;
                    }
                    break;

                case LoopMode::Loop:
                    v.readPos += advance;
                    if (v.readPos >= static_cast<double>(buf.frameCount)) {
                        v.readPos = std::fmod(v.readPos,
                                              static_cast<double>(buf.frameCount));
                    }
                    break;

                case LoopMode::PingPong: {
                    const double lastFrame = static_cast<double>(buf.frameCount) - 1.0;
                    if (v.pingPongFwd) {
                        v.readPos += advance;
                        if (v.readPos >= lastFrame) {
                            const double over = v.readPos - lastFrame;
                            v.readPos         = lastFrame - over;
                            if (v.readPos < 0.0) v.readPos = 0.0;
                            v.pingPongFwd = false;
                        }
                    } else {
                        v.readPos -= advance;
                        if (v.readPos <= 0.0) {
                            v.readPos     = -v.readPos;
                            v.pingPongFwd = true;
                        }
                    }
                    break;
                }
                } // switch loopMode
            } // per-frame loop

            // Apply filter chain and accumulate into destination (only when filters active)
            if (hasFilters && v.active) {
                const double bufDur =
                    static_cast<double>(frameCount) / static_cast<double>(config.sampleRate);
                for (uint32_t s = 0; s < v.filters.size(); ++s) {
                    if (!v.filters[s]) continue;
                    auto* fd = static_cast<FilterData*>(v.filters[s]->native);
                    if (!fd || fd->type == FilterType::None) continue;
                    processFilter(fd, v.filterStates[s],
                                  voiceTempBuffer.data(), frameCount,
                                  outCh, config.sampleRate, bufDur);
                }
                const uint32_t n = frameCount * outCh;
                for (uint32_t i = 0; i < n; ++i) dest[i] += voiceTempBuffer[i];
            }

            if (v.active) ++activeCount;
        } // per-voice loop

        // ----------------------------------------------------------------
        // Phase 6: Virtualize real voices below threshold
        // ----------------------------------------------------------------
        if (config.maxVirtualVoices > 0 && config.virtualizeThreshold > 0.0f) {
            for (auto& v : voices) {
                if (!v.active || v.paused || v.protect) continue;
                if (v.volume * (v.is3d ? v.attenuationGain : 1.0f) * globalVolume
                        < config.virtualizeThreshold &&
                    virtualVoices.size() < config.maxVirtualVoices) {
                    virtualVoices.push_back(std::move(v));
                    v = Voice{};   // free the real slot
                    --activeCount; // we moved one active voice out
                }
            }
        }

        // ----------------------------------------------------------------
        // Phase 6: Advance virtual voice positions + possible re-activation
        // ----------------------------------------------------------------
        const double bufferDuration =
            static_cast<double>(frameCount) / static_cast<double>(config.sampleRate);

        for (auto it = virtualVoices.begin(); it != virtualVoices.end(); ) {
            Voice& vv = *it;

            if (!vv.active || !vv.buffer || !vv.buffer->isValid()) {
                it = virtualVoices.erase(it);
                continue;
            }
            if (vv.paused) { ++it; continue; }

            // Advance readPos by the equivalent of frameCount output frames.
            const double advance =
                static_cast<double>(std::max(vv.pitch, 0.0001f))
                * static_cast<double>(vv.buffer->sampleRate)
                / static_cast<double>(config.sampleRate);
            const double delta = advance * static_cast<double>(frameCount);

            bool ended = false;
            switch (vv.loopMode) {
            case LoopMode::None:
                vv.readPos += delta;
                if (vv.readPos >= static_cast<double>(vv.buffer->frameCount))
                    ended = true;
                break;

            case LoopMode::Loop:
                vv.readPos += delta;
                if (vv.readPos >= static_cast<double>(vv.buffer->frameCount)) {
                    vv.readPos = std::fmod(vv.readPos,
                                           static_cast<double>(vv.buffer->frameCount));
                }
                break;

            case LoopMode::PingPong: {
                const double last = static_cast<double>(vv.buffer->frameCount) - 1.0;
                double rem = delta;
                while (rem > 0.0 && last > 0.0) {
                    if (vv.pingPongFwd) {
                        const double toEnd = last - vv.readPos;
                        if (rem <= toEnd) { vv.readPos += rem; rem = 0.0; }
                        else { rem -= toEnd; vv.readPos = last; vv.pingPongFwd = false; }
                    } else {
                        if (rem <= vv.readPos) { vv.readPos -= rem; rem = 0.0; }
                        else { rem -= vv.readPos; vv.readPos = 0.0; vv.pingPongFwd = true; }
                    }
                }
                break;
            }
            } // switch loopMode

            if (ended) {
                it = virtualVoices.erase(it);
                continue;
            }

            // Step volume fade (per-buffer approximation — sufficient while virtual)
            if (vv.fadeVolActive) {
                vv.fadeVolElapsed += bufferDuration;
                const double t = std::min(vv.fadeVolElapsed / vv.fadeVolTime, 1.0);
                vv.volume = vv.fadeVolFrom
                          + (vv.fadeVolTo - vv.fadeVolFrom) * static_cast<float>(t);
                if (vv.fadeVolElapsed >= vv.fadeVolTime) {
                    vv.volume        = vv.fadeVolTo;
                    vv.fadeVolActive = false;
                    if (vv.fadeVolStopOnDone) {
                        it = virtualVoices.erase(it);
                        continue;
                    }
                }
            }

            // Re-activate if effective volume is now above threshold
            if (vv.volume * (vv.is3d ? vv.attenuationGain : 1.0f) * globalVolume
                    >= config.virtualizeThreshold) {
                if (VoiceManager::reactivate(*this, vv)) {
                    it = virtualVoices.erase(it);
                    ++activeCount;
                    continue;
                }
            }

            ++it;
        } // virtual voice loop

        virtualVoiceCount.store(static_cast<uint32_t>(virtualVoices.size()),
                                std::memory_order_relaxed);

        // ----------------------------------------------------------------
        // Phase 9: Post-process audio buses.
        //   Pass 1 — apply each bus's filter chain in-place.
        //   Pass 2 — compute sidechain gain from trigger RMS, apply bus
        //            volume, accumulate into main output, capture viz.
        // ----------------------------------------------------------------
        if (!buses.empty()) {
            const double busDur =
                static_cast<double>(frameCount) / static_cast<double>(config.sampleRate);
            const uint32_t busN = frameCount * outCh;

            // Pass 1: filters
            for (auto& bus : buses) {
                if (!bus.active || bus.mixBuffer.size() < busN) continue;
                for (uint32_t s = 0; s < bus.filters.size(); ++s) {
                    if (!bus.filters[s]) continue;
                    auto* fd = static_cast<FilterData*>(bus.filters[s]->native);
                    if (!fd || fd->type == FilterType::None) continue;
                    processFilter(fd, bus.filterStates[s],
                                  bus.mixBuffer.data(), frameCount,
                                  outCh, config.sampleRate, busDur);
                }
            }

            // Pass 2: sidechain → volume → accumulate → viz
            for (auto& bus : buses) {
                if (!bus.active || bus.mixBuffer.size() < busN) continue;

                // Sidechain gain computation (per-buffer envelope follower)
                float sidechainGain = 1.0f;
                if (bus.sidechainActive) {
                    // Find trigger bus and compute its RMS.
                    float triggerRms = 0.0f;
                    for (const auto& trig : buses) {
                        if (trig.busId != bus.sidechainTriggerId || !trig.active) continue;
                        if (trig.mixBuffer.size() < busN) break;
                        float sum = 0.0f;
                        for (uint32_t i = 0; i < busN; ++i)
                            sum += trig.mixBuffer[i] * trig.mixBuffer[i];
                        triggerRms = std::sqrt(sum / static_cast<float>(busN));
                        break;
                    }

                    // Move envelope toward duckDb when trigger is active,
                    // toward 0 dB when silent.
                    const double targetDb =
                        (triggerRms > 1e-6f) ? static_cast<double>(bus.sidechainDuckDb) : 0.0;
                    if (targetDb < bus.sidechainEnvDb) {
                        bus.sidechainEnvDb = bus.sidechainAttCoef * bus.sidechainEnvDb
                                           + (1.0 - bus.sidechainAttCoef) * targetDb;
                    } else {
                        bus.sidechainEnvDb = bus.sidechainRelCoef * bus.sidechainEnvDb
                                           + (1.0 - bus.sidechainRelCoef) * targetDb;
                    }
                    sidechainGain = std::pow(10.0f,
                        static_cast<float>(bus.sidechainEnvDb) / 20.0f);
                }

                // Apply bus volume + sidechain gain and accumulate into main output.
                const float totalGain = bus.volume * sidechainGain;
                for (uint32_t i = 0; i < busN; ++i)
                    outputBuffer[i] += bus.mixBuffer[i] * totalGain;

                // Bus visualization capture.
                if (bus.vizEnabled) {
                    if (bus.vizBuffer.size() < busN) bus.vizBuffer.resize(busN);
                    std::copy(bus.mixBuffer.begin(),
                              bus.mixBuffer.begin() + busN,
                              bus.vizBuffer.begin());
                }
            }
        }

        // ----------------------------------------------------------------
        // Phase 13: Drive active MusicTrack players.
        // ----------------------------------------------------------------
        for (auto* player : musicPlayers) {
            if (!player || !player->active) continue;

            const double beatsPerFrame =
                static_cast<double>(player->bpm) / (60.0 * config.sampleRate);
            const double barLength = static_cast<double>(player->beatsPerBar);

            for (uint32_t f = 0; f < frameCount; ++f) {
                // ---- Advance beat clock ----
                const double prevBeat = player->beatPos;
                player->beatPos += beatsPerFrame;

                bool barBoundary  = false;
                bool beatBoundary = false;
                if (player->beatPos >= barLength) {
                    player->beatPos -= barLength;
                    barBoundary  = true;
                    beatBoundary = true;
                } else {
                    // Crossed an integer beat (but not a bar boundary)?
                    if (static_cast<uint32_t>(player->beatPos) >
                        static_cast<uint32_t>(prevBeat))
                        beatBoundary = true;
                }

                // ---- Mix current section into output ----
                bool loopWrapped = false;
                const int32_t curIdx = player->currentSection;
                if (curIdx >= 0 && curIdx < static_cast<int32_t>(player->sections.size())) {
                    const DecodedBuffer* pcm = player->sections[curIdx].pcm;
                    if (pcm && pcm->isValid()) {
                        const uint64_t frame0 = static_cast<uint64_t>(player->readPos);
                        const float    frac   = static_cast<float>(
                                                    player->readPos - static_cast<double>(frame0));
                        const uint32_t srcCh  = pcm->channels;
                        const uint32_t rdCh   = std::min(srcCh, 2u);
                        float s[2] = {0.0f, 0.0f};
                        for (uint32_t c = 0; c < rdCh; ++c)
                            s[c] = readSampleQ(config.resampleQuality,
                                               pcm->samples.data(),
                                               pcm->frameCount, srcCh,
                                               frame0, frac, c);
                        if (srcCh == 1) s[1] = s[0];

                        if (outCh >= 2) {
                            outputBuffer[f * outCh + 0] += s[0] * player->volume;
                            outputBuffer[f * outCh + 1] += s[1] * player->volume;
                        } else {
                            outputBuffer[f] += (s[0] + s[1]) * 0.5f * player->volume;
                        }

                        // Advance current section read position (always loop).
                        const double advance =
                            static_cast<double>(pcm->sampleRate) /
                            static_cast<double>(config.sampleRate);
                        player->readPos += advance;
                        if (player->readPos >= static_cast<double>(pcm->frameCount)) {
                            player->readPos = std::fmod(
                                player->readPos,
                                static_cast<double>(pcm->frameCount));
                            loopWrapped = true;
                        }
                    }
                }

                // ---- Mix outgoing cross-fade section ----
                if (player->crossFading) {
                    player->crossFadeElapsed += 1.0 / static_cast<double>(config.sampleRate);
                    const double alpha =
                        std::min(player->crossFadeElapsed / player->crossFadeSecs, 1.0);
                    const float fadeOutGain = static_cast<float>(1.0 - alpha) * player->volume;

                    const int32_t oldIdx = player->crossFadeFrom;
                    if (oldIdx >= 0 &&
                        oldIdx < static_cast<int32_t>(player->sections.size())) {
                        const DecodedBuffer* oldPcm = player->sections[oldIdx].pcm;
                        if (oldPcm && oldPcm->isValid()) {
                            const uint64_t fr0 = static_cast<uint64_t>(player->crossFadeFromPos);
                            const float    fc  = static_cast<float>(
                                player->crossFadeFromPos - static_cast<double>(fr0));
                            const uint32_t srcCh = oldPcm->channels;
                            const uint32_t rdCh  = std::min(srcCh, 2u);
                            float os[2] = {0.0f, 0.0f};
                            for (uint32_t c = 0; c < rdCh; ++c)
                                os[c] = readSampleQ(config.resampleQuality,
                                                    oldPcm->samples.data(),
                                                    oldPcm->frameCount, srcCh,
                                                    fr0, fc, c);
                            if (srcCh == 1) os[1] = os[0];

                            if (outCh >= 2) {
                                outputBuffer[f * outCh + 0] += os[0] * fadeOutGain;
                                outputBuffer[f * outCh + 1] += os[1] * fadeOutGain;
                            } else {
                                outputBuffer[f] += (os[0] + os[1]) * 0.5f * fadeOutGain;
                            }

                            const double adv =
                                static_cast<double>(oldPcm->sampleRate) /
                                static_cast<double>(config.sampleRate);
                            player->crossFadeFromPos += adv;
                            if (player->crossFadeFromPos >=
                                    static_cast<double>(oldPcm->frameCount)) {
                                player->crossFadeFromPos = std::fmod(
                                    player->crossFadeFromPos,
                                    static_cast<double>(oldPcm->frameCount));
                            }
                        }
                    }

                    if (alpha >= 1.0) player->crossFading = false;
                }

                // ---- Check and fire pending transition ----
                if (player->pendingSection >= 0 && !player->crossFading) {
                    const TransitionDef* td =
                        player->findTransition(player->currentSection,
                                               player->pendingSection);
                    const TransitionRule rule = td ? td->rule
                                                   : TransitionRule::Immediate;
                    const double blendSecs = td ? td->blendSecs : 0.0;

                    bool fire = false;
                    switch (rule) {
                    case TransitionRule::Immediate:     fire = true;        break;
                    case TransitionRule::OnBeat:        fire = beatBoundary; break;
                    case TransitionRule::OnBar:         fire = barBoundary;  break;
                    case TransitionRule::OnNextSection: fire = loopWrapped;  break;
                    case TransitionRule::CrossFade:     fire = true;        break;
                    }

                    if (fire) {
                        if (rule == TransitionRule::CrossFade && blendSecs > 0.0) {
                            player->crossFading      = true;
                            player->crossFadeFrom    = player->currentSection;
                            player->crossFadeFromPos = player->readPos;
                            player->crossFadeElapsed = 0.0;
                            player->crossFadeSecs    = blendSecs;
                        }
                        player->currentSection = player->pendingSection;
                        player->readPos        = 0.0;
                        player->pendingSection = -1;
                    }
                }
            } // per-frame loop (music player)
        } // music players loop

        // ----------------------------------------------------------------
        // Step global-volume fade (per-buffer approximation)
        // ----------------------------------------------------------------
        if (gFadeActive) {
            gFadeElapsed += frameCount * frameDuration;
            const double t = std::min(gFadeElapsed / gFadeTime, 1.0);
            globalVolume   = gFadeFrom + (gFadeTo - gFadeFrom) * static_cast<float>(t);
            if (gFadeElapsed >= gFadeTime) {
                globalVolume = gFadeTo;
                gFadeActive  = false;
            }
        }

        activeVoiceCount.store(activeCount, std::memory_order_relaxed);
    } // unlock voiceMutex

    // -----------------------------------------------------------------------
    // Apply master volume
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < totalSamples; ++i) {
        outputBuffer[i] *= globalVolume;
    }

    // -----------------------------------------------------------------------
    // Visualization capture
    // -----------------------------------------------------------------------
    if (vizEnabled && !vizBuffer.empty()) {
        const uint32_t n = std::min<uint32_t>(totalSamples,
                                              static_cast<uint32_t>(vizBuffer.size()));
        std::copy(outputBuffer, outputBuffer + n, vizBuffer.data());
    }
}

} // namespace systems::leal::campello_audio::pi
