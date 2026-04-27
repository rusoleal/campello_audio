/// @file audio_engine.cpp (WASM backend)
/// @brief AudioEngine implementation for WebAssembly (Emscripten).
///
/// Uses Emscripten's Web Audio AudioWorklet API for audio output.
/// Requires -pthread and SharedArrayBuffer browser support.
///
/// ### Device setup
///   1. Create a Web Audio AudioContext via emscripten_create_audio_context().
///   2. Start the WASM AudioWorklet thread.
///   3. Create an AudioWorkletProcessor and instantiate it as a node.
///   4. Connect the node to the audio context destination.
///   5. The process() callback calls MixerData::mixSamples() each 128-frame block.
///
/// ### Playback pipeline
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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::wasm;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Cast an AudioEngine's opaque native pointer to WasmData.
static inline WasmData* data(void* native) {
    return static_cast<WasmData*>(native);
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
// AudioWorklet process callback — audio thread
// ---------------------------------------------------------------------------

EM_BOOL wasm::processCallback(int /*numInputs*/,
                              const AudioSampleFrame* /*inputs*/,
                              int numOutputs,
                              AudioSampleFrame* outputs,
                              int /*numParams*/,
                              const AudioParamFrame* /*params*/,
                              void* userData) {
    auto* d = static_cast<WasmData*>(userData);
    if (!d || !d->running.load(std::memory_order_acquire)) {
        for (int o = 0; o < numOutputs; ++o) {
            if (outputs[o].data) {
                std::memset(outputs[o].data, 0,
                            outputs[o].numberOfChannels * 128 * sizeof(float));
            }
        }
        return EM_TRUE;
    }

    const uint32_t outCh = d->mixer.config.channels > 0 ? d->mixer.config.channels : 2u;
    constexpr uint32_t kBlockFrames = 128;

    // Ensure scratch buffer is large enough for interleaved mixing.
    const uint32_t needed = kBlockFrames * outCh;
    if (d->processScratch.size() < needed) {
        d->processScratch.resize(needed);
    }

    // Mix in interleaved format.
    d->mixer.mixSamples(d->processScratch.data(), kBlockFrames);

    // Deinterleave to Web Audio's planar format.
    for (int o = 0; o < numOutputs; ++o) {
        const int chCount = outputs[o].numberOfChannels;
        for (uint32_t f = 0; f < kBlockFrames; ++f) {
            for (int ch = 0; ch < chCount; ++ch) {
                if (ch < static_cast<int>(outCh)) {
                    outputs[o].data[ch * 128 + f] = d->processScratch[f * outCh + ch];
                } else {
                    outputs[o].data[ch * 128 + f] = 0.0f;
                }
            }
        }
    }

    return EM_TRUE;
}

// ---------------------------------------------------------------------------
// AudioWorklet async setup callbacks
// ---------------------------------------------------------------------------

void wasm::workletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T audioContext,
                                   EM_BOOL success,
                                   void* userData) {
    auto* d = static_cast<WasmData*>(userData);
    if (!d || d->destroyed.load()) return;
    if (!success) return;

    d->setupState.store(2);

    const int outputCh = static_cast<int>(
        d->mixer.config.channels > 0 ? d->mixer.config.channels : 2u
    );
    int outputChannelCounts[1] = { outputCh };

    EmscriptenAudioWorkletNodeCreateOptions options = {
        .numberOfInputs = 0,
        .numberOfOutputs = 1,
        .outputChannelCounts = outputChannelCounts
    };

    d->audioNode = emscripten_create_wasm_audio_worklet_node(
        audioContext, "campello-audio", &options, &processCallback, d
    );

    d->setupState.store(3);
    d->running.store(true, std::memory_order_release);

    // Connect the node to the audio context destination.
    EM_ASM({
        let ctx = emscriptenGetAudioObject($0);
        let node = emscriptenGetAudioObject($1);
        node.connect(ctx.destination);
    }, audioContext, d->audioNode);
}

void wasm::workletThreadInitialized(EMSCRIPTEN_WEBAUDIO_T audioContext,
                                    EM_BOOL success,
                                    void* userData) {
    auto* d = static_cast<WasmData*>(userData);
    if (!d || d->destroyed.load()) return;
    if (!success) return;

    d->setupState.store(1);

    WebAudioWorkletProcessorCreateOptions opts = {
        .name = "campello-audio",
    };
    emscripten_create_wasm_audio_worklet_processor_async(
        audioContext, &opts, workletProcessorCreated, d
    );
}

// ---------------------------------------------------------------------------
// AudioEngine — lifecycle
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { deinit(); }

