/// @file audio_engine.cpp (CoreAudio backend)
/// @brief AudioEngine implementation for macOS and iOS.
///
/// ### Device setup (Phase 3a)
///   1. (iOS only) Configure AVAudioSession for playback.
///   2. Locate the default output AudioComponent.
///   3. Instantiate and configure an AudioUnit (Float32 interleaved PCM).
///   4. Register renderCallback → MixerData::mixSamples().
///   5. AudioUnitInitialize + AudioOutputUnitStart.
///   6. (iOS only) Register AVAudioSessionInterruptionNotification.
///
/// ### Playback pipeline (Phase 4)
///   AudioEngine::play() locks voiceMutex, calls VoiceManager::allocate(),
///   binds the source's DecodedBuffer (or generates a ToneSource buffer), sets
///   initial voice parameters from PlayDescriptor + source defaults, and returns
///   a SoundHandle wrapping the voice ID.

#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/audio_bus.hpp>
#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/audio_snapshot.hpp>
#include <campello_audio/audio_stream.hpp>
#include <campello_audio/random_source.hpp>
#include <campello_audio/music_track.hpp>
#include "common.hpp"
#include "../pi/voice_manager.hpp"
#include "../pi/source_handle.hpp"
#include "../pi/audio_stream.hpp"
#include "../pi/mixer.hpp"
#include "../pi/snapshot.hpp"
#include "../pi/tone_gen.hpp"
#include "../pi/audio3d.hpp"
#include "../pi/music_track.hpp"
#include "../pi/audio_engine_play.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::coreaudio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Cast an AudioEngine's opaque native pointer to CoreAudioData.
static inline CoreAudioData* data(void* native) {
    return static_cast<CoreAudioData*>(native);
}

/// Helper: find and modify a voice under the mixer lock.
/// @p fn receives a Voice& and may modify it; returns false if voice not found.
template<typename Fn>
static bool withVoice(void* native, SoundHandle handle, Fn&& fn) {
    if (!native || !handle.isValid()) return false;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    Voice* v = VoiceManager::findAny(m, handle.id);
    if (!v) return false;
    fn(*v);
    return true;
}

// ---------------------------------------------------------------------------
// Render callback — audio thread
// ---------------------------------------------------------------------------

OSStatus coreaudio::renderCallback(void*                       inRefCon,
                                   AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                   const AudioTimeStamp*       /*inTimeStamp*/,
                                   UInt32                      /*inBusNumber*/,
                                   UInt32                      inNumberFrames,
                                   AudioBufferList*            ioData) {
    auto* d = static_cast<CoreAudioData*>(inRefCon);

    if (ioData->mNumberBuffers == 1) {
        d->mixer.mixSamples(static_cast<float*>(ioData->mBuffers[0].mData),
                            inNumberFrames);
    } else {
        for (UInt32 b = 0; b < ioData->mNumberBuffers; ++b) {
            std::memset(ioData->mBuffers[b].mData, 0,
                        ioData->mBuffers[b].mDataByteSize);
        }
    }
    return noErr;
}

// ---------------------------------------------------------------------------
// AudioEngine — lifecycle
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { deinit(); }

