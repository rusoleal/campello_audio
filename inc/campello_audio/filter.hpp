#pragma once

#include <memory>

namespace systems::leal::campello_audio {

/// @brief Base class for all audio filters / effects.
///
/// A filter transforms audio samples in real-time. Up to
/// kMaxFiltersPerSource filters may be attached to any AudioSource or
/// AudioBus via addFilter(). Each filter exposes named parameters that can be
/// adjusted live (including via smooth fades and LFO oscillation).
///
/// Derive from this class to implement custom DSP effects.
class Filter {
public:
    /// Maximum number of filters that can be attached to a single source/bus.
    static constexpr uint32_t kMaxFiltersPerSource = 8;

    virtual ~Filter();

    /// @brief Set a filter parameter to an immediate value.
    /// @param paramId  Implementation-defined parameter index.
    /// @param value    Target value.
    void setParam(uint32_t paramId, float value);

    /// @brief Smoothly ramp a filter parameter to @p to over @p timeSecs.
    void fadeParam(uint32_t paramId, float to, double timeSecs);

    /// @brief Oscillate a parameter between @p from and @p to with the given period.
    void oscillateParam(uint32_t paramId, float from, float to, double periodSecs);

protected:
    Filter();
    void* native = nullptr;
};

} // namespace systems::leal::campello_audio
