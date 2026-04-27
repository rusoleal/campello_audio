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

## Phase 1 — Third-Party Decoders ✅

Integrate lightweight, header-only / single-file decoders via CMake FetchContent.

- [x] Integrate **dr_wav** (dr_libs) for PCM/WAV decoding
  - FetchContent in `CMakeLists.txt` (FetchContent_Populate; header-only, no CMakeLists.txt)
  - `src/pi/decoder_wav.cpp` — DR_WAV_IMPLEMENTATION + decodeWav / decodeWavMem
  - `WavSource::load()` / `loadMem()` / `getSampleRate()` / `getDuration()` in `src/pi/wav_source.cpp`
- [x] Integrate **stb_vorbis** for OGG Vorbis decoding
  - FetchContent (stb repo) in `CMakeLists.txt`
  - `src/pi/decoder_ogg.cpp` — includes stb_vorbis.c, decodeOgg / decodeOggMem with short→float conversion
  - `OggSource::load()` / `loadMem()` / `getDuration()` in `src/pi/ogg_source.cpp`
- [x] Integrate **dr_mp3** for MP3 decoding
  - `src/pi/decoder_mp3.cpp` — DR_MP3_IMPLEMENTATION + decodeMp3 / decodeMp3Mem
  - `Mp3Source::load()` / `loadMem()` / `getDuration()` in `src/pi/mp3_source.cpp`
- [x] `src/pi/decoder.hpp` — `DecodedBuffer` struct (interleaved float PCM, isValid, getDuration)
- [x] `src/pi/source_handle.hpp` — `AudioSourceHandle` + derived WavSourceHandle / OggSourceHandle / Mp3SourceHandle
- [x] `src/pi/audio_source.cpp` — AudioSource base class (volume, looping, filter chain, RTPC stubs)
- [x] `src/pi/filter.cpp` — Filter base stubs (DSP in Phase 8)
- [x] Universal tests: `test_decoders.cpp`
  - WAV: in-memory minimal WAV buffer, sample rate, duration, reload, invalid data error paths
  - OGG / MP3: file-not-found and garbage-data error paths

---

## Phase 2 — Platform-Independent Mixer Core ✅

Implement `src/pi/mixer.cpp` — the heart of the engine.

- [x] `VoiceManager`: allocate / free / steal voices (LRU eviction for unprotected voices)
- [x] Per-voice sample accumulation loop (float, interleaved)
  - [x] Volume × pan matrix (stereo; extend to 5.1/7.1 in Phase 16)
  - [x] Pitch shift via linear resampler (`ResampleQuality::Linear`)
  - [x] Loop handling (`LoopMode::None`, `Loop`, `PingPong`)
  - [x] Single-instance enforcement
- [x] Master volume application
- [x] Fade/oscillate automation engine
  - [x] `fadeVolume`, `fadePan`, `fadePitch` (linear ramp per sample)
  - [x] `oscillateVolume`, `oscillatePan` (sinusoidal LFO per sample)
- [x] Visualization buffer capture
- [x] Thread-safe voice parameter update (mutex on voiceMutex)
- [x] Universal tests: verify mixing output for known input signals

---

## Phase 3 — Audio Backends

### 3a. CoreAudio (macOS / iOS) ✅

- [x] Open default output `AudioUnit` (kAudioUnitType_Output / kAudioUnitSubType_DefaultOutput)
- [x] Configure `AudioStreamBasicDescription` (Float32, interleaved, engine sample rate)
- [x] Register render callback — calls `MixerData::mixSamples()`
- [x] `AudioOutputUnitStart` / `AudioOutputUnitStop`
- [x] iOS: configure `AVAudioSession` category (`AVAudioSessionCategoryPlayback`)
- [x] Handle audio interruptions (phone calls, Siri) via `AVAudioSessionInterruptionNotification`
- [x] Integration test: `init()` returns true on macOS/iOS hardware

### 3b. AAudio (Android API 26+) ✅