bool AudioEngine::init(const AudioEngineDescriptor& descriptor) {
    auto* d = new CoreAudioData();
    d->mixer.config = descriptor;
    VoiceManager::init(d->mixer);

    if (descriptor.visualization) {
        d->mixer.vizBuffer.resize(descriptor.bufferSize * descriptor.channels);
        d->mixer.vizEnabled = true;
    }

#ifdef CAMPELLO_AUDIO_IOS
    coreaudio::setupAVAudioSession();
#endif

    AudioComponentDescription compDesc{};
    compDesc.componentType         = kAudioUnitType_Output;
#ifdef CAMPELLO_AUDIO_IOS
    compDesc.componentSubType      = kAudioUnitSubType_RemoteIO;
#else
    compDesc.componentSubType      = kAudioUnitSubType_DefaultOutput;
#endif
    compDesc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &compDesc);
    if (!comp) { delete d; return false; }

    if (AudioComponentInstanceNew(comp, &d->audioUnit) != noErr) {
        delete d; return false;
    }

    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate       = static_cast<Float64>(descriptor.sampleRate);
    asbd.mFormatID         = kAudioFormatLinearPCM;
    asbd.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd.mChannelsPerFrame = descriptor.channels;
    asbd.mBitsPerChannel   = 32;
    asbd.mFramesPerPacket  = 1;
    asbd.mBytesPerFrame    = sizeof(float) * descriptor.channels;
    asbd.mBytesPerPacket   = asbd.mBytesPerFrame;

    if (AudioUnitSetProperty(d->audioUnit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &asbd, sizeof(asbd)) != noErr) {
        AudioComponentInstanceDispose(d->audioUnit); delete d; return false;
    }

    AURenderCallbackStruct cb{};
    cb.inputProc       = coreaudio::renderCallback;
    cb.inputProcRefCon = d;
    if (AudioUnitSetProperty(d->audioUnit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr) {
        AudioComponentInstanceDispose(d->audioUnit); delete d; return false;
    }

    if (AudioUnitInitialize(d->audioUnit) != noErr) {
        AudioComponentInstanceDispose(d->audioUnit); delete d; return false;
    }

    if (AudioOutputUnitStart(d->audioUnit) != noErr) {
        AudioUnitUninitialize(d->audioUnit);
        AudioComponentInstanceDispose(d->audioUnit);
        delete d; return false;
    }

    d->running = true;

#ifdef CAMPELLO_AUDIO_IOS
    coreaudio::registerInterruptionHandler(d);
#endif

    native = d;
    return true;
}

void AudioEngine::deinit() {
    if (!native) return;
    auto* d = data(native);

    if (d->audioUnit) {
        if (d->running) {
            AudioOutputUnitStop(d->audioUnit);
            d->running = false;
        }
        AudioUnitUninitialize(d->audioUnit);
        AudioComponentInstanceDispose(d->audioUnit);
        d->audioUnit = nullptr;
    }

#ifdef CAMPELLO_AUDIO_IOS
    coreaudio::teardownAVAudioSession();
#endif

    delete d;
    native = nullptr;
}

void AudioEngine::tick() {
    if (!native) return;
    auto* d = data(native);

    // ---- Async load polling ----
    for (auto it = d->pendingLoads.begin(); it != d->pendingLoads.end(); ) {
        // ready  → async task completed
        // deferred → lazy future (runs synchronously when .get() is called)
        // timeout  → still in flight, leave in queue
        const auto status = it->future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready ||
            status == std::future_status::deferred) {
            bool ok = false;
            try {
                auto bytes = it->future.get();
                if (!bytes.empty()) {
                    auto* sh = static_cast<pi::AudioSourceHandle*>(it->source->native);
                    if (sh && sh->decodeBytes) {
                        ok = sh->decodeBytes(bytes);
                    }
                }
            } catch (...) {
                ok = false;
            }
            if (it->onDone) it->onDone(ok);
            it = d->pendingLoads.erase(it);
        } else {
            ++it;
        }
    }

    // ---- Snapshot blend ----
    const auto now = std::chrono::steady_clock::now();
    if (d->lastTickTimeValid) {
        const double deltaSecs =
            std::chrono::duration<double>(now - d->lastTickTime).count();

        auto& ss = d->snapshots;
        if (ss.blending) {
            ss.blendElapsed += deltaSecs;
            const double t = (ss.blendTimeSecs > 0.0)
                             ? std::clamp(ss.blendElapsed / ss.blendTimeSecs, 0.0, 1.0)
                             : 1.0;
            const float  ft = static_cast<float>(t);

            auto& m = d->mixer;
            std::lock_guard lk(m.voiceMutex);

            m.globalVolume = ss.globalFrom + (ss.globalTo - ss.globalFrom) * ft;
            for (const auto& bb : ss.busBlends) {
                for (auto& slot : m.buses) {
                    if (slot.busId == bb.busId && slot.active) {
                        slot.volume = bb.from + (bb.to - bb.from) * ft;
                        break;
                    }
                }
            }

            if (t >= 1.0) {
                ss.blending = false;
                if (ss.reverting) {
                    ss.reverting        = false;
                    ss.baselineCaptured = false;  // fresh slate for next snapshot
                }
            }
        }
    }
    d->lastTickTime      = now;
    d->lastTickTimeValid = true;
}

