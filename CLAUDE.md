# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Build Commands

```bash
cmake -B build && cmake --build build
```

Universal tests (no audio device):
```bash
cmake -B build -DBUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure
```

Integration tests (requires real audio hardware):
```bash
cmake -B build -DBUILD_INTEGRATION_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure
```

Platform selection is automatic via `CMAKE_SYSTEM_NAME` — see `macos.cmake`, `ios.cmake`, `android.cmake`, `windows.cmake`, `linux.cmake`.

## Architecture Overview

`campello_audio` is a cross-platform C++20 audio engine for game development.
All public types are in the `systems::leal::campello_audio` namespace.

### Handle-Based Abstraction Pattern

Identical to `campello_gpu`: public API classes hold `void* native` pointing to an opaque internal data struct. Platform-specific headers (CoreAudio, AAudio, WASAPI) only appear in `.cpp` files under `src/`.

- Public headers: `inc/campello_audio/*.hpp`
- Internal structs: `src/coreaudio/common.hpp`, `src/aaudio/common.hpp`, etc.
- Platform-independent mixing: `src/pi/mixer.hpp` / `mixer.cpp`
- Backend implementations: `src/coreaudio/`, `src/aaudio/`, `src/wasapi/`, `src/pulse/`, `src/alsa/`

### API Model (SoLoud-inspired)

```
AudioEngine::init()
  → play(AudioSource&) → SoundHandle
  → setVolume(handle, v) / fadeVolume(handle, to, t) / stop(handle)
  → update3d()   ← call once per game frame
  → deinit()
```

"Fire and forget": ignore the returned `SoundHandle` when you don't need control.
Store it when you need to modify the voice after launch.

### Key Types

| Category     | Types |
|--------------|-------|
| Engine       | `AudioEngine`, `AudioEngineDescriptor` |
| Sources      | `AudioSource` (base), `WavSource`, `OggSource`, `Mp3Source`, `ToneSource`, `AudioStream`, `RandomSource`, `MusicTrack` |
| Submix       | `AudioBus` (is-a AudioSource) |
| Playback     | `SoundHandle`, `PlayDescriptor` |
| 3D Audio     | `ListenerDescriptor`, `AttenuationModel` |
| Math         | `vector_math::Vector3<float>` (aliased as `Vec3` in descriptors/mixer) |
| Filters      | `Filter` (base), `LowPassFilter`, `HighPassFilter`, `EchoFilter`, `ReverbFilter`, `CompressorFilter`, `LimiterFilter`, `ChorusFilter`, `FlangerFilter`, `PitchShiftFilter` |
| RTPC         | `AudioParameter`, `AudioSourceProperty`, `CurveType` |
| Snapshots    | `AudioSnapshot` |
| Constants    | `WaveForm`, `LoopMode`, `ResampleQuality`, `TransitionRule`, `CurveType` |

### Platform-Independent Mixer (`src/pi/`)

The mixer knows nothing about audio hardware. The backend calls
`MixerData::mixSamples(float* out, uint32_t frames)` from its audio thread.
The mixer iterates active voices, decodes source data, applies volume/pan/pitch/filters,
accumulates into the output buffer, and applies the master volume.

### Dependencies

| Library      | Purpose               | CMake target  | Include |
|--------------|-----------------------|---------------|---------|
| vector_math  | 3D vectors / matrices | `vector_math` | `<vector_math/vector_math.hpp>` |

vector_math is maintained by the same author (rusoleal/vector_math). Always use
`systems::leal::vector_math::Vector3<float>` for positions/velocities/directions.
A `using Vec3 = ...` alias is provided in the descriptor headers for convenience.
**Do not** define local Vec3/Vec2/Matrix/Quaternion types — use vector_math.

### Third-Party Decoders (Phase 1)

| Library     | Format | Integration |
|-------------|--------|-------------|
| dr_wav      | WAV    | `src/pi/decoder.cpp` |
| stb_vorbis  | OGG    | `src/pi/decoder.cpp` |
| dr_mp3      | MP3    | `src/pi/decoder.cpp` |

All are single-file, public-domain, header-only. Fetch via CMake `FetchContent`.

### Backend Architecture

