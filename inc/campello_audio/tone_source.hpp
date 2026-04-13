#pragma once

#include <campello_audio/audio_source.hpp>
#include <campello_audio/constants/wave_form.hpp>

namespace systems::leal::campello_audio {

/// @brief Procedural tone generator — useful for UI sounds, alerts and tests.
///
/// Generates audio in real-time; no file loading required.
///
/// @code
/// ToneSource beep(WaveForm::Sine, 440.0f);
/// auto h = engine.play(beep);
/// engine.fadeVolume(h, 0.0f, 0.5);  // fade out over 0.5s
/// @endcode
class ToneSource : public AudioSource {
public:
    /// @param waveform   Waveform shape.
    /// @param frequency  Initial frequency in Hz (default A4 = 440 Hz).
    ToneSource(WaveForm waveform = WaveForm::Sine, float frequency = 440.0f);
    ~ToneSource() override;

    /// @brief Change the oscillator frequency.
    void setFrequency(float hz);

    /// @brief Retrieve the current frequency.
    float getFrequency() const;

    /// @brief Change the waveform shape.
    void setWaveForm(WaveForm waveform);

    /// @brief Retrieve the current waveform shape.
    WaveForm getWaveForm() const;
};

} // namespace systems::leal::campello_audio
