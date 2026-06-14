/// @file audio_engine.cpp (WASAPI backend)
/// @brief AudioEngine implementation for Windows via WASAPI shared mode.
///
/// ### Device setup
///   1. CoInitializeEx — COM apartment initialisation.
///   2. IMMDeviceEnumerator → GetDefaultAudioEndpoint → IMMDevice.
///   3. IMMDevice::Activate → IAudioClient.
///   4. Format negotiation: request Float32 at the user's sample rate;
///      fall back to GetMixFormat if unsupported.
///   5. IAudioClient::Initialize (SHARED, event-driven).
///   6. IAudioClient::GetService → IAudioRenderClient.
///   7. Spin render thread (MMCSS "Pro Audio" priority);
///      WaitForSingleObject(hEvent) → GetBuffer → mixSamples → ReleaseBuffer.
///
/// ### Device invalidation
///   The render thread detects AUDCLNT_E_DEVICE_INVALIDATED (headphone unplug,
///   default device change) and sets WASAPIData::deviceLost.  The engine
///   continues to run in a degraded (silent) state until deinit() is called.
///   Applications should poll isDeviceLost() and call deinit()/init() to recover.

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
using namespace systems::leal::campello_audio::wasapi;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline WASAPIData* data(void* native) {
    return static_cast<WASAPIData*>(native);
}

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
// Format helpers
// ---------------------------------------------------------------------------

/// True if the WAVEFORMATEX describes 32-bit IEEE float PCM.
static bool isFloat32Format(const WAVEFORMATEX* wfx) {
    if (!wfx) return false;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32)
        return true;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        wfx->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        static const GUID kFloat32Sub = {
            0x00000003, 0x0000, 0x0010,
            {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
        };
        return (IsEqualGUID(ext->SubFormat, kFloat32Sub) != 0) &&
               ext->Format.wBitsPerSample == 32;
    }
    return false;
}