void AudioEngine::loadAsync(AudioSource& source, AsyncByteLoader loader, LoadCallback onDone) {
    if (!native || !loader) return;
    data(native)->pendingLoads.push_back({
        &source,
        loader(),             // call immediately — future starts in flight now
        std::move(onDone)
    });
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

SoundHandle AudioEngine::play(AudioSource& source) {
    return play(source, PlayDescriptor{});
}

SoundHandle AudioEngine::play(AudioSource& source, const PlayDescriptor& pd) {
    if (!native) return SoundHandle::invalid;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    return pi::playSourceDispatch(m, source, pd);
}

SoundHandle AudioEngine::play3d(AudioSource& source,
                                float x, float y, float z) {
    PlayDescriptor pd;
    pd.enable3d  = true;
    pd.position  = {x, y, z};
    return play(source, pd);
}

SoundHandle AudioEngine::playBackground(AudioSource& source) {
    PlayDescriptor pd;
    pd.protect = true;
    return play(source, pd);
}

// ---------------------------------------------------------------------------
// Voice control
// ---------------------------------------------------------------------------

void AudioEngine::stop(SoundHandle handle) {
    if (!native || !handle.isValid()) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    VoiceManager::free(m, handle.id);
    VoiceManager::freeVirtual(m, handle.id);
}

void AudioEngine::stopAll() {
    if (!native) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    for (auto& v : m.voices) v.active = false;
    m.virtualVoices.clear();
}

void AudioEngine::pause(SoundHandle handle) {
    withVoice(native, handle, [](Voice& v) { v.paused = true; });
}

void AudioEngine::resume(SoundHandle handle) {
    withVoice(native, handle, [](Voice& v) { v.paused = false; });
}

bool AudioEngine::isPaused(SoundHandle handle) const {
    if (!native || !handle.isValid()) return false;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    const Voice* v = VoiceManager::findAny(m, handle.id);
    return v && v->paused;
}

bool AudioEngine::isValid(SoundHandle handle) const {
    if (!native || !handle.isValid()) return false;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    return VoiceManager::findAny(m, handle.id) != nullptr;
}

// ---------------------------------------------------------------------------
// Per-voice parameters
// ---------------------------------------------------------------------------

void AudioEngine::setVolume(SoundHandle h, float v) {
    withVoice(native, h, [v](Voice& voice) { voice.volume = v; });
}

void AudioEngine::setPan(SoundHandle h, float p) {
    withVoice(native, h, [p](Voice& voice) { voice.pan = p; });
}

void AudioEngine::setPitch(SoundHandle h, float p) {
    withVoice(native, h, [p](Voice& voice) { voice.pitch = p; });
}

void AudioEngine::setLooping(SoundHandle h, bool loop) {
    withVoice(native, h, [loop](Voice& voice) {
        voice.loopMode = loop ? LoopMode::Loop : LoopMode::None;
    });
}

void AudioEngine::setProtect(SoundHandle h, bool protect) {
    withVoice(native, h, [protect](Voice& voice) { voice.protect = protect; });
}

// ---------------------------------------------------------------------------
// Seek / position
// ---------------------------------------------------------------------------

void AudioEngine::seek(SoundHandle handle, double seconds) {
    withVoice(native, handle, [seconds](Voice& v) {
        if (v.stream) {
            v.stream->requestSeek(seconds);
            return;
        }
        if (!v.buffer) return;
        const double maxPos = static_cast<double>(v.buffer->frameCount - 1);
        v.readPos = std::clamp(seconds * v.buffer->sampleRate, 0.0, maxPos);
    });
}

double AudioEngine::getPosition(SoundHandle handle) const {
    if (!native || !handle.isValid()) return 0.0;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    const Voice* v = VoiceManager::findAny(m, handle.id);
    if (!v || !v->buffer || v->buffer->sampleRate == 0) return 0.0;
    return v->readPos / static_cast<double>(v->buffer->sampleRate);
}

// ---------------------------------------------------------------------------
// Fades
// ---------------------------------------------------------------------------

void AudioEngine::fadeVolume(SoundHandle h, float to, double timeSecs) {
    withVoice(native, h, [to, timeSecs](Voice& v) {
        v.fadeVolFrom    = v.volume;
        v.fadeVolTo      = to;
        v.fadeVolTime    = timeSecs;
        v.fadeVolElapsed = 0.0;
        v.fadeVolActive  = true;
        v.fadeVolStopOnDone = false;
    });
}

void AudioEngine::fadePan(SoundHandle h, float to, double timeSecs) {
    withVoice(native, h, [to, timeSecs](Voice& v) {
        v.fadePanFrom    = v.pan;
        v.fadePanTo      = to;
        v.fadePanTime    = timeSecs;
        v.fadePanElapsed = 0.0;
        v.fadePanActive  = true;
    });
}

void AudioEngine::fadePitch(SoundHandle h, float to, double timeSecs) {
    withVoice(native, h, [to, timeSecs](Voice& v) {
        v.fadePitchFrom    = v.pitch;
        v.fadePitchTo      = to;
        v.fadePitchTime    = timeSecs;
        v.fadePitchElapsed = 0.0;
        v.fadePitchActive  = true;
    });
}

void AudioEngine::fadeGlobalVolume(float to, double timeSecs) {
    if (!native) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    m.gFadeFrom    = m.globalVolume;
    m.gFadeTo      = to;
    m.gFadeTime    = timeSecs;
    m.gFadeElapsed = 0.0;
    m.gFadeActive  = true;
}

// ---------------------------------------------------------------------------
// LFO
// ---------------------------------------------------------------------------

void AudioEngine::oscillateVolume(SoundHandle h,
                                  float from, float to, double periodSecs) {
    withVoice(native, h, [from, to, periodSecs](Voice& v) {
        v.lfoVolFrom   = from;
        v.lfoVolTo     = to;
        v.lfoVolPeriod = periodSecs;
        v.lfoVolPhase  = 0.0;
        v.lfoVolActive = true;
    });
}

void AudioEngine::oscillatePan(SoundHandle h,
                               float from, float to, double periodSecs) {
    withVoice(native, h, [from, to, periodSecs](Voice& v) {
        v.lfoPanFrom   = from;
        v.lfoPanTo     = to;
        v.lfoPanPeriod = periodSecs;
        v.lfoPanPhase  = 0.0;
        v.lfoPanActive = true;
    });
}

// ---------------------------------------------------------------------------
// Global parameters
// ---------------------------------------------------------------------------

float AudioEngine::getGlobalVolume() const {
    if (!native) return 1.0f;
    return data(native)->mixer.globalVolume;
}

void AudioEngine::setGlobalVolume(float volume) {
    if (!native) return;
    data(native)->mixer.globalVolume = volume;
}

uint32_t AudioEngine::getActiveVoiceCount() const {
    if (!native) return 0;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    uint32_t count = 0;
    for (const auto& v : m.voices) if (v.active) ++count;
    return count;
}

uint32_t AudioEngine::getVirtualVoiceCount() const {
    if (!native) return 0;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    return static_cast<uint32_t>(m.virtualVoices.size());
}

uint32_t AudioEngine::getTotalVoiceCount() const {
    return getActiveVoiceCount() + getVirtualVoiceCount();
}

// ---------------------------------------------------------------------------
// 3D audio
// ---------------------------------------------------------------------------

void AudioEngine::set3dListenerParameters(const ListenerDescriptor& l) {
    if (!native) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    m.listenerPos     = l.position;
    m.listenerVel     = l.velocity;
    m.listenerForward = l.forward;
    m.listenerUp      = l.up;
}

void AudioEngine::set3dListenerParameters(uint32_t listenerIndex,
                                          const ListenerDescriptor& l) {
    if (listenerIndex == 0) set3dListenerParameters(l);
    // TODO(Phase 7): additional listeners.
}

void AudioEngine::set3dSourceParameters(SoundHandle h,
                                        float x, float y, float z) {
    withVoice(native, h, [x, y, z](Voice& v) {
        v.pos = {x, y, z};
        v.is3d = true;
    });
}

void AudioEngine::set3dSourceVelocity(SoundHandle h,
                                      float x, float y, float z) {
    withVoice(native, h, [x, y, z](Voice& v) { v.vel = {x, y, z}; });
}

void AudioEngine::set3dSourceAttenuation(SoundHandle h,
                                         AttenuationModel model, float rolloff) {
    withVoice(native, h, [model, rolloff](Voice& v) {
        v.attenuationModel = model;
        v.rolloff          = rolloff;
    });
}

void AudioEngine::set3dSourceDopplerFactor(SoundHandle h, float factor) {
    withVoice(native, h, [factor](Voice& v) { v.dopplerFactor = factor; });
}

void AudioEngine::set3dSourceMinMaxDistance(SoundHandle h,
                                            float minDist, float maxDist) {
    withVoice(native, h, [minDist, maxDist](Voice& v) {
        v.minDist = minDist;
        v.maxDist = maxDist;
    });
}

void AudioEngine::set3dSoundSpeed(float speedOfSound) {
    if (!native) return;
    data(native)->mixer.soundSpeed = speedOfSound;
}

void AudioEngine::update3d() {
    if (!native) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);

    // Helper that recomputes 3D parameters for one voice (real or virtual).
    auto process = [&](Voice& v) {
        if (!v.active || !v.is3d) return;

        v.attenuationGain = pi::computeAttenuation(
            v.attenuationModel,
            v.pos.distanceTo(m.listenerPos),
            v.minDist, v.maxDist, v.rolloff);

        v.pan = pi::computePan(
            v.pos, m.listenerPos,
            m.listenerForward, m.listenerUp);

        v.dopplerPitch = pi::computeDoppler(
            v.pos, v.vel,
            m.listenerPos, m.listenerVel,
            m.soundSpeed, v.dopplerFactor);
    };

    for (auto& v : m.voices)        process(v);
    for (auto& v : m.virtualVoices) process(v);
}

