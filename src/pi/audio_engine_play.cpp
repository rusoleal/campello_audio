/// @file audio_engine_play.cpp
/// @brief Platform-independent AudioEngine::play() dispatch and helpers.
///
/// This file consolidates source-type dispatch logic that was previously
/// copy-pasted into every backend audio_engine.cpp.

#include "audio_engine_play.hpp"
#include <campello_audio/audio_source.hpp>
#include <campello_audio/audio_bus.hpp>
#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/audio_stream.hpp>
#include <campello_audio/random_source.hpp>
#include <campello_audio/music_track.hpp>
#include <campello_audio/pattern_track.hpp>
#include "source_handle.hpp"
#include "mixer.hpp"
#include "voice_manager.hpp"
#include "filter_engine.hpp"
#include "rtpc.hpp"
#include "tone_gen.hpp"
#include "music_track.hpp"
#include "pattern_track.hpp"
#include "audio_stream.hpp"
#include <algorithm>
#include <cmath>
#include <random>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// doPlay — shared voice-allocation logic
// ---------------------------------------------------------------------------

SoundHandle pi::doPlay(MixerData& mx, const AudioSourceHandle* sh,
                       const PlayDescriptor& pd, uint32_t busId) {
    if (!sh) return SoundHandle::invalid;
    if (sh->type == SourceType::Bus) return SoundHandle::invalid;

    if (sh->singleInstance) {
        for (auto& v : mx.voices) {
            if (v.active && v.buffer == sh->pcmBuffer && sh->pcmBuffer)
                v.active = false;
        }
    }

    Voice* v = VoiceManager::allocate(mx);
    if (!v) return SoundHandle::invalid;

    if (sh->pcmBuffer) {
        if (!sh->pcmBuffer->isValid()) { v->active = false; return SoundHandle::invalid; }
        v->buffer = sh->pcmBuffer;
    } else if (sh->type == SourceType::Tone) {
        const auto* th = static_cast<const ToneSourceHandle*>(sh);
        v->ownedBuffer = std::make_unique<DecodedBuffer>(
            generateToneBuffer(th->waveform, th->frequency,
                               mx.config.sampleRate, mx.config.channels));
        v->buffer = v->ownedBuffer.get();
    } else {
        v->active = false;
        return SoundHandle::invalid;
    }

    v->volume  = sh->volume * pd.volume;
    v->pan     = pd.pan;
    v->pitch   = pd.pitch;
    v->paused  = pd.paused;
    v->protect = pd.protect;
    v->busId   = busId;

    if (sh->type == SourceType::Tone) {
        v->loopMode = LoopMode::Loop;
    } else if (pd.looping) {
        v->loopMode = LoopMode::Loop;
    } else if (sh->looping) {
        v->loopMode = (sh->loopMode == LoopMode::None) ? LoopMode::Loop : sh->loopMode;
    } else {
        v->loopMode = sh->loopMode;
    }

    v->readPos     = 0.0;
    v->pingPongFwd = true;
    v->is3d        = pd.enable3d;

    const uint32_t outCh = mx.config.channels > 0 ? mx.config.channels : 2u;
    v->filters = sh->filters;
    for (uint32_t s = 0; s < v->filters.size(); ++s) {
        if (!v->filters[s]) continue;
        const auto* fd = static_cast<const pi::FilterData*>(v->filters[s]->native);
        if (fd) pi::initFilterState(v->filterStates[s], fd->type,
                                    mx.config.sampleRate, outCh);
    }

    v->bindings = sh->bindings;

    if (pd.enable3d) {
        v->pos           = pd.position;
        v->vel           = pd.velocity;
        v->minDist       = pd.minDistance;
        v->maxDist       = pd.maxDistance;
        v->rolloff       = pd.rolloff;
        v->dopplerFactor = pd.dopplerFactor;
    }

    return SoundHandle{v->id};
}

// ---------------------------------------------------------------------------
// playSourceDispatch — source-type routing
// ---------------------------------------------------------------------------

