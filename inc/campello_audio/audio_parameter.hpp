#pragma once

#include <cstdint>
#include <string>
#include <campello_audio/constants/curve_type.hpp>

namespace systems::leal::campello_audio {

/// @brief A named game parameter that drives audio properties at runtime (RTPC).
///
/// Real-Time Parameter Control (RTPC) decouples game logic from audio
/// behaviour. The game sets abstract parameters ("speed", "health",
/// "wetness"); the audio engine maps them to volume, pitch, filter cutoff,
/// blend ratios, etc. via configurable curves — without any extra game code.
///
/// ### Workflow
/// 1. Create and register a parameter:
///    @code
///    auto speed = std::make_shared<AudioParameter>("speed", 0.0f, 200.0f);
///    engine.registerParameter(speed);
///    @endcode
///
/// 2. Bind it to a source property:
///    @code
///    engineSound.bindParameter("speed", AudioSourceProperty::Pitch,
///                              CurveType::Exponential, 0.5f, 2.0f);
///    // pitch ranges from 0.5× (stopped) to 2.0× (max speed)
///    @endcode
///
/// 3. Update it from game code each frame:
///    @code
///    engine.setParameter("speed", vehicle.getSpeed());
///    @endcode
class AudioParameter {
public:
    /// @param name      Unique identifier used in all API calls.
    /// @param minValue  Minimum game-side value.
    /// @param maxValue  Maximum game-side value.
    AudioParameter(const std::string& name,
                   float minValue = 0.0f,
                   float maxValue = 1.0f);
    ~AudioParameter();

    const std::string& getName()     const;
    float              getValue()    const;
    float              getMinValue() const;
    float              getMaxValue() const;

    /// @brief Set the current value (clamped to [minValue, maxValue]).
    void setValue(float value);

private:
    void* native = nullptr;
};

// ---------------------------------------------------------------------------
// Per-source property that a parameter can drive
// ---------------------------------------------------------------------------

/// @brief Audio source properties that can be controlled by an AudioParameter.
enum class AudioSourceProperty {
    Volume,       ///< Output volume [0..1].
    Pitch,        ///< Playback speed multiplier.
    Pan,          ///< Stereo pan [-1..1].
    FilterParam,  ///< A specific filter parameter (specify slot + paramId in bind call).
    SendLevel,    ///< Send gain to a bus.
    LowPassCutoff,  ///< Convenience shortcut for LowPassFilter::PARAM_CUTOFF on slot 0.
    HighPassCutoff  ///< Convenience shortcut for HighPassFilter::PARAM_CUTOFF on slot 0.
};

} // namespace systems::leal::campello_audio
