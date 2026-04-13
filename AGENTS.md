# AGENTS.md

Supplementary guidance for AI coding agents (Copilot, Cursor, etc.) working in this repository.
See `CLAUDE.md` for the authoritative reference — this file adds reminders specific to agent workflows.

---

## Before You Start Any Task

1. Read `CLAUDE.md` fully — it contains the architecture, coding conventions, and current status.
2. Read `TODO.md` to understand which phase is active and what has already been done.
3. Run universal tests to confirm the baseline builds cleanly:
   ```bash
   cmake -B build -DBUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure
   ```

---

## What Not to Do

- **Never add platform headers to `inc/`** — CoreAudio, AAudio, WASAPI, and ALSA headers belong only in `src/<backend>/`.
- **Never throw exceptions** — return `bool` or `nullptr` on failure everywhere.
- **Never write to `MEMORY.md` manually** — that is managed by the memory system.
- **Do not rewrite the public API** unless a TODO item explicitly calls for it.
- **Do not implement multiple phases in one PR** — keep changes scoped to one phase.
- **Do not rename `void* native`** — this is a deliberate pattern shared across campello_xxx libraries.
- **Do not define local Vec2/Vec3/Vec4/Matrix/Quaternion types** — always use `systems::leal::vector_math::Vector3<float>` (and other types) from the `vector_math` dependency. This library is maintained by the same author and must stay the single source of truth for math types across all campello_xxx libraries.

---

## Adding a New Decoder (Phase 1 template)

1. Add a `FetchContent_Declare` entry in `CMakeLists.txt` for the library.
2. Create `src/pi/decoder.hpp` with a `DecodedBuffer` struct:
   ```cpp
   struct DecodedBuffer {
       std::vector<float> samples;  // interleaved float PCM
       uint32_t           channels;
       uint32_t           sampleRate;
       uint64_t           frameCount;
   };
   ```
3. Implement `DecodedBuffer decodeWav(const std::string& path)` in `src/pi/decoder.cpp`.
4. Wire the result into `WavSource::native` (a `DecodedBuffer*` cast from `void*`).
5. Add a universal test that checks `frameCount > 0` for a known test asset.

---

## Adding a New Backend (Phase 3 template)

1. Create `src/<backend>/common.hpp` with a `<Backend>Data` struct containing:
   - The platform API handle(s) (e.g., `AudioComponentInstance`, `AAudioStream*`)
   - A `pi::MixerData mixer` member
2. Create `src/<backend>/audio_engine.cpp` implementing every `AudioEngine::` method.
3. The render/fill callback must call `data->mixer.mixSamples(out, frames)`.
4. Update `<platform>.cmake` to compile the new `src/<backend>/*.cpp` files.
5. Add an integration test that calls `engine.init({})` and checks the return value.

---

## Adding a New Filter (Phase 6 template)

1. Add `inc/campello_audio/<name>_filter.hpp` following the pattern of `low_pass_filter.hpp`.
2. Define `PARAM_*` constants for each tweakable parameter.
3. Implement the DSP in `src/pi/filter_engine.cpp` — one processing function per filter type.
4. Wire the filter into the mixer's filter chain in `src/pi/mixer.cpp`.
5. Add a universal construction test in `tests/platform/test_filters.cpp`.
6. Document the parameter table in the header's `@par Parameters` block.

---

## Commit Discipline

- One logical change per commit.
- Commit message format: `phase<N>: <short description>` (e.g., `phase1: integrate dr_wav decoder`).
- All universal tests must pass before committing.
- Do not commit platform-specific `.cmake` changes without also updating `ci.yml`.

---

## File Locations Quick Reference

| What                        | Where |
|-----------------------------|-------|
| Public API headers          | `inc/campello_audio/` |
| Enums                       | `inc/campello_audio/constants/` |
| Config structs              | `inc/campello_audio/descriptors/` |
| Shared types (Vec3, Handle) | `inc/campello_audio/types/` |
| Platform-independent mixer  | `src/pi/` |
| CoreAudio backend           | `src/coreaudio/` |
| AAudio backend              | `src/aaudio/` |
| WASAPI backend              | `src/wasapi/` |
| PulseAudio backend          | `src/pulse/` |
| ALSA backend                | `src/alsa/` |
| Universal tests             | `tests/universal/` |
| Integration tests           | `tests/platform/` |