SoundHandle pi::playSourceDispatch(MixerData& mx, AudioSource& source,
                                   const PlayDescriptor& pd) {
    const auto* sh = static_cast<const AudioSourceHandle*>(source.native);
    if (!sh) return SoundHandle::invalid;

    // -----------------------------------------------------------------
    // MusicTrack branch — register a beat-clock player in the mixer.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Music) {
        auto* mh = static_cast<MusicTrackHandle*>(source.native);
        auto& trackData = mh->data;

        for (auto& sec : trackData.sections) {
            if (!sec.audio) { sec.pcm = nullptr; continue; }
            const auto* sSh = static_cast<const AudioSourceHandle*>(sec.audio->native);
            sec.pcm = sSh ? sSh->pcmBuffer : nullptr;
        }

        if (trackData.sections.empty()) return SoundHandle::invalid;

        trackData.currentSection = 0;
        trackData.pendingSection = -1;
        trackData.readPos        = 0.0;
        trackData.beatPos        = 0.0;
        trackData.crossFading    = false;
        trackData.active         = true;

        auto it = std::find(mx.musicPlayers.begin(), mx.musicPlayers.end(), &trackData);
        if (it == mx.musicPlayers.end())
            mx.musicPlayers.push_back(&trackData);

        return SoundHandle::invalid;
    }

    // -----------------------------------------------------------------
    // PatternTrack branch — register a pattern player in the mixer.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::PatternTrack) {
        auto* ph = static_cast<PatternTrackHandle*>(source.native);
        auto& trackData = ph->data;

        if (!trackData.bank || trackData.sections.empty())
            return SoundHandle::invalid;

        for (auto& sec : trackData.sections) {
            sec.pattern = trackData.bank->getPattern(sec.patternLabel).get();
        }

        trackData.currentSection = 0;
        trackData.pendingSection = -1;
        trackData.beatPos        = 0.0;
        trackData.totalBeatPos   = 0.0;
        trackData.crossFading    = false;
        trackData.active         = true;

        auto it = std::find(mx.patternPlayers.begin(), mx.patternPlayers.end(), &trackData);
        if (it == mx.patternPlayers.end())
            mx.patternPlayers.push_back(&trackData);

        return SoundHandle::invalid;
    }

    // -----------------------------------------------------------------
    // AudioStream branch — stream from a ring-buffer prefetch thread.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Stream) {
        auto* ssh = static_cast<pi::AudioStreamHandle*>(source.native);
        if (!ssh || !ssh->data || !ssh->data->isOpen) return SoundHandle::invalid;

        Voice* v = VoiceManager::allocate(mx);
        if (!v) return SoundHandle::invalid;

        v->buffer  = nullptr;
        v->stream  = ssh->data;
        v->volume  = ssh->volume * pd.volume;
        v->pan     = pd.pan;
        v->pitch   = 1.0f;
        v->paused  = pd.paused;
        v->protect = pd.protect;
        v->busId   = 0;
        v->loopMode = (pd.looping || ssh->looping) ? LoopMode::Loop : LoopMode::None;

        ssh->data->looping = pd.looping || ssh->looping;

        return SoundHandle{v->id};
    }

    // -----------------------------------------------------------------
    // RandomSource branch — select a weighted-random variant and
    // delegate to playSourceDispatch on the chosen source.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Random) {
        auto* rsh = static_cast<RandomSourceHandle*>(source.native);
        const int32_t idx = rsh->pickVariant();
        if (idx < 0 || !rsh->variants[idx].source) return SoundHandle::invalid;

        PlayDescriptor modPd = pd;

        if (rsh->pitchVariation > 0.0f) {
            std::uniform_real_distribution<float> d(-rsh->pitchVariation, rsh->pitchVariation);
            modPd.pitch *= std::pow(2.0f, d(rsh->rng) / 12.0f);
        }
        if (rsh->volumeVariation > 0.0f) {
            std::uniform_real_distribution<float> d(-rsh->volumeVariation, rsh->volumeVariation);
            modPd.volume *= std::pow(10.0f, d(rsh->rng) / 20.0f);
        }

        return playSourceDispatch(mx, *rsh->variants[idx].source, modPd);
    }

    // -----------------------------------------------------------------
    // AudioBus branch — register a new BusSlot in the mixer.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Bus) {
        auto* bsh = static_cast<BusSourceHandle*>(source.native);
        const uint32_t outCh = mx.config.channels > 0 ? mx.config.channels : 2u;

        BusSlot slot;
        slot.busId  = mx.nextBusId++;
        slot.active = true;
        slot.volume = sh->volume;
        slot.filters = sh->filters;
        for (uint32_t s = 0; s < slot.filters.size(); ++s) {
            if (!slot.filters[s]) continue;
            const auto* fd = static_cast<const pi::FilterData*>(slot.filters[s]->native);
            if (fd) pi::initFilterState(slot.filterStates[s], fd->type,
                                        mx.config.sampleRate, outCh);
        }

        const uint32_t busId = slot.busId;
        mx.buses.push_back(std::move(slot));

        bsh->busId     = busId;
        bsh->mixerData = &mx;
        bsh->playFn    = [&mx](const AudioSourceHandle* sh,
                               const PlayDescriptor& p,
                               uint32_t bid) -> SoundHandle {
            return doPlay(mx, sh, p, bid);
        };

        return SoundHandle::invalid;
    }

    // -----------------------------------------------------------------
    // Default — one-shot sample or tone.
    // -----------------------------------------------------------------
    return doPlay(mx, sh, pd, 0);
}

