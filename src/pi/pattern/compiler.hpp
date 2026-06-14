#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "pattern.hpp"

namespace systems::leal::campello_audio {
class AudioSource;
}

namespace systems::leal::campello_audio::pi {

/// @brief Compiles high-level pattern expressions into flat Pattern timelines.
///
/// The editor uses this to turn designer-friendly expressions into runtime
/// Pattern objects that can be saved in a PatternBank.
///
/// Supported syntax (strudel-style):
/// @code
///   sound("bd*4 sd")
///   sound("<bd sd>").gain(0.8)
///   sound("bd*2, sd hh")
///   stack("bd*4", "sd", "hh(7,8)")
///   cat("bd*4", "sd")
///   rev("bd sd hh")
///   slow(2, "bd*4 sd")
///   fast(2, "bd*4 sd")
///   degradeBy(0.5, "bd*4 sd")
///   degradeBy(0.5, 12345, "bd*4 sd")
///   "bd*4".gain(0.8).pan(-0.3)
///   "hh(7,8)".lpf(800, 12000, 4.0)
///   "bd*4".sine(0.2, 0.8, 2.0)
///   "bd*4".saw(0.1, 0.9, 1.0)
///   "bd*4".square(0.0, 1.0, 4.0)
///   "bd*4".tri(0.3, 0.7, 2.0)
///   "bd*4".perlin(0.0, 1.0, 8.0)
/// @endcode
///
/// String literals are parsed as mini-notation. Function calls return
/// transformed patterns. Method calls attach parameter overrides.
class PatternCompiler {
public:
    PatternCompiler() = default;
    ~PatternCompiler() = default;

    /// @brief Register a source so the compiler can resolve mini-notation labels.
    /// Sources are only needed if you call compile() with expressions that
    /// reference them; they are not stored in the compiled Pattern.
    void registerSource(const std::string& label,
                        std::shared_ptr<AudioSource> source);

    /// @brief Compile an expression into a Pattern.
    /// @return nullptr on parse or compile error; call getLastError() for details.
    std::unique_ptr<Pattern> compile(const std::string& expression,
                                     double cycleBeats = 4.0);

    /// @brief Error message from the last failed compile() call.
    const std::string& getLastError() const { return lastError_; }

    /// @brief Clear any registered sources and error state.
    void reset();

private:
    std::string lastError_;
    std::unordered_map<std::string, std::shared_ptr<AudioSource>> sources_;
};

} // namespace systems::leal::campello_audio::pi