- [x] Create `AAudioStreamBuilder`, configure format (Float, channel count, sample rate)
- [x] Register `AAudioStream_dataCallback` — calls `MixerData::mixSamples()`
- [x] Handle `AAUDIO_ERROR_DISCONNECTED` (headphone unplug) via error callback → `deviceLost` atomic
- [x] Query negotiated sample rate / channel count back from opened stream
- [x] Low-latency shared-mode stream (`AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`)
- [ ] OpenSL ES fallback for API < 26 (out of scope for current phase)
- [ ] Integration test: `init()` returns true on Android

### 3c. WASAPI (Windows) ✅

- [x] CoInitializeEx + `IMMDeviceEnumerator` → default render endpoint
- [x] `IAudioClient::Initialize` (AUDCLNT_SHAREMODE_SHARED, EVENTCALLBACK)
- [x] MMCSS "Pro Audio" thread priority via `AvSetMmThreadCharacteristics`
- [x] `IAudioRenderClient` fill loop on dedicated event-driven thread
- [x] Handle device invalidation (`AUDCLNT_E_DEVICE_INVALIDATED`) → `deviceLost` atomic
- [x] Float32 format negotiation (`WAVEFORMATEXTENSIBLE` / `IsFormatSupported`)
- [ ] Integration test: `init()` returns true on Windows

### 3d. PulseAudio / ALSA (Linux) ✅

- [x] `pa_simple_new` for synchronous blocking writes (`PA_SAMPLE_FLOAT32LE`)
- [x] Dedicated fill thread calling `MixerData::mixSamples()` + `pa_simple_write()`
- [x] ALSA fallback: `snd_pcm_open` + `snd_pcm_set_params` (`SND_PCM_FORMAT_FLOAT_LE`)
- [x] ALSA underrun recovery via `snd_pcm_recover()` in fill thread
- [x] CMake auto-selects PulseAudio if found, falls back to ALSA (`linux.cmake`)
- [ ] Integration test: `init()` returns true on Linux

### 3e. WebAssembly / Emscripten (Web) ✅

- [x] Web Audio AudioContext via `emscripten_create_audio_context()`
- [x] AudioWorklet processor + node (`emscripten_create_wasm_audio_worklet_node()`)
- [x] 128-frame process callback — interleaved mix, then deinterleave to planar
- [x] Pthreads + SharedArrayBuffer support (`-sAUDIO_WORKLET=1 -sPTHREAD_POOL_SIZE=4`)
- [x] `wasm.cmake` with auto-detection via `CMAKE_SYSTEM_NAME STREQUAL Emscripten`
- [x] Integration test: `test_wasm.cpp` (compile-only under Node.js; requires real browser)
- [x] Browser example: `examples/wasm/` with HTML/JS UI and COOP/COEP documentation

---

## Phase 4 — Voice Playback Pipeline ✅

Wire sources → decoder → mixer → backend.

- [x] `AudioEngine::play()` — allocate voice, bind decoded buffer, enqueue to mixer
- [x] `AudioEngine::stop()` / `pause()` / `resume()` — voice lifecycle
- [x] `AudioEngine::seek()` — jump to sample offset in decoded buffer
- [x] `AudioEngine::isValid()` — check voice liveness via generation counter
- [x] `AudioEngine::tick()` — flush deferred async-load callbacks on the calling thread
- [x] `ToneSource` — one waveform period generated at play() time, stored in Voice::ownedBuffer, looped
- [x] Integration tests: play WAV, verify voice count increments; stop, verify it returns to 0

---

## Phase 5 — Async Loading ✅

Non-blocking asset loading — critical to avoid main-thread hitches.

- [x] `inc/campello_audio/types/loader.hpp` — `ByteLoader`, `AsyncByteLoader`, `LoadCallback` type aliases
- [x] `WavSource::load(ByteLoader)` — primary sync overload; `load(path)` and `loadMem()` are thin wrappers
- [x] `OggSource::load(ByteLoader)` / `Mp3Source::load(ByteLoader)` — same pattern
- [x] `AudioEngine::loadAsync(source, AsyncByteLoader, callback)` — calls lambda immediately, stores future
- [x] `AudioEngine::tick()` — polls pending futures (`wait_for(0)`), decodes on resolution, fires callback
- [x] Integration tests: `test_async_load.cpp` — sync loader, empty-bytes failure, deferred future, multi-load

