/// @file audio_engine.cpp (CoreAudio backend)
/// @brief AudioEngine implementation for macOS and iOS.
///
/// Stub implementation — full backend tracked in TODO.md (Phase 3).

#include <campello_audio/audio_engine.hpp>
#include "common.hpp"
#include <cstring>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::coreaudio;

// ---------------------------------------------------------------------------
// CoreAudio render callback
// ---------------------------------------------------------------------------

OSStatus coreaudio::renderCallback(void*                       inRefCon,
                                   AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                   const AudioTimeStamp*       /*inTimeStamp*/,
                                   UInt32                      /*inBusNumber*/,
                                   UInt32                      inNumberFrames,
                                   AudioBufferList*            ioData) {
    auto* data = static_cast<CoreAudioData*>(inRefCon);

    for (UInt32 buf = 0; buf < ioData->mNumberBuffers; ++buf) {
        auto* out = static_cast<float*>(ioData->mBuffers[buf].mData);
        data->mixer.mixSamples(out, inNumberFrames);
    }
    return noErr;
}

// ---------------------------------------------------------------------------
// AudioEngine public API — CoreAudio backend
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { deinit(); }

bool AudioEngine::init(const AudioEngineDescriptor& descriptor) {
    auto* data    = new CoreAudioData();
    data->mixer.config = descriptor;
    data->mixer.voices.resize(descriptor.maxVoices);
    if (descriptor.visualization) {
        data->mixer.vizBuffer.resize(descriptor.bufferSize * descriptor.channels);
        data->mixer.vizEnabled = true;
    }

    // TODO(Phase 3): open AudioUnit output, set stream format, register
    //   renderCallback, and start the audio unit.
    //   For now this is a no-op stub that compiles cleanly.

    native = data;
    return true;
}

void AudioEngine::deinit() {
    if (!native) return;
    auto* data = static_cast<CoreAudioData*>(native);
    if (data->running) {
        // TODO(Phase 3): AudioOutputUnitStop / AudioUnitUninitialize
        data->running = false;
    }
    delete data;
    native = nullptr;
}

SoundHandle AudioEngine::play(AudioSource& /*source*/) {
    // TODO(Phase 4): allocate voice, decode source, enqueue to mixer.
    return SoundHandle::invalid;
}

SoundHandle AudioEngine::play(AudioSource& source, const PlayDescriptor& /*descriptor*/) {
    return play(source);
}

SoundHandle AudioEngine::play3d(AudioSource& source, float /*x*/, float /*y*/, float /*z*/) {
    return play(source);
}

SoundHandle AudioEngine::playBackground(AudioSource& source) {
    return play(source);
}

void AudioEngine::stop(SoundHandle /*handle*/) {}
void AudioEngine::stopAll() {}
void AudioEngine::pause(SoundHandle /*handle*/) {}
void AudioEngine::resume(SoundHandle /*handle*/) {}

bool AudioEngine::isPaused(SoundHandle /*handle*/) const { return false; }
bool AudioEngine::isValid(SoundHandle /*handle*/)  const { return false; }

void AudioEngine::setVolume(SoundHandle /*h*/, float /*v*/)  {}
void AudioEngine::setPan(SoundHandle /*h*/, float /*p*/)     {}
void AudioEngine::setPitch(SoundHandle /*h*/, float /*p*/)   {}
void AudioEngine::setLooping(SoundHandle /*h*/, bool /*l*/)  {}
void AudioEngine::setProtect(SoundHandle /*h*/, bool /*p*/)  {}

void   AudioEngine::seek(SoundHandle /*h*/, double /*s*/)  {}
double AudioEngine::getPosition(SoundHandle /*h*/) const   { return 0.0; }

void AudioEngine::fadeVolume(SoundHandle /*h*/, float /*to*/, double /*t*/) {}
void AudioEngine::fadePan(SoundHandle /*h*/, float /*to*/, double /*t*/)    {}
void AudioEngine::fadePitch(SoundHandle /*h*/, float /*to*/, double /*t*/)  {}
void AudioEngine::fadeGlobalVolume(float /*to*/, double /*t*/)              {}

void AudioEngine::oscillateVolume(SoundHandle /*h*/, float /*f*/, float /*t*/, double /*s*/) {}
void AudioEngine::oscillatePan(SoundHandle /*h*/, float /*f*/, float /*t*/, double /*s*/)    {}

float    AudioEngine::getGlobalVolume()    const {
    if (!native) return 1.0f;
    return static_cast<CoreAudioData*>(native)->mixer.globalVolume;
}
void     AudioEngine::setGlobalVolume(float v) {
    if (!native) return;
    static_cast<CoreAudioData*>(native)->mixer.globalVolume = v;
}

uint32_t AudioEngine::getActiveVoiceCount() const {
    if (!native) return 0;
    return static_cast<CoreAudioData*>(native)->mixer.activeVoiceCount.load();
}
uint32_t AudioEngine::getTotalVoiceCount() const { return 0; }

void AudioEngine::set3dListenerParameters(const ListenerDescriptor& l) {
    if (!native) return;
    auto& m = static_cast<CoreAudioData*>(native)->mixer;
    m.listenerPos     = l.position;
    m.listenerVel     = l.velocity;
    m.listenerForward = l.forward;
    m.listenerUp      = l.up;
}
void AudioEngine::set3dSourceParameters(SoundHandle /*h*/, float /*x*/, float /*y*/, float /*z*/) {}
void AudioEngine::set3dSourceVelocity(SoundHandle /*h*/, float /*x*/, float /*y*/, float /*z*/)   {}
void AudioEngine::set3dSourceAttenuation(SoundHandle /*h*/, AttenuationModel /*m*/, float /*r*/)  {}
void AudioEngine::set3dSourceDopplerFactor(SoundHandle /*h*/, float /*f*/)                        {}
void AudioEngine::set3dSourceMinMaxDistance(SoundHandle /*h*/, float /*mn*/, float /*mx*/)        {}
void AudioEngine::set3dSoundSpeed(float s) {
    if (!native) return;
    static_cast<CoreAudioData*>(native)->mixer.soundSpeed = s;
}
void AudioEngine::update3d() {}

void AudioEngine::enableVisualization(bool enable) {
    if (!native) return;
    static_cast<CoreAudioData*>(native)->mixer.vizEnabled = enable;
}
const float* AudioEngine::getVisualizationData(uint32_t& sampleCount) const {
    if (!native) { sampleCount = 0; return nullptr; }
    auto& m = static_cast<CoreAudioData*>(native)->mixer;
    sampleCount = static_cast<uint32_t>(m.vizBuffer.size());
    return m.vizBuffer.data();
}

uint32_t AudioEngine::getSampleRate() const {
    if (!native) return 0;
    return static_cast<CoreAudioData*>(native)->mixer.config.sampleRate;
}
uint32_t AudioEngine::getMaxVoices() const {
    if (!native) return 0;
    return static_cast<CoreAudioData*>(native)->mixer.config.maxVoices;
}
uint32_t AudioEngine::getChannels() const {
    if (!native) return 0;
    return static_cast<CoreAudioData*>(native)->mixer.config.channels;
}
