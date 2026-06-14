#include "pattern_bank.hpp"
#include <campello_audio/audio_source.hpp>
#include <cstdint>
#include <fstream>
#include <vector>

namespace systems::leal::campello_audio::pi {

namespace {

// Binary format helpers — write little-endian
void writeU32(std::ostream& os, uint32_t v) {
    os.put(static_cast<char>( v        & 0xFF));
    os.put(static_cast<char>((v >>  8) & 0xFF));
    os.put(static_cast<char>((v >> 16) & 0xFF));
    os.put(static_cast<char>((v >> 24) & 0xFF));
}

void writeU64(std::ostream& os, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        os.put(static_cast<char>((v >> (i * 8)) & 0xFF));
}

void writeF32(std::ostream& os, float v) {
    static_assert(sizeof(float) == 4, "float must be 4 bytes");
    union { float f; uint32_t u; } conv;
    conv.f = v;
    writeU32(os, conv.u);
}

void writeF64(std::ostream& os, double v) {
    static_assert(sizeof(double) == 8, "double must be 8 bytes");
    union { double d; uint64_t u; } conv;
    conv.d = v;
    writeU64(os, conv.u);
}

void writeString(std::ostream& os, const std::string& s) {
    writeU32(os, static_cast<uint32_t>(s.size()));
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool readU32(std::istream& is, uint32_t& v) {
    char buf[4];
    if (!is.read(buf, 4)) return false;
    v = static_cast<uint8_t>(buf[0]) |
        (static_cast<uint8_t>(buf[1]) <<  8) |
        (static_cast<uint8_t>(buf[2]) << 16) |
        (static_cast<uint8_t>(buf[3]) << 24);
    return true;
}

bool readU64(std::istream& is, uint64_t& v) {
    char buf[8];
    if (!is.read(buf, 8)) return false;
    v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(static_cast<uint8_t>(buf[i])) << (i * 8);
    return true;
}

bool readF32(std::istream& is, float& v) {
    union { float f; uint32_t u; } conv;
    if (!readU32(is, conv.u)) return false;
    v = conv.f;
    return true;
}

bool readF64(std::istream& is, double& v) {
    union { double d; uint64_t u; } conv;
    if (!readU64(is, conv.u)) return false;
    v = conv.d;
    return true;
}

bool readString(std::istream& is, std::string& s) {
    uint32_t len = 0;
    if (!readU32(is, len)) return false;
    if (len > 65536) return false; // sanity limit
    s.resize(len);
    if (len > 0 && !is.read(s.data(), static_cast<std::streamsize>(len))) return false;
    return true;
}

constexpr char kMagic[4] = {'C', 'P', 'B', 'N'};
constexpr uint32_t kVersion = 1;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PatternBank::registerSource(const std::string& label,
                                 std::shared_ptr<AudioSource> source) {
    if (!source || label.empty()) return;
    sources_[label] = std::move(source);
}

void PatternBank::addPattern(const std::string& label,
                             std::shared_ptr<Pattern> pattern) {
    if (!pattern || label.empty()) return;
    patterns_[label] = std::move(pattern);
}

std::shared_ptr<AudioSource> PatternBank::getSource(const std::string& label) const {
    auto it = sources_.find(label);
    return (it != sources_.end()) ? it->second : nullptr;
}

std::shared_ptr<Pattern> PatternBank::getPattern(const std::string& label) const {
    auto it = patterns_.find(label);
    return (it != patterns_.end()) ? it->second : nullptr;
}

std::vector<std::string> PatternBank::getPatternLabels() const {
    std::vector<std::string> labels;
    labels.reserve(patterns_.size());
    for (const auto& p : patterns_) labels.push_back(p.first);
    return labels;
}

std::vector<std::string> PatternBank::getSourceLabels() const {
    std::vector<std::string> labels;
    labels.reserve(sources_.size());
    for (const auto& s : sources_) labels.push_back(s.first);
    return labels;
}

bool PatternBank::saveToFile(const std::string& path) const {
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;

    os.write(kMagic, 4);
    writeU32(os, kVersion);

    // Source table (labels only — runtime resolves AudioSource pointers)
    writeU32(os, static_cast<uint32_t>(sources_.size()));
    for (const auto& kv : sources_) {
        writeString(os, kv.first);
    }

    // Pattern table
    writeU32(os, static_cast<uint32_t>(patterns_.size()));
    for (const auto& kv : patterns_) {
        writeString(os, kv.first);
        const Pattern& pat = *kv.second;
        writeF64(os, pat.lengthInBeats);
        writeU32(os, static_cast<uint32_t>(pat.events.size()));
        for (const auto& ev : pat.events) {
            writeString(os, ev.sourceLabel);
            writeF64(os, ev.beat);
            writeF64(os, ev.duration);
            writeF32(os, ev.gain);
            writeF32(os, ev.pitch);
            writeF32(os, ev.pan);
            writeF32(os, ev.probability);
            writeU32(os, static_cast<uint32_t>(ev.paramCurves.size()));
            for (const auto& curve : ev.paramCurves) {
                writeU32(os, static_cast<uint32_t>(curve.targetParam));
                writeU32(os, static_cast<uint32_t>(curve.type));
                writeF64(os, curve.periodBeats);
                writeF32(os, curve.minValue);
                writeF32(os, curve.maxValue);
                writeString(os, curve.rtpcName);
            }
        }
    }

    return os.good();
}

bool PatternBank::loadFromFile(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    char magic[4];
    if (!is.read(magic, 4)) return false;
    if (magic[0] != kMagic[0] || magic[1] != kMagic[1] ||
        magic[2] != kMagic[2] || magic[3] != kMagic[3]) return false;

    uint32_t version = 0;
    if (!readU32(is, version)) return false;
    if (version != kVersion) return false;

    sources_.clear();
    patterns_.clear();

    uint32_t sourceCount = 0;
    if (!readU32(is, sourceCount)) return false;
    for (uint32_t i = 0; i < sourceCount; ++i) {
        std::string label;
        if (!readString(is, label)) return false;
        // Source pointers are resolved by the game after load.
        // We store a placeholder nullptr; the game calls registerSource.
        sources_[label] = nullptr;
    }

    uint32_t patternCount = 0;
    if (!readU32(is, patternCount)) return false;
    for (uint32_t i = 0; i < patternCount; ++i) {
        std::string label;
        if (!readString(is, label)) return false;

        auto pat = std::make_shared<Pattern>();
        if (!readF64(is, pat->lengthInBeats)) return false;

        uint32_t eventCount = 0;
        if (!readU32(is, eventCount)) return false;
        pat->events.reserve(eventCount);

        for (uint32_t e = 0; e < eventCount; ++e) {
            PatternEvent ev;
            if (!readString(is, ev.sourceLabel)) return false;
            if (!readF64(is, ev.beat)) return false;
            if (!readF64(is, ev.duration)) return false;
            if (!readF32(is, ev.gain)) return false;
            if (!readF32(is, ev.pitch)) return false;
            if (!readF32(is, ev.pan)) return false;
            if (!readF32(is, ev.probability)) return false;

            uint32_t curveCount = 0;
            if (!readU32(is, curveCount)) return false;
            ev.paramCurves.reserve(curveCount);
            for (uint32_t c = 0; c < curveCount; ++c) {
                ParameterCurve curve;
                uint32_t tp = 0, ct = 0;
                if (!readU32(is, tp)) return false;
                if (!readU32(is, ct)) return false;
                curve.targetParam = static_cast<PatternParam>(tp);
                curve.type = static_cast<CurveType>(ct);
                if (!readF64(is, curve.periodBeats)) return false;
                if (!readF32(is, curve.minValue)) return false;
                if (!readF32(is, curve.maxValue)) return false;
                if (!readString(is, curve.rtpcName)) return false;
                ev.paramCurves.push_back(std::move(curve));
            }
            pat->events.push_back(std::move(ev));
        }

        patterns_[label] = std::move(pat);
    }

    return is.good();
}

} // namespace systems::leal::campello_audio::pi
