#include "pattern.hpp"
#include <algorithm>
#include <cmath>

namespace systems::leal::campello_audio::pi {

namespace {

// Simple 1D smooth noise for Perlin curve type.
float noise(int n) {
    n = (n << 13) ^ n;
    int nn = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
    return 1.0f - static_cast<float>(nn) / 1073741824.0f;
}

float smoothNoise(float t) {
    int i = static_cast<int>(std::floor(t));
    float f = t - static_cast<float>(i);
    float u = f * f * (3.0f - 2.0f * f); // smoothstep
    return noise(i) * (1.0f - u) + noise(i + 1) * u;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ParameterCurve
// ---------------------------------------------------------------------------

float ParameterCurve::evaluate(double phase) const {
    phase = std::fmod(phase, 1.0);
    if (phase < 0.0) phase += 1.0;

    float t = static_cast<float>(phase);
    float shaped = 0.0f;

    switch (type) {
        case CurveType::Linear:
            shaped = t;
            break;
        case CurveType::Exponential:
            shaped = t * t;
            break;
        case CurveType::Logarithmic:
            shaped = std::sqrt(t);
            break;
        case CurveType::SCurve:
            shaped = t * t * (3.0f - 2.0f * t);
            break;
        case CurveType::Sine:
            shaped = static_cast<float>(std::sin(t * 3.14159265358979323846));
            break;
        case CurveType::Saw:
            shaped = t;
            break;
        case CurveType::Perlin: {
            float n = smoothNoise(t * 16.0f); // 16 octaves of variation per cycle
            shaped = (n + 1.0f) * 0.5f; // map [-1,1] -> [0,1]
            break;
        }
        case CurveType::Square:
            shaped = (t < 0.5f) ? 0.0f : 1.0f;
            break;
        case CurveType::Triangle:
            shaped = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
            break;
    }

    return minValue + (maxValue - minValue) * shaped;
}

float ParameterCurve::evaluate(double phase, float rtpcValue) const {
    if (!rtpcName.empty()) {
        return rtpcValue; // RTPC overrides curve when explicitly bound
    }
    return evaluate(phase);
}

// ---------------------------------------------------------------------------
// Pattern
// ---------------------------------------------------------------------------

void Pattern::query(double fromBeat, double toBeat,
                    std::vector<const PatternEvent*>& out) const {
    out.clear();
    if (events.empty() || fromBeat >= toBeat) return;

    // events are sorted by beat — binary search lower bound
    auto it = std::lower_bound(events.begin(), events.end(), fromBeat,
        [](const PatternEvent& ev, double value) {
            return (ev.beat + ev.duration) <= value;
        });

    for (; it != events.end(); ++it) {
        if (it->beat >= toBeat) break;
        out.push_back(&(*it));
    }
}

void Pattern::queryWrapped(double fromBeat, double toBeat,
                           std::vector<const PatternEvent*>& out) const {
    out.clear();
    if (events.empty() || lengthInBeats <= 0.0) return;

    double len = lengthInBeats;
    double f = std::fmod(fromBeat, len);
    double t = std::fmod(toBeat, len);
    if (f < 0.0) f += len;
    if (t < 0.0) t += len;

    if (f < t) {
        query(f, t, out);
    } else {
        // Wraps around end of pattern
        std::vector<const PatternEvent*> temp;
        query(f, len, out);
        query(0.0, t, temp);
        out.insert(out.end(), temp.begin(), temp.end());
    }
}

} // namespace systems::leal::campello_audio::pi
