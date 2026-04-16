/// @file random_source.cpp
/// @brief RandomSource — weighted-random variant pool with pitch/volume variation.

#include <campello_audio/random_source.hpp>
#include "source_handle.hpp"
#include <algorithm>
#include <cmath>

using namespace systems::leal::campello_audio;
using namespace systems::leal::campello_audio::pi;

// ---------------------------------------------------------------------------
// RandomSourceHandle — constructor and variant selection
// ---------------------------------------------------------------------------

RandomSourceHandle::RandomSourceHandle() {
    type = SourceType::Random;
    // Seed from hardware entropy.
    rng.seed(std::random_device{}());
}

int32_t RandomSourceHandle::pickVariant() {
    if (variants.empty()) return -1;
    const auto n = static_cast<int32_t>(variants.size());
    if (n == 1) { lastIndex = 0; return 0; }

    // Sum the weights of all eligible variants (exclude lastIndex if avoidRepeat).
    float totalWeight = 0.0f;
    for (int32_t i = 0; i < n; ++i) {
        if (avoidRepeat && i == lastIndex) continue;
        totalWeight += variants[i].weight;
    }

    // Degenerate case: all eligible weights are zero — pick the first eligible index.
    if (totalWeight <= 0.0f) {
        for (int32_t i = 0; i < n; ++i) {
            if (!avoidRepeat || i != lastIndex) { lastIndex = i; return i; }
        }
        return 0;
    }

    std::uniform_real_distribution<float> dist(0.0f, totalWeight);
    float r = dist(rng);

    for (int32_t i = 0; i < n; ++i) {
        if (avoidRepeat && i == lastIndex) continue;
        r -= variants[i].weight;
        if (r <= 0.0f) { lastIndex = i; return i; }
    }

    // Float rounding left r slightly above 0 — return the last eligible index.
    for (int32_t i = n - 1; i >= 0; --i) {
        if (!avoidRepeat || i != lastIndex) { lastIndex = i; return i; }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// RandomSource — public API
// ---------------------------------------------------------------------------

RandomSource::RandomSource() {
    native = new RandomSourceHandle();
}

RandomSource::~RandomSource() {
    delete static_cast<RandomSourceHandle*>(native);
    native = nullptr;
}

void RandomSource::addVariant(std::shared_ptr<AudioSource> source, float weight) {
    if (!source) return;
    auto* h = static_cast<RandomSourceHandle*>(native);
    h->variants.push_back({std::move(source), std::max(0.0f, weight)});
}

void RandomSource::clearVariants() {
    auto* h = static_cast<RandomSourceHandle*>(native);
    h->variants.clear();
    h->lastIndex = -1;
}

uint32_t RandomSource::getVariantCount() const {
    return static_cast<uint32_t>(
        static_cast<const RandomSourceHandle*>(native)->variants.size());
}

void RandomSource::setPitchVariation(float semitones) {
    static_cast<RandomSourceHandle*>(native)->pitchVariation = std::max(0.0f, semitones);
}

void RandomSource::setVolumeVariation(float db) {
    static_cast<RandomSourceHandle*>(native)->volumeVariation = std::max(0.0f, db);
}

float RandomSource::getPitchVariation() const {
    return static_cast<const RandomSourceHandle*>(native)->pitchVariation;
}

float RandomSource::getVolumeVariation() const {
    return static_cast<const RandomSourceHandle*>(native)->volumeVariation;
}

void RandomSource::setAvoidRepeat(bool avoid) {
    static_cast<RandomSourceHandle*>(native)->avoidRepeat = avoid;
}

bool RandomSource::getAvoidRepeat() const {
    return static_cast<const RandomSourceHandle*>(native)->avoidRepeat;
}

int32_t RandomSource::selectVariant() {
    return static_cast<RandomSourceHandle*>(native)->pickVariant();
}

int32_t RandomSource::getLastVariantIndex() const {
    return static_cast<const RandomSourceHandle*>(native)->lastIndex;
}
