#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "pattern.hpp"

namespace systems::leal::campello_audio {
class AudioSource;
}

namespace systems::leal::campello_audio::pi {

/// @brief Owns a collection of compiled patterns and their source references.
///
/// The editor creates and saves PatternBanks. The runtime loads them and
/// passes them to PatternTrack.
class PatternBank {
public:
    PatternBank() = default;
    ~PatternBank() = default;

    // Disable copy; enable move
    PatternBank(const PatternBank&) = delete;
    PatternBank& operator=(const PatternBank&) = delete;
    PatternBank(PatternBank&&) = default;
    PatternBank& operator=(PatternBank&&) = default;

    /// @brief Register a one-shot sample source that patterns can reference.
    void registerSource(const std::string& label,
                        std::shared_ptr<AudioSource> source);

    /// @brief Add a named compiled pattern.
    void addPattern(const std::string& label,
                    std::shared_ptr<Pattern> pattern);

    /// @brief Look up a registered source by label.
    std::shared_ptr<AudioSource> getSource(const std::string& label) const;

    /// @brief Look up a pattern by label.
    std::shared_ptr<Pattern> getPattern(const std::string& label) const;

    /// @return All registered pattern labels (useful for editor UI / introspection).
    std::vector<std::string> getPatternLabels() const;

    /// @return All registered source labels.
    std::vector<std::string> getSourceLabels() const;

    /// @brief Serialize to a binary .cpbank file.
    bool saveToFile(const std::string& path) const;

    /// @brief Deserialize from a binary .cpbank file.
    bool loadFromFile(const std::string& path);

private:
    std::unordered_map<std::string, std::shared_ptr<AudioSource>> sources_;
    std::unordered_map<std::string, std::shared_ptr<Pattern>> patterns_;
};

} // namespace systems::leal::campello_audio::pi
