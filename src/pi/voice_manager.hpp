#pragma once

#include "mixer.hpp"

namespace systems::leal::campello_audio::pi {

/// @brief Voice allocation, lookup, and LRU-eviction helpers.
///
/// All methods must be called while the caller holds MixerData::voiceMutex.
struct VoiceManager {
    /// @brief Resize the real voice pool to config.maxVoices and zero-initialise slots.
    static void init(MixerData& mx);

    /// @brief Allocate a free real voice slot.
    ///
    /// Strategy:
    ///   1. Return the first inactive slot (O(n) scan).
    ///   2. Pool full: virtualize the quietest active non-protected voice whose
    ///      volume is below config.virtualizeThreshold (if virtual pool has room).
    ///   3. Pool full + nothing to virtualize: steal oldest non-protected slot (LRU).
    ///   4. All slots protected: return nullptr.
    ///
    /// The returned pointer is valid as long as voiceMutex is held.
    static Voice* allocate(MixerData& mx);

    /// @brief Deactivate a real voice by ID.
    static void free(MixerData& mx, uint64_t id);

    /// @brief Find an active real voice by ID.
    /// @return Pointer, or nullptr if not found.
    static Voice* find(MixerData& mx, uint64_t id);

    /// @brief Find a virtual voice by ID.
    /// @return Pointer, or nullptr if not found.
    static Voice* findVirtual(MixerData& mx, uint64_t id);

    /// @brief Find a voice in either the real or virtual pool.
    /// Real pool is checked first. Returns nullptr if not found in either.
    static Voice* findAny(MixerData& mx, uint64_t id);

    /// @brief Remove a virtual voice by ID (no-op if not found).
    static void freeVirtual(MixerData& mx, uint64_t id);

    /// @brief Move a virtual voice back to the real pool, preserving its ID and state.
    ///
    /// Finds the first free real slot. Returns false if no slot is available —
    /// in that case the caller should leave the voice in the virtual pool.
    static bool reactivate(MixerData& mx, Voice& virtualVoice);
};

} // namespace systems::leal::campello_audio::pi