Each backend implements the same contract:
1. Open the audio device using the platform API.
2. Set up a callback / thread that calls `MixerData::mixSamples()`.
3. Map the engine descriptor (sampleRate, bufferSize, channels) to the platform API.

| Backend    | Platform      | Status           | Source dir       |
|------------|---------------|------------------|------------------|
| CoreAudio  | macOS / iOS   | Complete         | `src/coreaudio/` |
| AAudio     | Android ≥26   | Complete         | `src/aaudio/`    |
| WASAPI     | Windows       | Complete         | `src/wasapi/`    |
| PulseAudio | Linux         | Complete         | `src/pulse/`     |
| ALSA       | Linux fallback| Complete         | `src/alsa/`      |

### Audio Flow

```
AudioSource (decoded PCM in memory)
  ↓
VoiceManager (allocate / steal / free voices)
  ↓
Mixer (volume × pan matrix, pitch, filters, 3D attenuation)
  ↓
Backend callback (CoreAudio / AAudio / WASAPI / PulseAudio)
  ↓
Hardware
```

### Coding Conventions

- No exceptions; return `bool` / `nullptr` on failure.
- No raw owning pointers in public API — resources managed by their owning class.
- Filters passed as `std::shared_ptr<Filter>` (same filter instance can be shared between sources).
- Atomic counters (`std::atomic<>`) for metrics visible across threads.
- Platform headers (`AudioToolbox`, `aaudio`, `mmdeviceapi.h`) never appear in `inc/`.
- Follow the campello_gpu style: descriptors in `inc/.../descriptors/`, enums in `inc/.../constants/`.

## Development Sequence

See `TODO.md` for the full phased plan. The recommended order is:

1. Phase 1: Decoders (dr_wav, stb_vorbis, dr_mp3)
2. Phase 2: Mixer core
3. Phase 3a: CoreAudio backend (macOS/iOS)
4. Phase 4: Voice playback pipeline
5. Phase 3b/c/d: remaining backends (Android, Windows, Linux)
6. Phase 5: 3D audio
7. Phase 6: Filters
8. Phase 7: AudioBus

## Testing

- **Universal tests** (`tests/universal/`): no audio device needed; test enums, descriptor defaults, Vec3 math, SoundHandle equality. Run on every CI platform.
- **Integration tests** (`tests/platform/`): require real audio hardware; test init/deinit, playback, 3D, filters. Run on macOS CI with CoreAudio.

## Current Status

Phases 0–16 complete + AAudio and WASAPI backends implemented. PulseAudio/ALSA (Linux) is the next backend.

- CoreAudio backend (macOS/iOS): functional and tested.
- AAudio backend (Android ≥26): complete — `AAudioStreamBuilder`, float32 PCM, low-latency shared mode, error callback for `AAUDIO_ERROR_DISCONNECTED`.
- WASAPI backend (Windows): complete — event-driven shared mode, MMCSS "Pro Audio" thread, `WAVEFORMATEXTENSIBLE` float32, `AUDCLNT_E_DEVICE_INVALIDATED` handling.
- PulseAudio backend (Linux): complete — `pa_simple` blocking writes, float32 PCM, dedicated fill thread.
- ALSA backend (Linux fallback): complete — `snd_pcm_open` + `snd_pcm_set_params`, `snd_pcm_recover()` for underruns, dedicated fill thread. CMake auto-selects PulseAudio first.
- Voice playback pipeline, async loading, voice virtualization, and 3D audio are all implemented.
- All filters implemented: `LowPassFilter`, `HighPassFilter`, `EchoFilter`, `ReverbFilter`, `CompressorFilter`, `LimiterFilter`, `ChorusFilter`, `FlangerFilter`, `PitchShiftFilter`.
- `AudioBus` implemented: child routing, bus filter chain, bus visualization, `setSidechain()`/`clearSidechain()`.
- `AudioStream` implemented: ring-buffer streaming with prefetch thread; WAV/OGG/MP3 streaming.
- Resample quality: `Point`, `Linear`, `CatmullRom` — set via `AudioEngineDescriptor::resampleQuality`.
- Surround: VBAP panning for 5.1 (6ch) and 7.1 (8ch) via `src/pi/surround.hpp`; `channels=6/8` in `AudioEngineDescriptor`. Stereo path unchanged.
- **Next step:** Phase 17 — Examples & Documentation.
