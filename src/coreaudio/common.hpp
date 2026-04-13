#pragma once

/// macOS / iOS CoreAudio backend — internal header.
/// Not included by any public API header.

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <atomic>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include "../../src/pi/mixer.hpp"

namespace systems::leal::campello_audio::coreaudio {

/// All CoreAudio state for one AudioEngine instance.
struct CoreAudioData {
    AudioComponentInstance  audioUnit    = nullptr;
    bool                    running      = false;

    /// Platform-independent mixer — the callback writes into this.
    pi::MixerData           mixer;
};

/// AudioUnit render callback.
OSStatus renderCallback(void*                       inRefCon,
                        AudioUnitRenderActionFlags* ioActionFlags,
                        const AudioTimeStamp*       inTimeStamp,
                        UInt32                      inBusNumber,
                        UInt32                      inNumberFrames,
                        AudioBufferList*            ioData);

} // namespace systems::leal::campello_audio::coreaudio
