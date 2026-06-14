# SwiftUI Demo for campello_audio

A minimal macOS / iOS example app written in SwiftUI that demonstrates the
`campello_audio` engine through an Objective-C++ wrapper.

## What it demonstrates

- **Audio engine init / shutdown**
- **Tone playback** — procedural sine wave (no file assets needed)
- **Live pattern editor** — type mini-notation and hear it played instantly
- **Preloaded sound bank** — kick, snare, hi-hat, clap, and piano tones (synthesized at runtime)
- **RTPC curves** — an "Intensity" slider that modulates pattern event gain in real-time
- **Master volume** — global volume control
- **Voice count** — live display of active voices in the mixer

## Pattern mini-notation

The text editor accepts the same mini-notation syntax as the C++ `PatternCompiler`:

| Example | Description |
|---------|-------------|
| `bd*4 sd` | Kick every beat, snare on beat 3 |
| `hh(7,8)` | 7 hi-hats evenly distributed across 8 steps |
| `stack("bd*4", "sd", "hh*8")` | Layered patterns |
| `"bd*4".gain(0.8).pan(-0.3)` | Per-event gain and pan |
| `slow(2, "bd*4 sd")` | Half speed |
| `fast(2, "bd*4 sd")` | Double speed |
| `c4 d4 e4 f4 g4 a4` | Piano scale |

**Available sounds:** `bd` `sd` `hh` `cp` `c4` `d4` `e4` `f4` `g4` `a4`

## Quick Start — Xcode

1. **Build the library** (from the repo root):
   ```bash
   cmake -B build && cmake --build build
   ```

2. **Open Xcode** → Create New Project → **macOS App** (or iOS App)
   - Interface: **SwiftUI**
   - Language: **Swift**

3. **Add source files** to the project:
   - Drag these 4 files into the project navigator:
     - `CampelloAudioEngine.h`
     - `CampelloAudioEngine.mm`
     - `SwiftUIDemoApp.swift`
     - `ContentView.swift`

4. **Create a bridging header**:
   - File → New → File → Header File → name it `SwiftUIDemo-Bridging-Header.h`
   - Add this line:
     ```objc
     #import "CampelloAudioEngine.h"
     ```
   - Build Settings → **Objective-C Bridging Header** →
     set to `$(SRCROOT)/SwiftUIDemo-Bridging-Header.h`

5. **Configure search paths**:
   - Build Settings → **Header Search Paths** → add:
     ```
     $(SRCROOT)/../../../inc
     $(SRCROOT)/../../../src/pi
     ```
   - Build Settings → **Library Search Paths** → add:
     ```
     $(SRCROOT)/../../../build
     ```

6. **Link the library**:
   - Build Phases → **Link Binary With Libraries** → click **+** →
     **Add Other…** → **Add Files…** → navigate to `build/libcampello_audio.dylib`

7. **Embed the dylib** (macOS only):
   - Build Phases → click **+** → **New Copy Files Phase**
   - Destination: **Frameworks**
   - Drag `libcampello_audio.dylib` into the list

8. **Build and run** (`Cmd+R`)

## Quick Start — Command Line (macOS)

If you prefer not to open Xcode:

```bash
cd examples/apple/swift_ui_demo
./build_macos.sh
```

This produces a `SwiftUIDemo` executable in the current directory.

## Project Layout

```
swift_ui_demo/
├── README.md                 ← this file
├── build_macos.sh            ← command-line build script
├── CampelloAudioEngine.h     ← Objective-C public interface
├── CampelloAudioEngine.mm    ← Objective-C++ wrapper implementation
├── SwiftUIDemoApp.swift      ← SwiftUI @main entry point
└── ContentView.swift         ← SwiftUI view (editor, buttons, sliders, status)
```

## Architecture

```
SwiftUI View (ContentView.swift)
    ↓
Objective-C header (CampelloAudioEngine.h)
    ↓
Objective-C++ wrapper (CampelloAudioEngine.mm)
    ↓
campello_audio C++ API (AudioEngine, PatternCompiler, PatternTrack, etc.)
```

Swift cannot import C++ headers directly, so an Objective-C++ wrapper is the
standard, reliable bridge. The wrapper exposes only plain Objective-C types
(`NSString`, `float`, `BOOL`) that Swift can use natively.

## Sound synthesis

The wrapper generates all drum and piano sounds at runtime using simple
synthesis (sine sweeps, noise bursts, decay envelopes). No external audio files
are required — the entire sound bank fits in ~100 KB of memory.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `campello_audio/audio_engine.hpp` not found | Check Header Search Paths point to `inc/` |
| `pattern/pattern.hpp` not found | Check Header Search Paths include `src/pi/` |
| `libcampello_audio.dylib` not found | Check Library Search Paths point to `build/` |
| `dyld: Library not loaded` | Add the dylib to a **Copy Files** build phase with destination **Frameworks** |
| Bridging header errors | Make sure the bridging header path is correct in Build Settings |
| Pattern compile error | Check the pattern syntax — available sounds: bd, sd, hh, cp, c4, d4, e4, f4, g4, a4 |