// ---------------------------------------------------------------------------
// Sidechain / ducking — Phase 9
// ---------------------------------------------------------------------------

void AudioEngine::setSidechain(AudioBus* trigger, AudioBus* target,
                               float duckDb, float attackSecs, float releaseSecs) {
    if (!native || !trigger || !target) return;

    const auto* trgSh = static_cast<const BusSourceHandle*>(trigger->native);
    const auto* tgtSh = static_cast<const BusSourceHandle*>(target->native);
    if (!trgSh || !tgtSh || trgSh->busId == 0 || tgtSh->busId == 0) return;

    auto& m = data(native)->mixer;
    const float sr = static_cast<float>(m.config.sampleRate);

    // Per-buffer attack/release: coefficients computed from per-sample values
    // raised to the power of buffer size.
    const float attSamp = std::exp(-1.0f / (std::max(attackSecs,  0.001f) * sr));
    const float relSamp = std::exp(-1.0f / (std::max(releaseSecs, 0.001f) * sr));

    std::lock_guard lk(m.voiceMutex);
    for (auto& bus : m.buses) {
        if (bus.busId != tgtSh->busId) continue;
        bus.sidechainActive    = true;
        bus.sidechainTriggerId = trgSh->busId;
        bus.sidechainDuckDb    = std::min(duckDb, 0.0f);   // must be ≤ 0
        bus.sidechainAttCoef   = static_cast<double>(attSamp);
        bus.sidechainRelCoef   = static_cast<double>(relSamp);
        bus.sidechainEnvDb     = 0.0;
        break;
    }
}

