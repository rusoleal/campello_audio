#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <campello_audio/constants/loop_mode.hpp>
#include <campello_audio/constants/curve_type.hpp>
#include <campello_audio/filter.hpp>

namespace systems::leal::campello_audio {

class AudioParameter;
enum class AudioSourceProperty;

/// @brief Abstract base for all audio data sources.
///
/// An AudioSource holds configuration for how a sound should be played.
/// The AudioEngine creates one or more voice instances from it at play time.
/// A source can be played multiple times simultaneously; each play call
/// produces an independent voice.
///
/// Never instantiate this directly — use WavSource, OggSource, etc.
class AudioSource {
    friend class AudioEngine;  ///< Engine reads native handle directly at play time.
    friend class AudioBus;     ///< Bus reads native handle to route child sources.

public:
    virtual ~AudioSource();

    // -----------------------------------------------------------------------
    // Playback defaults
    // -----------------------------------------------------------------------

    /// @brief Set the default volume for new instances [0..1].
    void setVolume(float volume);

    /// @brief Retrieve the current default volume.
    float getVolume() const;

    /// @brief Set whether new instances loop by default.
    void setLooping(bool loop);

    /// @brief Set the loop mode for new instances.
    void setLoopMode(LoopMode mode);

    /// @brief If true, at most one instance plays at a time.
    /// Triggering a new play() on a single-instance source stops the previous.
    void setSingleInstance(bool singleInstance);

    // -----------------------------------------------------------------------
    // Filter chain
    // -----------------------------------------------------------------------

    /// @brief Attach a filter to the given slot [0..Filter::kMaxFiltersPerSource-1].
    /// Replaces any existing filter in that slot.
    void addFilter(std::shared_ptr<Filter> filter, uint32_t slot);

    /// @brief Remove the filter in @p slot.
    void removeFilter(uint32_t slot);

    // -----------------------------------------------------------------------
    // RTPC — parameter binding
    // -----------------------------------------------------------------------

    /// @brief Bind a named AudioParameter to an audio property on this source.
    ///
    /// When the parameter value changes (via AudioEngine::setParameter()), the
    /// engine maps it to @p property using @p curve, remapped from the
    /// parameter's [minValue..maxValue] range to [@p outputMin..@p outputMax].
    ///
    /// @param parameterName  Name of a registered AudioParameter.
    /// @param property       Which audio property to drive.
    /// @param curve          How the parameter value maps to the property value.
    /// @param outputMin      Property value when parameter is at its minimum.
    /// @param outputMax      Property value when parameter is at its maximum.
    /// @param filterSlot     Only used when property == FilterParam. Slot index.
    /// @param filterParamId  Only used when property == FilterParam. Param within filter.
    void bindParameter(const std::string&  parameterName,
                       AudioSourceProperty property,
                       CurveType           curve     = CurveType::Linear,
                       float               outputMin = 0.0f,
                       float               outputMax = 1.0f,
                       uint32_t            filterSlot    = 0,
                       uint32_t            filterParamId = 0);

    /// @brief Remove the binding for @p parameterName on this source.
    void unbindParameter(const std::string& parameterName);

protected:
    AudioSource();
    void* native = nullptr;
};

} // namespace systems::leal::campello_audio
