# campello_audio — Development TODO

Phases are sequential. Each phase builds on the previous.
Check off tasks as they are completed.

---

## Phase 0 — Project Scaffold ✅

- [x] Directory structure mirroring campello_gpu conventions
- [x] Root `CMakeLists.txt` with platform dispatch
- [x] `CMakeLists.txt` FetchContent for `vector_math` dependency
- [x] Per-platform cmake files: `macos.cmake`, `ios.cmake`, `android.cmake`, `windows.cmake`, `linux.cmake`
- [x] Version config template (`src/campello_audio_config.h.in`)
- [x] Public API headers (`inc/campello_audio/`)
  - [x] Constants: `attenuation_model.hpp`, `wave_form.hpp`, `loop_mode.hpp`, `resample_quality.hpp`, `transition_rule.hpp`, `curve_type.hpp`
  - [x] Descriptors: `audio_engine_descriptor.hpp` (virtualization + multi-listener), `play_descriptor.hpp`, `listener_descriptor.hpp`
  - [x] Types: `sound_handle.hpp` (Vec3 provided by `vector_math` dependency)
  - [x] Sources: `audio_source.hpp` (+ RTPC binding), `wav_source.hpp`, `ogg_source.hpp`, `mp3_source.hpp`, `tone_source.hpp` (+ loadAsync)
  - [x] Streaming: `audio_stream.hpp`
  - [x] Variation: `random_source.hpp`
  - [x] Bus: `audio_bus.hpp`
  - [x] Filters: `filter.hpp`, `low_pass_filter.hpp`, `high_pass_filter.hpp`, `echo_filter.hpp`, `reverb_filter.hpp`, `compressor_filter.hpp`, `limiter_filter.hpp`, `chorus_filter.hpp`, `flanger_filter.hpp`, `pitch_shift_filter.hpp`
  - [x] RTPC: `audio_parameter.hpp` (`AudioParameter`, `AudioSourceProperty`)
  - [x] Snapshots: `audio_snapshot.hpp`
  - [x] Adaptive music: `music_track.hpp`
  - [x] Engine: `audio_engine.hpp` (sidechain, RTPC, snapshots, multi-listener, `tick()`, `requestMusicTransition()`)
- [x] `tests/CMakeLists.txt` with universal + integration targets
- [x] Universal tests: `test_constants`, `test_descriptors`, `test_math` (vector_math smoke test), `test_sound_handle`
- [x] Integration test stubs: `test_engine`, `test_playback`, `test_3d`, `test_filters`
- [x] GitHub Actions CI (`ci.yml`)
- [x] `README.md`, `TODO.md`, `CLAUDE.md`, `AGENTS.md`, `CHANGELOG.md`

---

## Phase 1 — Third-Party Decoders

Integrate lightweight, header-only / single-file decoders via CMake FetchContent.

- [ ] Integrate **dr_wav** (dr_libs) for PCM/WAV decoding
  - Add `FetchContent_Declare(dr_libs ...)` to CMakeLists.txt
  - Implement `WavSource::load()` / `loadMem()` using `drwav_open_file` / `drwav_open_memory`
  - Implement `WavSource::getSampleRate()` and `getDuration()`
- [ ] Integrate **stb_vorbis** for OGG Vorbis decoding
  - Implement `OggSource::load()` / `loadMem()` using `stb_vorbis_open_filename` / `stb_vorbis_open_memory`
  - Implement `OggSource::getDuration()`
- [ ] Integrate **dr_mp3** for MP3 decoding
  - Implement `Mp3Source::load()` / `loadMem()` using `drmp3_open_file` / `drmp3_open_memory`
  - Implement `Mp3Source::getDuration()`
- [ ] Add `src/pi/decoder.hpp` / `decoder.cpp` with a common `DecodedBuffer` struct (interleaved float PCM)
- [ ] Universal tests: verify sample-count, sample-rate, and channel count for test assets

---

## Phase 2 — Platform-Independent Mixer Core

Implement `src/pi/mixer.cpp` — the heart of the engine.