/// Build a WAVEFORMATEXTENSIBLE for Float32 interleaved PCM.
static WAVEFORMATEXTENSIBLE makeFloat32Format(uint32_t sampleRate,
                                              uint32_t channels) {
    // Standard Windows speaker masks.
    static const DWORD kMasks[] = {
        0,                          // 0 channels (unused)
        0x00000004,                 // 1: FRONT_CENTER
        0x00000003,                 // 2: FL | FR
        0,                          // 3: unused
        0,                          // 4: unused
        0,                          // 5: unused
        0x0000003F,                 // 6 (5.1): FL|FR|FC|LFE|BL|BR
        0,                          // 7: unused
        0x0000063F,                 // 8 (7.1): FL|FR|FC|LFE|BL|BR|SL|SR
    };
    const DWORD mask = (channels < 9) ? kMasks[channels] : 0;

    WAVEFORMATEXTENSIBLE wfx = {};
    wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels       = static_cast<WORD>(channels);
    wfx.Format.nSamplesPerSec  = sampleRate;
    wfx.Format.wBitsPerSample  = 32;
    wfx.Format.nBlockAlign     = static_cast<WORD>(channels * 4);
    wfx.Format.nAvgBytesPerSec = sampleRate * channels * 4;
    wfx.Format.cbSize          =
        static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = mask;
    // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
    wfx.SubFormat = {0x00000003, 0x0000, 0x0010,
                     {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    return wfx;
}

// ---------------------------------------------------------------------------
// Render thread
// ---------------------------------------------------------------------------

void wasapi::renderProc(WASAPIData* d) {
    // Elevate to MMCSS "Pro Audio" class for lowest latency.
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(L"Pro Audio", &taskIndex);

    while (d->running.load(std::memory_order_relaxed)) {
        const DWORD waitResult = WaitForSingleObject(d->hEvent, 200 /*ms*/);

        if (!d->running.load(std::memory_order_relaxed)) break;
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0) break;  // unexpected error

        UINT32 padding = 0;
        HRESULT hr = d->audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            if (hr == static_cast<HRESULT>(AUDCLNT_E_DEVICE_INVALIDATED))
                d->deviceLost.store(true, std::memory_order_release);
            break;
        }

        const UINT32 available = d->bufferFrames - padding;
        if (available == 0) continue;

        BYTE* pData = nullptr;
        hr = d->renderClient->GetBuffer(available, &pData);
        if (FAILED(hr)) {
            if (hr == static_cast<HRESULT>(AUDCLNT_E_DEVICE_INVALIDATED))
                d->deviceLost.store(true, std::memory_order_release);
            break;
        }

        d->mixer.mixSamples(reinterpret_cast<float*>(pData), available);
        d->renderClient->ReleaseBuffer(available, 0);
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
}

// ---------------------------------------------------------------------------
// AudioEngine — lifecycle
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { deinit(); }

bool AudioEngine::init(const AudioEngineDescriptor& descriptor) {
    auto* d = new WASAPIData();
    d->mixer.config = descriptor;
    VoiceManager::init(d->mixer);

    if (descriptor.visualization) {
        d->mixer.vizBuffer.resize(descriptor.bufferSize * descriptor.channels);
        d->mixer.vizEnabled = true;
    }

    // ---- COM initialisation ------------------------------------------------
    // COINIT_APARTMENTTHREADED is standard for game-thread callers.
    // Ignore RPC_E_CHANGED_MODE — COM already initialised by the caller is fine.
    const HRESULT hrCom = CoInitializeEx(nullptr,
                                         COINIT_APARTMENTTHREADED |
                                         COINIT_DISABLE_OLE1DDE);
    d->mixer.config = descriptor;  // keep a copy before any mutation

    // ---- Device enumeration -----------------------------------------------
    IMMDeviceEnumerator* pEnum = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&pEnum)))) {
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }

    IMMDevice* pDevice = nullptr;
    if (FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) {
        pEnum->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }
    pEnum->Release();

    // ---- Audio client -------------------------------------------------------
    IAudioClient* pClient = nullptr;
    if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                  nullptr,
                                  reinterpret_cast<void**>(&pClient)))) {
        pDevice->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }
    pDevice->Release();

    // ---- Format negotiation -------------------------------------------------
    // Try our preferred Float32 format; fall back to the device mix format.
    WAVEFORMATEXTENSIBLE wfxPreferred =
        makeFloat32Format(descriptor.sampleRate, descriptor.channels);

    WAVEFORMATEX* pClosest = nullptr;
    const HRESULT hrFmt = pClient->IsFormatSupported(
        AUDCLNT_SHAREMODE_SHARED,
        &wfxPreferred.Format, &pClosest);

    WAVEFORMATEX*        pUse     = &wfxPreferred.Format;
    WAVEFORMATEX*        pMixFmt  = nullptr;  // from GetMixFormat, freed below

    if (FAILED(hrFmt)) {
        // Preferred format rejected — ask the device what it prefers.
        if (FAILED(pClient->GetMixFormat(&pMixFmt)) || !isFloat32Format(pMixFmt)) {
            // Device does not expose a Float32 mix format.
            if (pMixFmt) CoTaskMemFree(pMixFmt);
            if (pClosest) CoTaskMemFree(pClosest);
            pClient->Release();
            if (SUCCEEDED(hrCom)) CoUninitialize();
            delete d; return false;
        }
        pUse = pMixFmt;
    } else if (hrFmt == S_FALSE && pClosest) {
        // The device can do Float32 but at a different sample rate / channel count.
        if (!isFloat32Format(pClosest)) {
            CoTaskMemFree(pClosest);
            if (SUCCEEDED(hrCom)) CoUninitialize();
            pClient->Release();
            delete d; return false;
        }
        pUse = pClosest;
    }

    // Update mixer config to the actual format we'll use.
    d->mixer.config.sampleRate = pUse->nSamplesPerSec;
    d->mixer.config.channels   = pUse->nChannels;

    // ---- IAudioClient::Initialize (event-driven shared mode) ----------------
    const REFERENCE_TIME kBufferDuration =
        static_cast<REFERENCE_TIME>(10000000.0
            * descriptor.bufferSize / d->mixer.config.sampleRate);

    HRESULT hr = pClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        kBufferDuration, 0, pUse, nullptr);

    if (pClosest) CoTaskMemFree(pClosest);
    if (pMixFmt)  CoTaskMemFree(pMixFmt);

    if (FAILED(hr)) {
        pClient->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }

    // ---- Get actual buffer size & render client ----------------------------
    UINT32 bufFrames = 0;
    pClient->GetBufferSize(&bufFrames);
    d->bufferFrames = bufFrames;

    IAudioRenderClient* pRender = nullptr;
    if (FAILED(pClient->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&pRender)))) {
        pClient->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }

    // ---- Event handle -------------------------------------------------------
    HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent || FAILED(pClient->SetEventHandle(hEvent))) {
        if (hEvent) CloseHandle(hEvent);
        pRender->Release();
        pClient->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }

    d->audioClient  = pClient;
    d->renderClient = pRender;
    d->hEvent       = hEvent;

    // ---- Pre-fill silence then start ----------------------------------------
    {
        BYTE* pData = nullptr;
        if (SUCCEEDED(pRender->GetBuffer(bufFrames, &pData))) {
            std::memset(pData, 0, bufFrames * d->mixer.config.channels * sizeof(float));
            pRender->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    if (FAILED(pClient->Start())) {
        CloseHandle(hEvent);
        pRender->Release();
        pClient->Release();
        if (SUCCEEDED(hrCom)) CoUninitialize();
        delete d; return false;
    }

    // ---- Spin render thread -------------------------------------------------
    d->running.store(true, std::memory_order_release);
    d->renderThread = std::thread(renderProc, d);

    native = d;
    return true;
}

void AudioEngine::deinit() {
    if (!native) return;
    auto* d = data(native);

    // Signal the render thread to stop and wait for it.
    d->running.store(false, std::memory_order_release);
    if (d->hEvent) SetEvent(d->hEvent);  // wake the thread so it can exit
    if (d->renderThread.joinable()) d->renderThread.join();

    if (d->audioClient) {
        d->audioClient->Stop();
        d->audioClient->Reset();
    }

    if (d->renderClient) { d->renderClient->Release(); d->renderClient = nullptr; }
    if (d->audioClient)  { d->audioClient->Release();  d->audioClient  = nullptr; }
    if (d->hEvent)       { CloseHandle(d->hEvent);     d->hEvent       = nullptr; }

    CoUninitialize();

    delete d;
    native = nullptr;
}

// ---------------------------------------------------------------------------
// tick — game thread: async loads + snapshot blend
// ---------------------------------------------------------------------------

void AudioEngine::tick() {
    if (!native) return;
    auto* d = data(native);

    for (auto it = d->pendingLoads.begin(); it != d->pendingLoads.end(); ) {
        const auto status = it->future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready ||
            status == std::future_status::deferred) {
            bool ok = false;
            try {
                auto bytes = it->future.get();
                if (!bytes.empty()) {
                    auto* sh = static_cast<pi::AudioSourceHandle*>(it->source->native);
                    if (sh && sh->decodeBytes) ok = sh->decodeBytes(bytes);
                }
            } catch (...) { ok = false; }
            if (it->onDone) it->onDone(ok);
            it = d->pendingLoads.erase(it);
        } else {
            ++it;
        }
    }

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
            const float ft = static_cast<float>(t);

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
                    ss.baselineCaptured = false;
                }
            }
        }
    }
    d->lastTickTime      = now;
    d->lastTickTimeValid = true;
}

