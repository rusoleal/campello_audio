#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <vector>

namespace systems::leal::campello_audio {

/// @brief Synchronous loader — callable that returns raw encoded bytes.
///
/// The engine calls this on the calling thread immediately; it should return
/// the complete encoded file bytes (WAV, OGG, MP3, …) for the source to decode.
///
/// @code
/// src.load([]() -> std::vector<uint8_t> {
///     return myPakFile.read("sounds/shot.wav");
/// });
/// @endcode
using ByteLoader = std::function<std::vector<uint8_t>()>;

/// @brief Asynchronous loader — callable that returns a future of encoded bytes.
///
/// The engine calls this immediately to obtain the future; the actual I/O work
/// runs wherever the caller arranges (std::async, custom thread pool, coroutine).
/// The future must eventually resolve to the complete encoded bytes for decoding.
///
/// @code
/// engine.loadAsync(src,
///     []() -> std::future<std::vector<uint8_t>> {
///         return std::async(std::launch::async, []{ return readFile("shot.wav"); });
///     },
///     [](bool ok) { if (ok) engine.play(src); }
/// );
/// @endcode
using AsyncByteLoader = std::function<std::future<std::vector<uint8_t>>()>;

/// @brief Callback fired on the game thread during AudioEngine::tick() once an
///        async load completes. @p success is false if the loader threw or the
///        returned bytes could not be decoded into valid audio data.
using LoadCallback = std::function<void(bool success)>;

} // namespace systems::leal::campello_audio