- [ ] `VoiceManager`: allocate / free / steal voices (LRU eviction for unprotected voices)
- [ ] Per-voice sample accumulation loop (float, interleaved)
  - [ ] Volume × pan matrix (stereo; extend to 5.1/7.1 in Phase 9)
  - [ ] Pitch shift via linear resampler (`ResampleQuality::Linear`)
  - [ ] Loop handling (`LoopMode::None`, `Loop`, `PingPong`)
  - [ ] Single-instance enforcement
- [ ] Master volume application
- [ ] Fade/oscillate automation engine
  - [ ] `fadeVolume`, `fadePan`, `fadePitch` (linear ramp per sample)
  - [ ] `oscillateVolume`, `oscillatePan` (sinusoidal LFO per sample)
- [ ] Visualization buffer capture
- [ ] Thread-safe voice parameter update (lock-free double-buffer or mutex)
- [ ] Universal tests: verify mixing output for known input signals

---

## Phase 3 — Audio Backends

### 3a. CoreAudio (macOS / iOS)

- [ ] Open default output `AudioUnit` (kAudioUnitType_Output / kAudioUnitSubType_DefaultOutput)
- [ ] Configure `AudioStreamBasicDescription` (Float32, interleaved, engine sample rate)
- [ ] Register render callback — calls `MixerData::mixSamples()`
- [ ] `AudioOutputUnitStart` / `AudioOutputUnitStop`
- [ ] iOS: configure `AVAudioSession` category (`AVAudioSessionCategoryPlayback`)
- [ ] Handle audio interruptions (phone calls, Siri) via `AVAudioSessionInterruptionNotification`
- [ ] Integration test: `init()` returns true on macOS/iOS hardware

### 3b. AAudio (Android API 26+)

- [ ] Create `AAudioStreamBuilder`, configure format (Float, channel count, sample rate)
- [ ] Register `AAudioStream_dataCallback` — calls `MixerData::mixSamples()`
- [ ] Implement OpenSL ES fallback for API < 26
- [ ] Handle `AAUDIO_ERROR_DISCONNECTED` (headphone unplug)
- [ ] Integration test: `init()` returns true on Android

### 3c. WASAPI (Windows)

- [ ] CoInitializeEx + `IMMDeviceEnumerator` → default render endpoint
- [ ] `IAudioClient::Initialize` (AUDCLNT_SHAREMODE_SHARED)
- [ ] `IAudioRenderClient` fill loop on dedicated thread
- [ ] Handle device invalidation (`AUDCLNT_E_DEVICE_INVALIDATED`)
- [ ] Integration test: `init()` returns true on Windows

### 3d. PulseAudio / ALSA (Linux)

- [ ] `pa_simple_new` for synchronous blocking writes
- [ ] Dedicated fill thread calling `MixerData::mixSamples()` + `pa_simple_write()`
- [ ] ALSA fallback when PulseAudio is unavailable
- [ ] Integration test: `init()` returns true on Linux

---

## Phase 4 — Voice Playback Pipeline

Wire sources → decoder → mixer → backend.

- [ ] `AudioEngine::play()` — allocate voice, bind decoded buffer, enqueue to mixer
- [ ] `AudioEngine::stop()` / `pause()` / `resume()` — voice lifecycle
- [ ] `AudioEngine::seek()` — jump to sample offset in decoded buffer
- [ ] `AudioEngine::isValid()` — check voice liveness via generation counter
- [ ] `AudioEngine::tick()` — flush deferred async-load callbacks on the calling thread
- [ ] `ToneSource` real-time sample generation (no pre-decode; generated per callback)
- [ ] Integration tests: play WAV, verify voice count increments; stop, verify it returns to 0

---

## Phase 5 — Async Loading

Non-blocking asset loading — critical to avoid main-thread hitches.