---

## Phase 6 — Voice Virtualization ✅

Keeps inaudible voices alive without spending DSP time on them.

- [x] `MixerData::virtualVoices` — separate pool of logically-playing, DSP-silent voices
- [x] `VoiceManager::allocate()` — virtualizes quietest voice < threshold before LRU steal
- [x] `mixSamples()` sweep — virtualizes real voices that drop below threshold mid-play
- [x] Virtual voice position tracking — readPos advanced every callback (handles Loop/PingPong/None)
- [x] Re-activation — virtual voice promoted back to real pool when volume rises above threshold
- [x] `VoiceManager::findAny/findVirtual/freeVirtual/reactivate` — lookup + lifecycle helpers
- [x] `isValid()`, `stop()`, `stopAll()`, parameter setters — all work on real AND virtual voices
- [x] `AudioEngine::getVirtualVoiceCount()` / `getTotalVoiceCount()` — implemented
- [x] Integration tests: `test_virtualization.cpp` — allocate path, mixer sweep, re-activation, stop, isValid

---

## Phase 7 — 3D Audio ✅

- [x] `src/pi/audio3d.hpp` / `audio3d.cpp` — panner + attenuation
- [x] Per-voice 3D state: position, velocity, min/max distance, rolloff, Doppler factor
- [x] Attenuation models: `None`, `Linear`, `Inverse`, `Exponential`
- [x] Doppler pitch shift (relative velocity along source-listener axis)
- [x] `AudioEngine::update3d()` — recompute pan + attenuation for all 3D voices
- [x] Multi-listener support: `set3dListenerParameters(uint32_t index, ...)`; results summed
- [x] Universal tests: `test_audio3d.cpp` — attenuation models, pan direction, Doppler shift, clamp
- [x] Integration tests: verify volume decreases with distance for `Linear` model

---

## Phase 8 — Filter DSP ✅

- [x] `src/pi/filter_engine.hpp` / `filter_engine.cpp` — per-voice filter chain (up to 8 slots)
- [x] `LowPassFilter` — bi-quad IIR (Audio EQ Cookbook)
- [x] `HighPassFilter` — bi-quad IIR
- [x] `EchoFilter` — delay line with feedback and LP coefficient
- [x] `Filter::setParam()` — immediate value write
- [x] `Filter::fadeParam()` — per-buffer linear ramp automation
- [x] `Filter::oscillateParam()` — sinusoidal LFO automation
- [x] Integration tests: voice with LPF/HPF/Echo plays; setParam/fadeParam/oscillateParam; chained filters
- [x] `ReverbFilter` — Freeverb (8 comb + 4 all-pass per channel)
- [x] `CompressorFilter` — feed-forward RMS compressor with soft knee
- [x] `LimiterFilter` — true-peak brickwall limiter with lookahead
- [x] `ChorusFilter` — multi-voice LFO delay (stereo spread)
- [x] `FlangerFilter` — short modulated delay with feedback
- [x] `PitchShiftFilter` — phase-vocoder / WSOLA pitch shifter

---

## Phase 9 — AudioBus & Sidechain ✅

- [x] `AudioBus` as a virtual AudioSource routed through the mixer hierarchy
- [x] `AudioBus::play()` — routes a child source through the bus
- [x] Bus filter chain applied after child mixing
- [x] Bus visualization data
- [x] `AudioEngine::setSidechain(trigger, target, duckDb, attackSecs, releaseSecs)`
  - RMS-detector on trigger bus output
  - Gain reduction envelope applied to target bus
- [x] `AudioEngine::clearSidechain(target)`
- [x] Integration test: music bus ducks when SFX bus exceeds threshold

---

## Phase 10 — RTPC (Real-Time Parameter Control) ✅

