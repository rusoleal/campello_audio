/// @file voice_manager.cpp
/// @brief VoiceManager — voice pool allocation, LRU stealing, and lookup.

#include "voice_manager.hpp"

namespace systems::leal::campello_audio::pi {

void VoiceManager::init(MixerData& mx) {
    mx.voices.clear();
    mx.voices.resize(mx.config.maxVoices);  // default-constructs each Voice
}

Voice* VoiceManager::allocate(MixerData& mx) {
    // 1. First inactive (free) slot
    for (auto& v : mx.voices) {
        if (!v.active) {
            v          = Voice{};
            v.id       = mx.nextId++;
            v.active   = true;
            v.lruAge   = mx.lruClock++;
            return &v;
        }
    }

    // 2. Pool full — try to virtualize the quietest non-protected voice that is
    //    below the threshold, freeing its slot for the new voice.
    if (mx.config.maxVirtualVoices > 0 &&
        mx.virtualVoices.size() < mx.config.maxVirtualVoices) {
        Voice* quietest = nullptr;
        for (auto& v : mx.voices) {
            if (!v.active || v.protect) continue;
            if (!quietest || v.volume < quietest->volume) quietest = &v;
        }
        const float effVol = quietest
            ? quietest->volume * (quietest->is3d ? quietest->attenuationGain : 1.0f)
            : 0.0f;
        if (quietest && effVol < mx.config.virtualizeThreshold) {
            mx.virtualVoices.push_back(std::move(*quietest));
            *quietest        = Voice{};
            quietest->id     = mx.nextId++;
            quietest->active = true;
            quietest->lruAge = mx.lruClock++;
            return quietest;
        }
    }

    // 3. Steal oldest non-protected slot (LRU eviction)
    Voice* victim = nullptr;
    for (auto& v : mx.voices) {
        if (!v.protect) {
            if (!victim || v.lruAge < victim->lruAge) {
                victim = &v;
            }
        }
    }
    if (!victim) return nullptr;  // all voices are protected

    // Reset stolen slot and assign a fresh id / age.
    // Active voice count is unchanged — we replaced one active voice with another.
    *victim        = Voice{};
    victim->id     = mx.nextId++;
    victim->active = true;
    victim->lruAge = mx.lruClock++;
    return victim;
}

void VoiceManager::free(MixerData& mx, uint64_t id) {
    for (auto& v : mx.voices) {
        if (v.active && v.id == id) {
            v.active = false;
            return;
        }
    }
}

Voice* VoiceManager::find(MixerData& mx, uint64_t id) {
    for (auto& v : mx.voices) {
        if (v.active && v.id == id) return &v;
    }
    return nullptr;
}

Voice* VoiceManager::findVirtual(MixerData& mx, uint64_t id) {
    for (auto& v : mx.virtualVoices) {
        if (v.active && v.id == id) return &v;
    }
    return nullptr;
}

Voice* VoiceManager::findAny(MixerData& mx, uint64_t id) {
    Voice* v = find(mx, id);
    return v ? v : findVirtual(mx, id);
}

void VoiceManager::freeVirtual(MixerData& mx, uint64_t id) {
    for (auto it = mx.virtualVoices.begin(); it != mx.virtualVoices.end(); ++it) {
        if (it->id == id) {
            mx.virtualVoices.erase(it);
            return;
        }
    }
}

bool VoiceManager::reactivate(MixerData& mx, Voice& vv) {
    for (auto& slot : mx.voices) {
        if (!slot.active) {
            const uint64_t savedId  = vv.id;
            const uint64_t savedAge = mx.lruClock++;
            slot                    = std::move(vv);
            slot.id                 = savedId;
            slot.active             = true;
            slot.lruAge             = savedAge;
            return true;
        }
    }
    return false;  // no free real slot — caller leaves voice in virtual pool
}

} // namespace systems::leal::campello_audio::pi