- [ ] `src/pi/loader.hpp` / `loader.cpp` — background thread pool (1–2 threads)
- [ ] `WavSource::loadAsync(path, callback)` — decode on loader thread, post callback to tick queue
- [ ] `OggSource::loadAsync(path, callback)`
- [ ] `Mp3Source::loadAsync(path, callback)`
- [ ] `AudioEngine::tick()` — drain callback queue, invoke on calling thread
- [ ] Integration tests: load 10 sounds concurrently, verify all callbacks fire before timeout

---

## Phase 6 — Voice Virtualization

Keeps inaudible voices alive without spending DSP time on them.

- [ ] Extend `VoiceManager` with a virtual-voice pool (tracked but not mixed)
- [ ] Virtualize voices whose computed volume < `AudioEngineDescriptor::virtualizeThreshold`
- [ ] Re-activate a virtual voice (resume from saved playback position) when it becomes audible
- [ ] `AudioEngine::getVirtualVoiceCount()` metric
- [ ] Integration tests: spawn 300 voices, verify active count ≤ maxVoices, virtual count ≤ maxVirtualVoices

---

## Phase 7 — 3D Audio

- [ ] `src/pi/audio3d.hpp` / `audio3d.cpp` — panner + attenuation
- [ ] Per-voice 3D state: position, velocity, min/max distance, rolloff, Doppler factor
- [ ] Attenuation models: `None`, `Linear`, `Inverse`, `Exponential`
- [ ] Doppler pitch shift (relative velocity along source-listener axis)
- [ ] `AudioEngine::update3d()` — recompute pan + attenuation for all 3D voices
- [ ] Multi-listener support: `set3dListenerParameters(uint32_t index, ...)`; results summed
- [ ] Integration tests: verify volume decreases with distance for `Linear` model

---

## Phase 8 — Filter DSP

- [ ] `src/pi/filter_engine.cpp` — per-voice filter chain (up to 8 slots)
- [ ] `LowPassFilter` — bi-quad IIR (Audio EQ Cookbook)
- [ ] `HighPassFilter` — bi-quad IIR
- [ ] `EchoFilter` — delay line with feedback and LP coefficient
- [ ] `ReverbFilter` — Freeverb (8 comb + 4 all-pass per channel)
- [ ] `CompressorFilter` — feed-forward RMS compressor with soft knee
- [ ] `LimiterFilter` — true-peak brickwall limiter with lookahead
- [ ] `ChorusFilter` — multi-voice LFO delay (stereo spread)
- [ ] `FlangerFilter` — short modulated delay with feedback
- [ ] `PitchShiftFilter` — phase-vocoder / WSOLA pitch shifter
- [ ] `Filter::fadeParam()` — per-sample linear ramp
- [ ] `Filter::oscillateParam()` — sinusoidal LFO
- [ ] Integration tests: verify LPF reduces high-frequency energy (FFT comparison)

---

## Phase 9 — AudioBus & Sidechain

- [ ] `AudioBus` as a virtual AudioSource routed through the mixer hierarchy
- [ ] `AudioBus::play()` — routes a child source through the bus
- [ ] Bus filter chain applied after child mixing
- [ ] Bus visualization data
- [ ] `AudioEngine::setSidechain(trigger, target, duckDb, attackSecs, releaseSecs)`
  - RMS-detector on trigger bus output
  - Gain reduction envelope applied to target bus
- [ ] `AudioEngine::clearSidechain(target)`
- [ ] Integration test: music bus ducks when SFX bus exceeds threshold

---

## Phase 10 — RTPC (Real-Time Parameter Control)

- [ ] `AudioParameter` internal store: name → current value
- [ ] `AudioEngine::registerParameter()` / `setParameter()` / `getParameter()`
- [ ] `AudioSource::bindParameter()` — link parameter to source property via curve
- [ ] `src/pi/rtpc.hpp` / `rtpc.cpp` — evaluate curve, write to voice property each tick
- [ ] Supported properties: `Volume`, `Pitch`, `Pan`, `FilterParam`, `SendLevel`, `LowPassCutoff`, `HighPassCutoff`
- [ ] Curve evaluation: `Linear`, `Exponential`, `Logarithmic`, `SCurve`, `Sine`
- [ ] Integration tests: set "speed" parameter, verify engine pitch follows curve