// ---------------------------------------------------------------------------
// Transition helpers
// ---------------------------------------------------------------------------

void pi::requestMusicTransition(MixerData& mx, const std::string& sectionLabel) {
    for (auto* player : mx.musicPlayers) {
        if (!player || !player->active) continue;
        const int32_t idx = player->findSection(sectionLabel);
        if (idx < 0) continue;
        if (idx == player->currentSection) continue;
        player->pendingSection = idx;
    }
}

void pi::requestPatternTransition(MixerData& mx, const std::string& sectionLabel) {
    for (auto* player : mx.patternPlayers) {
        if (!player || !player->active) continue;
        const int32_t idx = player->findSection(sectionLabel);
        if (idx < 0) continue;
        if (idx == player->currentSection) continue;
        player->pendingSection = idx;
    }
}

// ---------------------------------------------------------------------------
// RTPC helpers — extracted from per-backend audio_engine.cpp files
// ---------------------------------------------------------------------------

static void applyRtpcToVoices(MixerData& m,
                              const std::string& paramName,
                              float value, float minVal, float maxVal) {
    auto applyOne = [&](Voice& v) {
        if (!v.active) return;
        for (const auto& b : v.bindings) {
            if (b.paramName != paramName) continue;
            const float mapped = applyBinding(value, minVal, maxVal, b);
            switch (b.property) {
                case AudioSourceProperty::Volume:
                    v.volume = mapped;
                    break;
                case AudioSourceProperty::Pitch:
                    v.pitch = mapped;
                    break;
                case AudioSourceProperty::Pan:
                    v.pan = mapped;
                    break;
                case AudioSourceProperty::FilterParam:
                    if (b.filterSlot < v.filters.size() && v.filters[b.filterSlot]) {
                        v.filters[b.filterSlot]->setParam(b.filterParamId, mapped);
                    }
                    break;
                case AudioSourceProperty::LowPassCutoff:
                    if (v.filters[0]) v.filters[0]->setParam(0, mapped);
                    break;
                case AudioSourceProperty::HighPassCutoff:
                    if (v.filters[0]) v.filters[0]->setParam(0, mapped);
                    break;
                case AudioSourceProperty::SendLevel:
                    break;
            }
        }
    };

    for (auto& v : m.voices)        applyOne(v);
    for (auto& v : m.virtualVoices) applyOne(v);
}

void pi::engineRegisterParameter(
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    std::shared_ptr<AudioParameter> p) {
    if (!p) return;
    params[p->getName()] = std::move(p);
}

void pi::engineUnregisterParameter(
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name) {
    params.erase(name);
}

void pi::engineSetParameter(
    MixerData& mx,
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name, float value) {
    auto it = params.find(name);
    if (it == params.end()) return;

    auto& param = *it->second;
    param.setValue(value);

    std::lock_guard lk(mx.voiceMutex);
    mx.rtpcCache[name] = param.getValue();
    applyRtpcToVoices(mx, name, param.getValue(),
                      param.getMinValue(), param.getMaxValue());
}

float pi::engineGetParameter(
    const std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name) {
    const auto it = params.find(name);
    return (it != params.end()) ? it->second->getValue() : 0.0f;
}