void AudioEngine::loadAsync(AudioSource& source, AsyncByteLoader loader,
                            LoadCallback onDone) {
    if (!native || !loader) return;
    data(native)->pendingLoads.push_back({
        &source, loader(), std::move(onDone)
    });
}

// ---------------------------------------------------------------------------
// Internal — voice allocation
// ---------------------------------------------------------------------------

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
SoundHandle AudioEngine::play3d(AudioSource& source, float x, float y, float z) {
    PlayDescriptor pd;
    pd.enable3d = true;
    pd.position = {x, y, z};
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
        if (v.stream) { v.stream->requestSeek(seconds); return; }
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
        v.fadeVolFrom       = v.volume;
        v.fadeVolTo         = to;
        v.fadeVolTime       = timeSecs;
        v.fadeVolElapsed    = 0.0;
        v.fadeVolActive     = true;
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

void AudioEngine::oscillateVolume(SoundHandle h, float from, float to,
                                  double periodSecs) {
    withVoice(native, h, [from, to, periodSecs](Voice& v) {
        v.lfoVolFrom   = from;
        v.lfoVolTo     = to;
        v.lfoVolPeriod = periodSecs;
        v.lfoVolPhase  = 0.0;
        v.lfoVolActive = true;
    });
}

void AudioEngine::oscillatePan(SoundHandle h, float from, float to,
                               double periodSecs) {
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
    uint32_t n = 0;
    for (const auto& v : m.voices) if (v.active) ++n;
    return n;
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
}

void AudioEngine::set3dSourceParameters(SoundHandle h, float x, float y, float z) {
    withVoice(native, h, [x, y, z](Voice& v) { v.pos = {x, y, z}; v.is3d = true; });
}

void AudioEngine::set3dSourceVelocity(SoundHandle h, float x, float y, float z) {
    withVoice(native, h, [x, y, z](Voice& v) { v.vel = {x, y, z}; });
}

void AudioEngine::set3dSourceAttenuation(SoundHandle h, AttenuationModel model,
                                         float rolloff) {
    withVoice(native, h, [model, rolloff](Voice& v) {
        v.attenuationModel = model;
        v.rolloff          = rolloff;
    });
}

void AudioEngine::set3dSourceDopplerFactor(SoundHandle h, float factor) {
    withVoice(native, h, [factor](Voice& v) { v.dopplerFactor = factor; });
}

void AudioEngine::set3dSourceMinMaxDistance(SoundHandle h, float minDist,
                                            float maxDist) {
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

    auto process = [&](Voice& v) {
        if (!v.active || !v.is3d) return;
        v.attenuationGain = pi::computeAttenuation(
            v.attenuationModel,
            v.pos.distanceTo(m.listenerPos),
            v.minDist, v.maxDist, v.rolloff);
        v.pan = pi::computePan(v.pos, m.listenerPos,
                               m.listenerForward, m.listenerUp);
        v.dopplerPitch = pi::computeDoppler(
            v.pos, v.vel, m.listenerPos, m.listenerVel,
            m.soundSpeed, v.dopplerFactor);
    };

    for (auto& v : m.voices)        process(v);
    for (auto& v : m.virtualVoices) process(v);
}

// ---------------------------------------------------------------------------
// Sidechain / ducking
// ---------------------------------------------------------------------------

void AudioEngine::setSidechain(AudioBus* trigger, AudioBus* target,
                               float duckDb, float attackSecs,
                               float releaseSecs) {
    if (!native || !trigger || !target) return;
    const auto* trgSh = static_cast<const BusSourceHandle*>(trigger->native);
    const auto* tgtSh = static_cast<const BusSourceHandle*>(target->native);
    if (!trgSh || !tgtSh || trgSh->busId == 0 || tgtSh->busId == 0) return;

    auto& m = data(native)->mixer;
    const float sr = static_cast<float>(m.config.sampleRate);
    const float attSamp = std::exp(-1.0f / (std::max(attackSecs,  0.001f) * sr));
    const float relSamp = std::exp(-1.0f / (std::max(releaseSecs, 0.001f) * sr));

    std::lock_guard lk(m.voiceMutex);
    for (auto& bus : m.buses) {
        if (bus.busId != tgtSh->busId) continue;
        bus.sidechainActive    = true;
        bus.sidechainTriggerId = trgSh->busId;
        bus.sidechainDuckDb    = std::min(duckDb, 0.0f);
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
// RTPC
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
// Mix snapshots
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

    if (ss.current && !ss.reverting) {
        const auto* cur = static_cast<const pi::AudioSnapshotData*>(ss.current->native);
        if (cur && cur->priority > sd->priority) return;
    }

    auto& m = d->mixer;
    std::lock_guard lk(m.voiceMutex);

    auto busIdOf = [](const AudioBus* bus) -> uint32_t {
        if (!bus) return 0;
        const auto* bsh = static_cast<const pi::BusSourceHandle*>(bus->native);
        return bsh ? bsh->busId : 0;
    };

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

    ss.globalFrom = m.globalVolume;
    ss.busBlends.clear();
    for (const auto& e : sd->busVolumes) {
        const uint32_t bid = busIdOf(e.bus);
        if (bid == 0) continue;
        float curVol = 1.0f;
        for (const auto& slot : m.buses)
            if (slot.busId == bid && slot.active) { curVol = slot.volume; break; }
        ss.busBlends.push_back({bid, curVol, e.volume});
    }
    ss.globalTo = sd->hasGlobalVolume ? sd->globalVolume : ss.globalFrom;

    ss.savedFilters.clear();
    const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;
    for (const auto& fe : sd->busFilters) {
        const uint32_t bid = busIdOf(fe.bus);
        if (bid == 0) continue;
        for (auto& slot : m.buses) {
            if (slot.busId != bid || !slot.active) continue;
            ss.savedFilters.push_back({bid, fe.slot, slot.filters[fe.slot]});
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

    if (blendTimeSecs <= 0.0) {
        m.globalVolume = ss.globalTo;
        for (const auto& bb : ss.busBlends)
            for (auto& slot : m.buses)
                if (slot.busId == bb.busId) { slot.volume = bb.to; break; }
        ss.blending = false;
    }
}

void AudioEngine::revertSnapshot(double blendTimeSecs) {
    if (!native) return;
    auto* d  = data(native);
    auto& ss = d->snapshots;
    if (!ss.baselineCaptured) return;

    auto& m = d->mixer;
    const uint32_t outCh = m.config.channels > 0 ? m.config.channels : 2u;
    std::lock_guard lk(m.voiceMutex);

    ss.globalFrom = m.globalVolume;
    ss.globalTo   = ss.baselineGlobal;
    ss.busBlends.clear();
    for (const auto& bb : ss.baselineBuses) {
        float curVol = 1.0f;
        for (const auto& slot : m.buses)
            if (slot.busId == bb.busId && slot.active) { curVol = slot.volume; break; }
        ss.busBlends.push_back({bb.busId, curVol, bb.volume});
    }

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
        for (const auto& bb : ss.busBlends)
            for (auto& slot : m.buses)
                if (slot.busId == bb.busId) { slot.volume = bb.to; break; }
        ss.blending         = false;
        ss.reverting        = false;
        ss.baselineCaptured = false;
    }
}

// ---------------------------------------------------------------------------
// Adaptive music
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
