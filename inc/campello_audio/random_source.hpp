#pragma once

#include <cstdint>
#include <memory>
#include <campello_audio/audio_source.hpp>

namespace systems::leal::campello_audio {

/// @brief A container that randomly selects one variant each time it is played.
///
/// Eliminates the "machine gun" repetition artefact that occurs when the same
/// sound is triggered many times in quick succession. Each play() call picks a
/// variant based on its relative weight and applies randomized pitch and volume
/// offsets before playback.
///
/// @code
/// auto gunshot = std::make_shared<RandomSource>();
/// for (auto& path : {"shot1.wav", "shot2.wav", "shot3.wav"}) {
///     auto v = std::make_shared<WavSource>();
///     v->load(path);
///     gunshot->addVariant(v);
/// }
/// gunshot->setPitchVariation(2.0f);   // ±2 semitones
/// gunshot->setVolumeVariation(2.0f);  // ±2 dB
///
/// engine.play(*gunshot);
/// @endcode
class RandomSource : public AudioSource {
public:
    RandomSource();
    ~RandomSource() override;

    /// @brief Add a sound variant to the pool.
    /// @param source  The audio source for this variant.
    /// @param weight  Relative probability weight (default 1.0 = equal chance).
    ///                A variant with weight 2.0 is twice as likely as one with 1.0.
    void addVariant(std::shared_ptr<AudioSource> source, float weight = 1.0f);

    /// @brief Remove all variants from the pool.
    void clearVariants();

    /// @return Number of variants currently in the pool.
    uint32_t getVariantCount() const;

    /// @brief Maximum random pitch offset applied to each play, in semitones.
    /// A value of 2.0 means the pitch is randomized in [-2, +2] semitones.
    void setPitchVariation(float semitones);

    /// @brief Maximum random volume offset applied to each play, in decibels.
    /// A value of 3.0 means the volume is randomized in [-3, +3] dB.
    void setVolumeVariation(float db);

    float getPitchVariation() const;
    float getVolumeVariation() const;

    /// @brief If true, the same variant is never chosen twice in a row.
    void setAvoidRepeat(bool avoid);
    bool getAvoidRepeat() const;
};

} // namespace systems::leal::campello_audio