- [x] `AudioParameter` internal store: name → current value
- [x] `AudioEngine::registerParameter()` / `setParameter()` / `getParameter()`
- [x] `AudioSource::bindParameter()` — link parameter to source property via curve
- [x] `src/pi/rtpc.hpp` / `rtpc.cpp` — evaluate curve, write to voice property each tick
- [x] Supported properties: `Volume`, `Pitch`, `Pan`, `FilterParam`, `SendLevel`, `LowPassCutoff`, `HighPassCutoff`
- [x] Curve evaluation: `Linear`, `Exponential`, `Logarithmic`, `SCurve`, `Sine`
- [x] Integration tests: set "speed" parameter, verify engine pitch follows curve

---

## Phase 11 — Mix Snapshots ✅

- [x] `AudioSnapshot` internal representation: bus volumes + filter overrides
- [x] `AudioEngine::registerSnapshot()` / `applySnapshot()` / `revertSnapshot()`
- [x] Blend engine: smooth interpolation of all snapshot properties over `blendTimeSecs`
- [x] Priority system: higher-priority snapshot wins when two are active
- [x] Integration test: apply "underwater" snapshot (LPF + volume), revert, verify properties return

---

## Phase 12 — RandomSource & Variation ✅

- [x] `RandomSource` picks a variant using weighted random selection
- [x] Avoid-repeat logic: exclude last-played variant from next selection
- [x] Per-play pitch offset: uniform random in `[-pitchVariation, +pitchVariation]` semitones
- [x] Per-play volume offset: uniform random in `[-volumeVariation, +volumeVariation]` dB
- [x] Universal tests: selectVariant(), weighted distribution, avoid-repeat, getLastVariantIndex()

---

## Phase 13 — Adaptive Music (MusicTrack) ✅

- [x] `MusicTrack` looping section player with beat clock
- [x] Beat clock: BPM + time signature → sample-accurate beat/bar position
- [x] Section transitions: `Immediate`, `OnBeat`, `OnBar`, `OnNextSection`, `CrossFade`
- [x] `AudioEngine::requestMusicTransition(sectionLabel)` — queued, not immediate
- [x] Integration tests: all 5 transition rules verified; beat clock math verified universally

---

## Phase 14 — Audio Streaming ✅

- [x] `AudioStream` ring-buffer decoder (decode ahead on loader thread, consume in mixer)
- [x] WAV streaming via dr_wav seek + read
- [x] OGG streaming via stb_vorbis
- [x] MP3 streaming via dr_mp3
- [x] Accurate seek in streams (repositions decoder to target frame)
- [x] `AudioStream::getBufferMemoryBytes()` — expose ring buffer footprint
- [x] Integration test: stream a file, verify memory stays under 2 MB

---

## Phase 15 — Resample Quality ✅

- [x] `ResampleQuality::Point` — nearest-neighbor, frac ignored
- [x] `ResampleQuality::Linear` — linear interpolation between adjacent samples
- [x] `ResampleQuality::CatmullRom` — Catmull-Rom cubic spline (4-point)
- [x] Per-engine quality setting in `AudioEngineDescriptor` (`resampleQuality`, default `Linear`)
- [x] Universal tests: Point ignores frac, Linear interpolates, CatmullRom differs from Linear, all modes agree at integer positions

---

## Phase 16 — Surround Sound ✅

- [x] 5.1 channel panning matrix (ITU-R BS.775): FL/FR=±30°, FC=0°, BL/BR=±110°
- [x] 7.1 channel panning matrix: adds SL/SR=±90° and moves rear to ±150°
- [x] VBAP (Vector Base Amplitude Panning) — pairwise constant-power interpolation over sorted speaker arc; `src/pi/surround.hpp`
- [x] `AudioEngineDescriptor::channels` = 6 / 8 supported in mixer; stereo path unchanged
- [x] LFE (ch3) always 0 — bass management left to audio device
- [x] Universal tests: 5.1 center/left/right/LFE, 7.1 center/left/right/LFE

---

## Phase 17 — Examples & Documentation

- [x] macOS AppKit/CMake keyboard sampler example (`examples/apple/macos_keyboard_sampler`)
- [ ] macOS/iOS Xcode example app (Swift + campello_audio)
- [x] Android NativeActivity example (`examples/android`)
- [x] Windows console example (`examples/windows`)
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
