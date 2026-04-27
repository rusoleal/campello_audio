# Changelog

## [Unreleased]

### Added
- **Phase 0 — Project Scaffold**: public API headers, CMake build system with per-platform dispatch, CI pipeline, universal test suite.
- **Phase 1 — Third-Party Decoders**: integrated `dr_wav`, `stb_vorbis`, `dr_mp3` for WAV/OGG/MP3 decoding.
- **Phase 2 — Platform-Independent Mixer Core**: voice manager with LRU stealing, per-voice mixing (volume × pan, linear resampler, loop modes), fade/LFO automation, visualization capture.
- **Phase 3 — Audio Backends**:
  - CoreAudio (macOS / iOS) with `AVAudioSession` interruption handling.
  - AAudio (Android API 26+) with low-latency shared mode and disconnect handling.
  - WASAPI (Windows) with MMCSS "Pro Audio" thread priority and device invalidation recovery.
  - PulseAudio / ALSA (Linux) with auto-selection and ALSA underrun recovery.
  - **WebAssembly / Emscripten** — Web Audio AudioWorklet backend with pthreads support.
- **Phase 4 — Voice Playback Pipeline**: `play()`, `stop()`, `pause()`, `resume()`, `seek()`, `ToneSource` generation.
- **Phase 5 — Async Loading**: `loadAsync()` with future-based deferred decoding, flushed in `tick()`.
- **Phase 6 — Voice Virtualization**: quiet voices moved to a virtual pool, position-tracked, reactivated when audible.
- **Phase 7 — 3D Audio**: attenuation models (linear, inverse, exponential), Doppler shift, multi-listener support.
- **Phase 8 — Filter DSP**: bi-quad IIR (LPF/HPF), echo, Freeverb reverb, RMS compressor, brickwall limiter, chorus, flanger, pitch shifter (WSOLA).
- **Phase 9 — AudioBus & Sidechain**: bus routing with filter chains, RMS sidechain ducking.
- **Phase 10 — RTPC**: `AudioParameter` registration, curve-based binding to source properties.
- **Phase 11 — Mix Snapshots**: named snapshot registry, smooth blend/revert with priority system.
- **Phase 12 — RandomSource**: weighted random selection, avoid-repeat, per-play pitch/volume variation.
- **Phase 13 — Adaptive Music**: `MusicTrack` with beat clock, section transitions (Immediate, OnBeat, OnBar, OnNextSection, CrossFade).
- **Phase 14 — Audio Streaming**: ring-buffer streaming for WAV/OGG/MP3 with accurate seek.
- **Phase 15 — Resample Quality**: Point, Linear, Catmull-Rom cubic spline resamplers.
- **Phase 16 — Surround Sound**: 5.1 and 7.1 channel panning with VBAP.
- **Documentation**: Doxygen configuration for HTML API reference (`BUILD_DOCS` CMake option).
- **Examples**: macOS keyboard sampler, Android NativeActivity, Windows console, WASM browser.