void AudioEngine::clearSidechain(AudioBus* target) {
    if (!native || !target) return;
    const auto* tgtSh = static_cast<const BusSourceHandle*>(target->native);
    if (!tgtSh || tgtSh->busId == 0) return;

    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    for (auto& bus : m.buses) {
        if (bus.busId != tgtSh->busId) continue;
        bus.sidechainActive    = false;
        bus.sidechainTriggerId = 0;
        bus.sidechainEnvDb     = 0.0;
        break;
    }
}

// ---------------------------------------------------------------------------
// RTPC — Phase 10
// ---------------------------------------------------------------------------

void AudioEngine::registerParameter(std::shared_ptr<AudioParameter> p) {
    if (!native) return;
    pi::engineRegisterParameter(data(native)->parameters, std::move(p));
}

void AudioEngine::unregisterParameter(const std::string& name) {
    if (!native) return;
    pi::engineUnregisterParameter(data(native)->parameters, name);
}

void AudioEngine::setParameter(const std::string& name, float value) {
    if (!native) return;
    pi::engineSetParameter(data(native)->mixer, data(native)->parameters, name, value);
}

float AudioEngine::getParameter(const std::string& name) const {
    if (!native) return 0.0f;
    return pi::engineGetParameter(data(native)->parameters, name);
}