bool AudioEngine::init(const AudioEngineDescriptor& descriptor) {
    auto* d = new WasmData();
    d->mixer.config = descriptor;
    VoiceManager::init(d->mixer);

    if (descriptor.visualization) {
        d->mixer.vizBuffer.resize(descriptor.bufferSize * descriptor.channels);
        d->mixer.vizEnabled = true;
    }

    EmscriptenWebAudioCreateAttributes attrs = {
        .latencyHint = "interactive",
        .sampleRate = descriptor.sampleRate
    };

    d->audioContext = emscripten_create_audio_context(&attrs);
    if (!d->audioContext) {
        delete d;
        return false;
    }

    // Allocate a 16-byte aligned stack for the AudioWorklet thread.
    d->workletStack.resize(4096 + 15);
    void* stackBase = reinterpret_cast<void*>(
        (reinterpret_cast<uintptr_t>(d->workletStack.data()) + 15) & ~static_cast<uintptr_t>(15)
    );

    emscripten_start_wasm_audio_worklet_thread_async(
        d->audioContext,
        stackBase,
        4096,
        &workletThreadInitialized,
        d
    );

    native = d;
    return true;
}

void AudioEngine::deinit() {
    if (!native) return;
    auto* d = data(native);

    d->running.store(false, std::memory_order_release);
    d->destroyed.store(true, std::memory_order_release);

    if (d->audioNode) {
        emscripten_destroy_web_audio_node(d->audioNode);
        d->audioNode = 0;
    }
    if (d->audioContext) {
        emscripten_destroy_audio_context(d->audioContext);
        d->audioContext = 0;
    }

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
// Internal helpers — voice allocation
// ---------------------------------------------------------------------------

/// Shared voice-allocation logic used by both play() and playOnBus().
/// @p sh    Pre-extracted source handle (caller has friend access to get it).
/// @p busId 0 = main mix; non-zero = route to that bus slot.
static SoundHandle doPlay(void* nativePtr, const AudioSourceHandle* sh,
                           const PlayDescriptor& pd, uint32_t busId) {
    if (!nativePtr || !sh) return SoundHandle::invalid;
    if (sh->type == SourceType::Bus) return SoundHandle::invalid;
    auto& m = data(nativePtr)->mixer;

    std::lock_guard lk(m.voiceMutex);

    if (sh->singleInstance) {
        for (auto& v : m.voices) {
            if (v.active && v.buffer == sh->pcmBuffer && sh->pcmBuffer)
                v.active = false;
        }
    }

    Voice* v = VoiceManager::allocate(m);
    if (!v) return SoundHandle::invalid;

    if (sh->pcmBuffer) {
        if (!sh->pcmBuffer->isValid()) { v->active = false; return SoundHandle::invalid; }
        v->buffer = sh->pcmBuffer;
    } else if (sh->type == SourceType::Tone) {
        const auto* th = static_cast<const ToneSourceHandle*>(sh);
        v->ownedBuffer = std::make_unique<DecodedBuffer>(
            generateToneBuffer(th->waveform, th->frequency,
                               m.config.sampleRate, m.config.channels));
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

    const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;
    v->filters = sh->filters;
    for (uint32_t s = 0; s < v->filters.size(); ++s) {
        if (!v->filters[s]) continue;
        const auto* fd = static_cast<const pi::FilterData*>(v->filters[s]->native);
        if (fd) pi::initFilterState(v->filterStates[s], fd->type,
                                    m.config.sampleRate, outCh);
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

/// Called by AudioBus::play() (via BusSourceHandle::playFn) to route a child
/// source through a specific bus slot.
static SoundHandle playOnBus(void* nativePtr, const AudioSourceHandle* sh,
                              const PlayDescriptor& pd, uint32_t busId) {
    return doPlay(nativePtr, sh, pd, busId);
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

    const auto* sh = static_cast<const AudioSourceHandle*>(source.native);
    if (!sh) return SoundHandle::invalid;

    // -----------------------------------------------------------------
    // MusicTrack branch — register a beat-clock player in the mixer.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Music) {
        auto* mh = static_cast<MusicTrackHandle*>(source.native);
        auto& trackData = mh->data;

        auto& m = data(native)->mixer;
        std::lock_guard lk(m.voiceMutex);

        // Resolve PCM pointers for every section now that we're at play time.
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

        // Register (or replace) an entry for this player.
        auto it = std::find(m.musicPlayers.begin(), m.musicPlayers.end(), &trackData);
        if (it == m.musicPlayers.end())
            m.musicPlayers.push_back(&trackData);

        return SoundHandle::invalid;  // music tracks have no voice handle
    }

    // -----------------------------------------------------------------
    // AudioStream branch — stream from a ring-buffer prefetch thread.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Stream) {
        auto* ssh = static_cast<pi::AudioStreamHandle*>(source.native);
        if (!ssh || !ssh->data || !ssh->data->isOpen) return SoundHandle::invalid;

        std::lock_guard lk(m.voiceMutex);

        Voice* v = VoiceManager::allocate(m);
        if (!v) return SoundHandle::invalid;

        v->buffer  = nullptr;
        v->stream  = ssh->data;
        v->volume  = ssh->volume * pd.volume;
        v->pan     = pd.pan;
        v->pitch   = 1.0f;  // no pitch shifting for streams in Phase 14
        v->paused  = pd.paused;
        v->protect = pd.protect;
        v->busId   = 0;
        v->loopMode = (pd.looping || ssh->looping) ? LoopMode::Loop : LoopMode::None;

        // Propagate looping to stream data so the prefetch thread loops correctly
        ssh->data->looping = pd.looping || ssh->looping;

        return SoundHandle{v->id};
    }

    // -----------------------------------------------------------------
    // RandomSource branch — select a weighted-random variant and
    // delegate to play() on the chosen source with pitch/volume offsets.
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

        return play(*rsh->variants[idx].source, modPd);
    }

    // -----------------------------------------------------------------
    // AudioBus branch — register a new BusSlot in the mixer.
    // -----------------------------------------------------------------
    if (sh->type == SourceType::Bus) {
        auto* bsh = static_cast<BusSourceHandle*>(source.native);
        const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;

        std::lock_guard lk(m.voiceMutex);

        BusSlot slot;
        slot.busId  = m.nextBusId++;
        slot.active = true;
        slot.volume = sh->volume;
        slot.filters = sh->filters;
        for (uint32_t s = 0; s < slot.filters.size(); ++s) {
            if (!slot.filters[s]) continue;
            const auto* fd = static_cast<const pi::FilterData*>(slot.filters[s]->native);
            if (fd) pi::initFilterState(slot.filterStates[s], fd->type,
                                        m.config.sampleRate, outCh);
        }

        const uint32_t busId = slot.busId;
        m.buses.push_back(std::move(slot));

        // Give the bus a back-pointer and a play callback.
        bsh->busId     = busId;
        bsh->mixerData = &m;
        bsh->playFn    = [nativePtr = native](const AudioSourceHandle* sh,
                                              const PlayDescriptor& p,
                                              uint32_t bid) -> SoundHandle {
            return playOnBus(nativePtr, sh, p, bid);
        };

        return SoundHandle::invalid;  // buses have no voice handle
    }

    return doPlay(native, sh, pd, 0);
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
    if (!native || !p) return;
    auto* d = data(native);
    d->parameters[p->getName()] = std::move(p);
}

void AudioEngine::unregisterParameter(const std::string& name) {
    if (!native) return;
    data(native)->parameters.erase(name);
}

/// Apply all bindings for @p paramName to every active (and virtual) voice.
/// Called from setParameter() — must be called under voiceMutex.
static void applyRtpcToVoices(pi::MixerData& m,
                               const std::string& paramName,
                               float value, float minVal, float maxVal) {
    auto applyOne = [&](pi::Voice& v) {
        if (!v.active) return;
        for (const auto& b : v.bindings) {
            if (b.paramName != paramName) continue;
            const float mapped = pi::applyBinding(value, minVal, maxVal, b);
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
                    // Convenience: drive PARAM_CUTOFF on slot 0.
                    if (v.filters[0]) v.filters[0]->setParam(0, mapped);
                    break;
                case AudioSourceProperty::HighPassCutoff:
                    if (v.filters[0]) v.filters[0]->setParam(0, mapped);
                    break;
                case AudioSourceProperty::SendLevel:
                    // SendLevel — not yet wired to bus send gains; no-op for now.
                    break;
            }
        }
    };

    for (auto& v : m.voices)        applyOne(v);
    for (auto& v : m.virtualVoices) applyOne(v);
}

void AudioEngine::setParameter(const std::string& name, float value) {
    if (!native) return;
    auto* d = data(native);

    auto it = d->parameters.find(name);
    if (it == d->parameters.end()) return;

    auto& param = *it->second;
    param.setValue(value);   // clamped inside AudioParameter::setValue

    auto& m = d->mixer;
    std::lock_guard lk(m.voiceMutex);
    applyRtpcToVoices(m, name, param.getValue(), param.getMinValue(), param.getMaxValue());
}

float AudioEngine::getParameter(const std::string& name) const {
    if (!native) return 0.0f;
    const auto& params = data(native)->parameters;
    const auto it = params.find(name);
    return (it != params.end()) ? it->second->getValue() : 0.0f;
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

    // Apply to every active music player that contains a section with this label.
    for (auto* player : m.musicPlayers) {
        if (!player || !player->active) continue;
        const int32_t idx = player->findSection(sectionLabel);
        if (idx < 0) continue;
        // Do not queue a transition to the section already playing.
        if (idx == player->currentSection) continue;
        player->pendingSection = idx;
    }
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
