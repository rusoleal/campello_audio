# campello_audio

A C++20, multiplatform audio engine for game development.
Part of the `campello_xxx` library family by Ruben Leal.

Inspired by the design philosophy of [SoLoud](https://github.com/jarikomppa/soloud):
simple "fire and forget" playback for common cases, with full control available when needed.

---

## 🚀 Part of the Campello Engine

This project is a module within the **Campello** ecosystem.

👉 Main repository: https://github.com/rusoleal/campello

Campello is a modular, composable game engine built as a collection of independent libraries.
Each module is designed to work standalone, but integrates seamlessly into the engine runtime.

---

## Platform & Backend Support

| Platform | Backend      | Status   |
|----------|-------------|----------|
| macOS    | CoreAudio   | Complete |
| iOS      | CoreAudio   | Complete |
| Android  | AAudio      | Complete |
| Windows  | WASAPI      | Complete |
| Linux    | PulseAudio / ALSA | Complete |
| Web      | Web Audio (Emscripten) | Complete |

---

## Requirements

- CMake 3.22.1+
- C++20 compiler (Clang 14+, GCC 12+, MSVC 19.34+)
- Platform SDK for the target backend (Xcode, NDK, Windows SDK, Emscripten)

### Dependencies (fetched automatically by CMake)

| Library | Purpose | Source |
|---------|---------|--------|
| [vector_math](https://github.com/rusoleal/vector_math) | 3D vectors, matrices, quaternions | Fetched via CMake FetchContent |

---

## Build

```bash
cmake -B build
cmake --build build
```

### Options

| Option                   | Default | Description                                   |
|--------------------------|---------|-----------------------------------------------|
| `BUILD_TESTS`            | OFF     | Build universal unit tests (no audio device)  |
| `BUILD_INTEGRATION_TESTS`| OFF     | Build platform integration tests (real device)|
| `BUILD_EXAMPLES`         | OFF     | Build example applications                    |
| `BUILD_DOCS`             | OFF     | Generate HTML API reference (requires Doxygen)|

### Platform-specific builds

**macOS**
```bash
cmake -B build && cmake --build build
```

**iOS** (cross-compile from macOS)
```bash
cmake -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-ios
```

**Android** (requires NDK)
```bash
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26
cmake --build build-android
```

**Windows** (Visual Studio generator)
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**Linux**
```bash
# PulseAudio (default if libpulse-dev is installed)
cmake -B build && cmake --build build

# ALSA fallback (auto-selected when PulseAudio is absent)
cmake -B build && cmake --build build
```

**WebAssembly / Emscripten**
```bash
# Requires emsdk activated in your shell
emcmake cmake -B build -DBUILD_TESTS=ON && cmake --build build

# Run universal tests under Node.js
node build/tests/campello_audio_universal_tests.js

# Build the browser example
emcmake cmake -B build -DBUILD_EXAMPLES=ON && cmake --build build
# Serve build/examples/wasm/ with COOP/COEP headers (see examples/wasm/README.md)
```

### Tests

```bash
# Run universal tests (no audio device required)
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Documentation

```bash
# Generate HTML API reference (requires Doxygen + Graphviz)
cmake -B build -DBUILD_DOCS=ON
cmake --build build --target docs

# Open docs/html/index.html in your browser
```

### WASM Notes

- The WASM backend uses **pthreads** and **SharedArrayBuffer**. The hosting page must be served with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` headers.
- Browsers require a **user gesture** (click/tap) to resume the Web Audio AudioContext. Call `engine.play()` from a button handler, or use `emscripten_resume_audio_context_async()` from JavaScript.
- File paths (`WavSource::load(path)`) work only if assets are preloaded into Emscripten's virtual filesystem (`--preload-file`). For dynamic loading, fetch bytes in JS and use `loadMem()`.

### Examples

```bash
# Build platform-specific example applications
cmake -B build -DBUILD_EXAMPLES=ON
cmake --build build
```

---

## API Overview

All public types live in the `systems::leal::campello_audio` namespace.

### Engine Lifecycle

```cpp
#include <campello_audio/audio_engine.hpp>

using namespace systems::leal::campello_audio;

AudioEngine engine;

AudioEngineDescriptor desc;
desc.sampleRate = 44100;
desc.maxVoices  = 32;
desc.channels   = 2;

engine.init(desc);
// ... use engine ...
engine.deinit();
```

---

### Fire and Forget

The simplest usage — play a sound and ignore the handle:

```cpp
#include <campello_audio/wav_source.hpp>

WavSource shot;
shot.load("assets/gunshot.wav");

engine.play(shot);   // returns SoundHandle — safe to ignore
```

---

### Controlled Playback

Store the handle to adjust the voice after it starts:

```cpp
WavSource music;
music.load("assets/theme.wav");

PlayDescriptor pd;
pd.looping = true;
pd.volume  = 0.8f;

SoundHandle h = engine.play(music, pd);

// Later...
engine.fadeVolume(h, 0.0f, 2.0);  // fade out over 2 seconds
engine.stop(h);
```

---

### OGG and MP3

```cpp
#include <campello_audio/ogg_source.hpp>
#include <campello_audio/mp3_source.hpp>

OggSource ogg;
ogg.load("assets/ambient.ogg");
engine.play(ogg);

Mp3Source mp3;
mp3.load("assets/track.mp3");
engine.play(mp3);
```

---

### Procedural Tones

```cpp
#include <campello_audio/tone_source.hpp>

ToneSource beep(WaveForm::Sine, 440.0f);
SoundHandle h = engine.play(beep);
engine.fadeVolume(h, 0.0f, 0.3);   // short beep
```

---

### Mixing Buses

Route groups of sounds through a shared effects chain:

```cpp
#include <campello_audio/audio_bus.hpp>
#include <campello_audio/reverb_filter.hpp>

AudioBus sfxBus;
auto reverb = std::make_shared<ReverbFilter>(0.7f, 0.4f);
sfxBus.addFilter(reverb, 0);

// Play the bus into the main mix
engine.play(sfxBus);

// Route individual sounds through the bus
WavSource explosion;
explosion.load("explosion.wav");
sfxBus.play(explosion);
```

---

### Filters

Up to 8 filters per source or bus. Parameters can be automated:

```cpp
#include <campello_audio/low_pass_filter.hpp>

WavSource rain;
rain.load("rain.wav");

auto lpf = std::make_shared<LowPassFilter>(3000.0f, 0.707f);
rain.addFilter(lpf, 0);

SoundHandle h = engine.play(rain);

// Smoothly sweep the cutoff (underwater effect)
lpf->fadeParam(LowPassFilter::PARAM_CUTOFF, 500.0f, 1.5);

// Remove filter
rain.removeFilter(0);
```

Available filters:

| Class               | Effect                                     |
|---------------------|--------------------------------------------|
| `LowPassFilter`     | Attenuate high frequencies (bi-quad IIR)   |
| `HighPassFilter`    | Attenuate low frequencies (bi-quad IIR)    |
| `EchoFilter`        | Delay-line echo / feedback                 |
| `ReverbFilter`      | Freeverb algorithmic room reverb           |
| `CompressorFilter`  | Dynamic range compressor (soft-knee RMS)   |
| `LimiterFilter`     | True-peak brickwall limiter with lookahead |
| `ChorusFilter`      | Multi-voice chorus (stereo thickening)     |
| `FlangerFilter`     | Modulated comb-filter flanger              |
| `PitchShiftFilter`  | Pitch shift without tempo change (WSOLA)   |

---

### Audio Streaming

For large files (music, long ambience) use `AudioStream` to decode on the fly
without loading the entire file into RAM:

```cpp
#include <campello_audio/audio_stream.hpp>

AudioStream music;
music.open("assets/music/theme.ogg");
music.setLooping(true);
engine.play(music);
```

---

### Sound Variation (RandomSource)

Prevent the "machine gun" effect by pooling several variants:

```cpp
#include <campello_audio/random_source.hpp>

RandomSource gunshot;
for (auto& p : {"shot1.wav", "shot2.wav", "shot3.wav"}) {
    auto v = std::make_shared<WavSource>();
    v->load(p);
    gunshot.addVariant(v);
}
gunshot.setPitchVariation(2.0f);   // ±2 semitones
gunshot.setVolumeVariation(2.0f);  // ±2 dB
gunshot.setAvoidRepeat(true);

engine.play(gunshot);  // picks a different variant each call
```

---

### Dynamics (Compressor & Limiter)

Place a compressor on a bus to prevent layered sounds from clipping,
and a limiter on the master output to satisfy platform certification:

```cpp
#include <campello_audio/compressor_filter.hpp>
#include <campello_audio/limiter_filter.hpp>

// SFX bus compressor
AudioBus sfxBus;
auto comp = std::make_shared<CompressorFilter>(-12.0f, 4.0f, 5.0f, 50.0f);
sfxBus.addFilter(comp, 0);

// Master output limiter (on the main mix bus)
AudioBus masterBus;
auto limiter = std::make_shared<LimiterFilter>(-0.3f, 5.0f, 50.0f);
masterBus.addFilter(limiter, 0);
```

---

### Sidechain / Ducking

Automatically lower music when dialogue plays:

```cpp
// Dialogue bus triggers ducking of music bus
engine.setSidechain(&dialogueBus, &musicBus,
                    -12.0f,   // duck by 12 dB
                    0.05f,    // 50 ms attack
                    0.5f);    // 500 ms release
```

---

### Async Loading

Avoid main-thread hitches by loading assets in the background:

```cpp
auto shot = std::make_shared<WavSource>();
shot->loadAsync("gunshot.wav", [&](bool ok) {
    // Called on the main thread during engine.tick()
    if (ok) engine.play(*shot);
});

// In your game loop:
engine.tick();  // drains the callback queue
```

---

### RTPC — Real-Time Parameter Control

Bind named game parameters to audio properties using configurable curves:

```cpp
#include <campello_audio/audio_parameter.hpp>

// Register a "speed" parameter ranging 0–200 km/h
auto speed = std::make_shared<AudioParameter>("speed", 0.0f, 200.0f);
engine.registerParameter(speed);

// Bind it: engine pitch 0.5× at rest, 2.0× at max speed (exponential curve)
engineSound.bindParameter("speed", AudioSourceProperty::Pitch,
                          CurveType::Exponential, 0.5f, 2.0f);

// Drive it from game code each frame — no audio code required
engine.setParameter("speed", vehicle.getSpeed());
```

---

### Mix Snapshots

Switch global audio moods smoothly:

```cpp
#include <campello_audio/audio_snapshot.hpp>

auto underwater = std::make_shared<AudioSnapshot>("underwater");
underwater->setGlobalVolume(0.6f);
underwater->setBusFilter(&sfxBus, std::make_shared<LowPassFilter>(400.0f), 0);
engine.registerSnapshot(underwater);

// Player dives
engine.applySnapshot("underwater", 1.5);   // blend in over 1.5 s

// Player surfaces
engine.revertSnapshot(1.0);                // blend out over 1.0 s
```

---

### Adaptive Music

Define sections and transitions; the engine handles the timing:

```cpp
#include <campello_audio/music_track.hpp>

auto track = std::make_shared<MusicTrack>();
track->setBpm(120.0f);
track->setTimeSignature(4, 4);

auto explore = std::make_shared<AudioStream>(); explore->open("explore.ogg");
auto combat  = std::make_shared<AudioStream>(); combat->open("combat.ogg");

track->addSection(explore, "explore");
track->addSection(combat,  "combat");
track->addTransition("explore", "combat", TransitionRule::OnBar,  0.5);
track->addTransition("combat",  "explore",TransitionRule::OnBar,  1.0);

engine.play(*track);

// When the player enters combat — transition fires at next bar boundary
engine.requestMusicTransition("combat");
```

---

### 3D Positional Audio

```cpp
// Call once per frame with the current listener state
ListenerDescriptor listener;
listener.position = {camX, camY, camZ};
listener.forward  = {fwdX, fwdY, fwdZ};
listener.up       = {0.0f, 1.0f, 0.0f};
engine.set3dListenerParameters(listener);

// Play a 3D-positioned sound
PlayDescriptor pd3d;
pd3d.enable3d    = true;
pd3d.position    = {10.0f, 0.0f, -5.0f};
pd3d.minDistance = 1.0f;
pd3d.maxDistance = 50.0f;

WavSource gunshot;
gunshot.load("gunshot.wav");
SoundHandle h = engine.play(gunshot, pd3d);

// Update source position each frame
engine.set3dSourceParameters(h, newX, newY, newZ);

// Apply 3D calculations — call once per frame before audio processing
engine.update3d();
```

---

### Fade & Oscillate

```cpp
// Smooth volume fade
engine.fadeVolume(h, 0.0f, 2.0);

// Pan sweep
engine.fadePan(h, -1.0f, 1.0);

// LFO volume tremolo
engine.oscillateVolume(h, 0.5f, 1.0f, 0.25);   // 4 Hz tremolo
```

---

### Visualization

```cpp
AudioEngineDescriptor desc;
desc.visualization = true;
engine.init(desc);

// Each frame
uint32_t count;
const float* samples = engine.getVisualizationData(count);
// samples contains the last mixed buffer (count floats)
```

---

## Key Types

| Category         | Types |
|------------------|-------|
| Engine           | `AudioEngine`, `AudioEngineDescriptor` |
| Sources          | `WavSource`, `OggSource`, `Mp3Source`, `ToneSource`, `AudioStream`, `RandomSource`, `AudioBus`, `MusicTrack` |
| Playback         | `SoundHandle`, `PlayDescriptor` |
| 3D Audio         | `ListenerDescriptor`, `AttenuationModel` |
| Math             | `vector_math::Vector3<float>` (aliased as `Vec3` in descriptors) |
| Filters          | `LowPassFilter`, `HighPassFilter`, `EchoFilter`, `ReverbFilter`, `CompressorFilter`, `LimiterFilter`, `ChorusFilter`, `FlangerFilter`, `PitchShiftFilter` |
| RTPC             | `AudioParameter`, `AudioSourceProperty`, `CurveType` |
| Snapshots        | `AudioSnapshot` |
| Constants        | `WaveForm`, `LoopMode`, `ResampleQuality`, `TransitionRule` |

---

## Namespace

All public types are in `systems::leal::campello_audio`.

---

## License

See [LICENSE](LICENSE).