// ---------------------------------------------------------------------------
// Mix snapshots — Phase 11
// ---------------------------------------------------------------------------

void AudioEngine::registerSnapshot(std::shared_ptr<AudioSnapshot> s) {
    if (!native || !s) return;
    data(native)->snapshots.registry[s->getName()] = std::move(s);
}

void AudioEngine::applySnapshot(const std::string& name, double blendTimeSecs) {
    if (!native) return;
    auto* d  = data(native);
    auto& ss = d->snapshots;

    const auto it = ss.registry.find(name);
    if (it == ss.registry.end()) return;

    const auto& snap = it->second;
    const auto* sd = static_cast<const pi::AudioSnapshotData*>(snap->native);

    // Priority check: do not replace an active higher-priority snapshot.
    if (ss.current && !ss.reverting) {
        const auto* cur = static_cast<const pi::AudioSnapshotData*>(ss.current->native);
        if (cur && cur->priority > sd->priority) return;
    }

    auto& m = d->mixer;
    std::lock_guard lk(m.voiceMutex);

    // Helper: resolve busId from AudioBus pointer (friend access via AudioEngine).
    auto busIdOf = [](const AudioBus* bus) -> uint32_t {
        if (!bus) return 0;
        const auto* bsh = static_cast<const pi::BusSourceHandle*>(bus->native);
        return bsh ? bsh->busId : 0;
    };

    // Capture the stable baseline once — the state before any snapshot was ever applied.
    if (!ss.baselineCaptured) {
        ss.baselineGlobal = m.globalVolume;
        ss.baselineBuses.clear();
        for (const auto& e : sd->busVolumes) {
            const uint32_t bid = busIdOf(e.bus);
            if (bid == 0) continue;
            for (const auto& slot : m.buses) {
                if (slot.busId == bid && slot.active) {
                    ss.baselineBuses.push_back({bid, slot.volume});
                    break;
                }
            }
        }
        ss.baselineCaptured = true;
    }

    // Capture "from" values (current live state, possibly mid-blend).
    ss.globalFrom = m.globalVolume;
    ss.busBlends.clear();
    for (const auto& e : sd->busVolumes) {
        const uint32_t bid = busIdOf(e.bus);
        if (bid == 0) continue;
        float curVol = 1.0f;
        for (const auto& slot : m.buses) {
            if (slot.busId == bid && slot.active) { curVol = slot.volume; break; }
        }
        ss.busBlends.push_back({bid, curVol, e.volume});
    }

    ss.globalTo = sd->hasGlobalVolume ? sd->globalVolume : ss.globalFrom;

    // Apply bus filters immediately; save originals for revert.
    ss.savedFilters.clear();
    const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;
    for (const auto& fe : sd->busFilters) {
        const uint32_t bid = busIdOf(fe.bus);
        if (bid == 0) continue;
        for (auto& slot : m.buses) {
            if (slot.busId != bid || !slot.active) continue;
            // Save original.
            ss.savedFilters.push_back({bid, fe.slot, slot.filters[fe.slot]});
            // Install snapshot filter.
            slot.filters[fe.slot] = fe.filter;
            if (fe.filter) {
                const auto* fd = static_cast<const pi::FilterData*>(fe.filter->native);
                if (fd) pi::initFilterState(slot.filterStates[fe.slot], fd->type,
                                            m.config.sampleRate, outCh);
            } else {
                slot.filterStates[fe.slot] = pi::FilterState{};
            }
            break;
        }
    }

    ss.current       = snap;
    ss.blending      = true;
    ss.reverting     = false;
    ss.blendTimeSecs = blendTimeSecs;
    ss.blendElapsed  = 0.0;

    // Zero blend time: apply immediately.
    if (blendTimeSecs <= 0.0) {
        m.globalVolume = ss.globalTo;
        for (const auto& bb : ss.busBlends) {
            for (auto& slot : m.buses) {
                if (slot.busId == bb.busId) { slot.volume = bb.to; break; }
            }
        }
        ss.blending = false;
    }
}

