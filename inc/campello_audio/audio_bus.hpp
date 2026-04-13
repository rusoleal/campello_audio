#pragma once

#include <cstdint>
#include <memory>
#include <campello_audio/audio_source.hpp>
#include <campello_audio/types/sound_handle.hpp>
#include <campello_audio/descriptors/play_descriptor.hpp>

namespace systems::leal::campello_audio {

/// @brief Mixing bus — a submix channel that routes voices through shared effects.
///
/// An AudioBus is itself an AudioSource so it can be played by the engine or
/// routed into another bus (hierarchical mixing). Voices played *into* the bus
/// are mixed together, the bus filters/volume are applied, and the result is
/// fed to the bus's own destination (main mix or parent bus).
///
/// Typical use cases:
/// - SFX bus with a compressor to prevent clipping during intense moments.
/// - Music bus with a low-pass filter for an "underwater" effect.
/// - Voice bus routed through a radio distortion filter.
///
/// @code
/// AudioBus sfxBus;
/// auto busHandle = engine.play(sfxBus);
///
/// WavSource explosion;
/// explosion.load("explosion.wav");
/// sfxBus.play(explosion);   // routed through sfxBus
/// @endcode
class AudioBus : public AudioSource {
public:
    AudioBus();
    ~AudioBus() override;

    /// @brief Play @p source through this bus using default parameters.
    SoundHandle play(AudioSource& source);

    /// @brief Play @p source through this bus with explicit parameters.
    SoundHandle play(AudioSource& source, const PlayDescriptor& descriptor);

    /// @brief Set the bus output volume [0..1].
    void setVolume(float volume);

    /// @brief Retrieve the most recent mixed waveform for visualisation.
    /// @param[out] sampleCount  Number of samples in the returned buffer.
    /// @return Pointer to a float buffer owned by the bus. Valid until the
    ///         next mix tick. nullptr if visualization is disabled.
    const float* getVisualizationData(uint32_t& sampleCount) const;
};

} // namespace systems::leal::campello_audio
