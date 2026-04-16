#pragma once

#include <cstdint>
#include <campello_audio/constants/wave_form.hpp>
#include "decoder.hpp"

namespace systems::leal::campello_audio::pi {

/// @brief Generate one loopable period of @p waveform at @p frequency Hz.
///
/// Called from AudioEngine::play() for ToneSource voices.
/// The returned buffer is stored in Voice::ownedBuffer and played on loop.
DecodedBuffer generateToneBuffer(WaveForm  waveform,
                                  float     frequency,
                                  uint32_t  sampleRate,
                                  uint32_t  channels);

} // namespace systems::leal::campello_audio::pi