---

## Phase 11 — Mix Snapshots

- [ ] `AudioSnapshot` internal representation: bus volumes + filter overrides
- [ ] `AudioEngine::registerSnapshot()` / `applySnapshot()` / `revertSnapshot()`
- [ ] Blend engine: smooth interpolation of all snapshot properties over `blendTimeSecs`
- [ ] Priority system: higher-priority snapshot wins when two are active
- [ ] Integration test: apply "underwater" snapshot (LPF + volume), revert, verify properties return

---

## Phase 12 — RandomSource & Variation

- [ ] `RandomSource` picks a variant using weighted random selection
- [ ] Avoid-repeat logic: exclude last-played variant from next selection
- [ ] Per-play pitch offset: uniform random in `[-pitchVariation, +pitchVariation]` semitones
- [ ] Per-play volume offset: uniform random in `[-volumeVariation, +volumeVariation]` dB
- [ ] Integration tests: play 100 times, verify statistical distribution of variants

---

## Phase 13 — Adaptive Music (MusicTrack)

- [ ] `MusicTrack` looping section player with beat clock
- [ ] Beat clock: BPM + time signature → sample-accurate beat/bar position
- [ ] Section transitions: `Immediate`, `OnBeat`, `OnBar`, `OnNextSection`, `CrossFade`
- [ ] `AudioEngine::requestMusicTransition(sectionLabel)` — queued, not immediate
- [ ] Integration test: set BPM to 120, request `OnBar` transition, verify it fires at correct sample offset

---

## Phase 14 — Audio Streaming

- [ ] `AudioStream` ring-buffer decoder (decode ahead on loader thread, consume in mixer)
- [ ] WAV streaming via dr_wav seek + read
- [ ] OGG streaming via stb_vorbis
- [ ] MP3 streaming via dr_mp3
- [ ] Accurate seek in streams (repositions decoder to target frame)
- [ ] `AudioStream::getBufferMemoryBytes()` — expose ring buffer footprint
- [ ] Integration test: stream a 10-minute OGG file, verify memory stays under 2 MB

---

## Phase 15 — Resample Quality

- [ ] `ResampleQuality::Point` (already done as default)
- [ ] `ResampleQuality::Linear` — linear interpolation between samples
- [ ] `ResampleQuality::CatmullRom` — Catmull-Rom cubic interpolation
- [ ] Per-engine quality setting in `AudioEngineDescriptor`

---

## Phase 16 — Surround Sound

- [ ] 5.1 channel panning matrix (ITU-R BS.775)
- [ ] 7.1 channel panning matrix
- [ ] VBAP (Vector Base Amplitude Panning) for arbitrary speaker layouts
- [ ] `AudioEngineDescriptor::channels` = 6 / 8 support in backends

---

## Phase 17 — Examples & Documentation

- [ ] macOS/iOS Xcode example app (Swift + campello_audio)
- [ ] Android example app (NDK + campello_audio via AAudio)
- [ ] Windows example (Win32 + campello_audio via WASAPI)
- [ ] Doxygen configuration and HTML generation
- [ ] Full API reference in README

---

## Backlog / Nice-to-Have

- [ ] HRTF / binaural spatializer (Steam Audio / Resonance Audio integration)
- [ ] Geometry-based acoustic simulation (occlusion, portals, room reverb)
- [ ] Convolution reverb (impulse response loading and processing)
- [ ] LUFS loudness metering (EBU R128 / ITU-R BS.1770)
- [ ] Platform-specific backends: Sony Tempest (PS5), XAudio2 (Xbox), Nintendo Switch audio
- [ ] C API wrapper for use from C / other language bindings
- [ ] Python / Lua bindings
- [ ] Hot-reload of audio assets in debug builds
- [ ] Asset bundle / pak file integration
- [ ] MIDI playback via platform synthesizer
- [ ] Per-voice audio latency measurement / reporting