void AudioEngine::revertSnapshot(double blendTimeSecs) {
    if (!native) return;
    auto* d  = data(native);
    auto& ss = d->snapshots;

    if (!ss.baselineCaptured) return;  // nothing to revert

    auto& m = d->mixer;
    const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;
    std::lock_guard lk(m.voiceMutex);

    // Capture "from" values.
    ss.globalFrom = m.globalVolume;
    ss.globalTo   = ss.baselineGlobal;
    ss.busBlends.clear();
    for (const auto& bb : ss.baselineBuses) {
        float curVol = 1.0f;
        for (const auto& slot : m.buses) {
            if (slot.busId == bb.busId && slot.active) { curVol = slot.volume; break; }
        }
        ss.busBlends.push_back({bb.busId, curVol, bb.volume});
    }

    // Restore original filters immediately.
    for (const auto& sf : ss.savedFilters) {
        for (auto& slot : m.buses) {
            if (slot.busId != sf.busId || !slot.active) continue;
            slot.filters[sf.slot] = sf.filter;
            if (sf.filter) {
                const auto* fd = static_cast<const pi::FilterData*>(sf.filter->native);
                if (fd) pi::initFilterState(slot.filterStates[sf.slot], fd->type,
                                            m.config.sampleRate, outCh);
            } else {
                slot.filterStates[sf.slot] = pi::FilterState{};
            }
            break;
        }
    }
    ss.savedFilters.clear();

    ss.current       = nullptr;
    ss.blending      = true;
    ss.reverting     = true;
    ss.blendTimeSecs = blendTimeSecs;
    ss.blendElapsed  = 0.0;

    if (blendTimeSecs <= 0.0) {
        m.globalVolume = ss.baselineGlobal;
        for (const auto& bb : ss.busBlends) {
            for (auto& slot : m.buses) {
                if (slot.busId == bb.busId) { slot.volume = bb.to; break; }
            }
        }
        ss.blending         = false;
        ss.reverting        = false;
        ss.baselineCaptured = false;
    }
}

// ---------------------------------------------------------------------------
// Adaptive music — Phase 13
// ---------------------------------------------------------------------------

void AudioEngine::requestMusicTransition(const std::string& sectionLabel) {
    if (!native || sectionLabel.empty()) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    pi::requestMusicTransition(m, sectionLabel);
}

void AudioEngine::requestPatternTransition(const std::string& sectionLabel) {
    if (!native || sectionLabel.empty()) return;
    auto& m = data(native)->mixer;
    std::lock_guard lk(m.voiceMutex);
    pi::requestPatternTransition(m, sectionLabel);
}

// ---------------------------------------------------------------------------
// Visualization
// ---------------------------------------------------------------------------

void AudioEngine::enableVisualization(bool enable) {
    if (!native) return;
    data(native)->mixer.vizEnabled = enable;
}

const float* AudioEngine::getVisualizationData(uint32_t& sampleCount) const {
    if (!native) { sampleCount = 0; return nullptr; }
    auto& m    = data(native)->mixer;
    sampleCount = static_cast<uint32_t>(m.vizBuffer.size());
    return m.vizBuffer.data();
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

uint32_t AudioEngine::getSampleRate()    const {
    return native ? data(native)->mixer.config.sampleRate : 0;
}
uint32_t AudioEngine::getMaxVoices()     const {
    return native ? data(native)->mixer.config.maxVoices : 0;
}
uint32_t AudioEngine::getChannels()      const {
    return native ? data(native)->mixer.config.channels : 0;
}
uint32_t AudioEngine::getListenerCount() const {
    return native ? data(native)->mixer.config.listenerCount : 0;
}
