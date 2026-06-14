#pragma once

#include <campello_audio/types/sound_handle.hpp>
#include <campello_audio/descriptors/play_descriptor.hpp>
#include <string>
#include <unordered_map>
#include <memory>

namespace systems::leal::campello_audio {
class AudioSource;
class AudioParameter;
}

namespace systems::leal::campello_audio::pi {

struct MixerData;

/// @brief Platform-independent source-type dispatch for AudioEngine::play().
///
/// Extracted from the per-backend audio_engine.cpp files to eliminate
/// duplication.  Called by every backend with voiceMutex already locked.
SoundHandle playSourceDispatch(MixerData& mx, AudioSource& source,
                               const PlayDescriptor& pd);

/// @brief Shared voice-allocation logic used by playSourceDispatch and bus routing.
SoundHandle doPlay(MixerData& mx, const struct AudioSourceHandle* sh,
                   const PlayDescriptor& pd, uint32_t busId);

/// @brief Request a music transition on every active MusicTrack player.
void requestMusicTransition(MixerData& mx, const std::string& sectionLabel);

/// @brief Request a pattern transition on every active PatternTrack player.
void requestPatternTransition(MixerData& mx, const std::string& sectionLabel);

// ---------------------------------------------------------------------------
// RTPC helpers — extracted from per-backend audio_engine.cpp files.
// ---------------------------------------------------------------------------

void engineRegisterParameter(
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    std::shared_ptr<AudioParameter> p);

void engineUnregisterParameter(
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name);

void engineSetParameter(
    MixerData& mx,
    std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name, float value);

float engineGetParameter(
    const std::unordered_map<std::string, std::shared_ptr<AudioParameter>>& params,
    const std::string& name);

} // namespace systems::leal::campello_audio::pi
