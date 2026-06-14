# Pattern Syntax Reference

> How to write pattern expressions for `campello_audio`'s `PatternCompiler`.

---

## Overview

The `PatternCompiler` accepts a **subset** of [strudel.cc](https://strudel.cc/) / [TidalCycles](https://tidalcycles.org/) syntax. Expressions compile into a flat `Pattern` timeline that the runtime queries on the audio thread.

```cpp
pi::PatternCompiler compiler;
compiler.registerSource("bd", kickSource);
compiler.registerSource("sd", snareSource);
compiler.registerSource("hh", hihatSource);

auto pattern = compiler.compile(R"(sound("bd*4 sd").gain(0.8))", 4.0);
```

---

## Two-Level Grammar

The compiler has two nested parsers:

| Level | Parser | What it handles |
|-------|--------|-----------------|
| **Expression** | `ExprTokenizer` + `parseExpression()` | Functions (`sound()`, `stack()`), method chains (`.gain()`), quoted strings, numbers |
| **Mini-notation** | `Tokenizer` + `parseMiniNotation()` | Raw rhythm strings like `bd*4 sd [hh oh]` |

A quoted string inside an expression is handed to the mini-notation parser. So `"bd*4 sd"` is an expression-level string literal whose contents are parsed as mini-notation.

---

## Mini-Notation (Inside String Literals)

Mini-notation describes a single cycle of events. Tokens are space-separated.

| Syntax | Meaning | Example | Produces |
|--------|---------|---------|----------|
| `word` | Play sample `word` once | `bd` | 1 event |
| `word* n` | Repeat `word` n times | `bd*4` | 4 evenly spaced events |
| `[a b c]` | Group — treated as one slot, subdivided | `[bd sd]` | bd and sd each get half the group's time |
| `word(hits,steps)` | Euclidean rhythm | `hh(3,8)` | 3 hits distributed across 8 steps |
| `~` or `.` | Rest (silence) | `bd ~ sd ~` | bd at 0, sd at 2 |
| `a b c` | Sequence — evenly distributed | `bd sd hh` | 3 events at 0, 1.33, 2.67 |
| `<a b c>` | **Slow cat** — one element per cycle, pattern length expands | `<bd sd>` | bd@0, sd@4 (length=8) |
| `a,b` | **Parallel** — both play simultaneously | `bd,sd` | bd@0, sd@0 |
| `a/2` | **Slow** — element gets 2× the time | `bd/2 sd` | bd gets 2/3, sd gets 1/3 |
| `a?` | **Probability** — 50% chance (default) | `bd? sd` | bd.prob=0.5, sd.prob=1.0 |
| `a?0.3` | **Probability** — custom chance | `bd?0.3` | bd.prob=0.3 |

### Mini-Notation Examples

```
"bd sd"              → bd@0, sd@2          (in a 4-beat cycle)
"bd*4"               → bd@0,1,2,3
"[bd sd] hh"         → bd@0, sd@1, hh@2    (group takes 2 beats, then hh)
"hh(3,8)"            → hh@0, 1.33, 2.67    (3 hits in 8 steps)
"bd ~ sd ~"          → bd@0, sd@2
"bd*2 [sd hh]"       → bd@0, bd@1, sd@2, hh@3
"<bd sd>"            → bd@0, sd@4          (pattern length = 8)
"<bd sd>, hh"        → bd@0, hh@0, sd@4, hh@4
"bd/2 sd"            → bd@0, sd@2.667      (bd gets 2/3 of cycle)
"bd?0.3 sd"          → bd@0 (prob 0.3), sd@2.667
```

### Mini-Notation → NOT Supported

These strudel/Tidal features are **not** implemented:

| Feature | Strudel Syntax | Status |
|---------|---------------|--------|
| Slow cat nesting inside groups | `[<a b> c]` | ⚠️ Acts like regular group (no expansion) |
| Nested slow cats | `<a <b c>>` | ❌ Only outermost `< >` expands |

---

## Expression-Level Functions

These operate on Patterns (the result of parsing mini-notation).

### `sound(string)`

Parses a mini-notation string into a Pattern. This is the entry point for strudel-style syntax.

```
sound("bd*4 sd")
sound("<bd sd>").gain(0.8)
```

### `stack(pattern, pattern, ...)`

Layers multiple patterns on top of each other. Result length = max(child lengths). Shorter children are scaled up.

```
stack("bd*4", "sd", "hh*8")
stack("<bd sd>", "hh*8")   // result length = 8 (from slow cat)
```

### `cat(pattern, pattern, ...)`

Concatenates patterns end-to-end. Result length = sum of child lengths.

```
cat("bd*4", "sd")          // bd*4 in [0,4), sd at 4. Total = 8.
cat("bd", "sd", "hh")      // bd@0, sd@4, hh@8. Total = 12.
```

### `rev(pattern)`

Reverses a pattern in time.

```
rev("bd sd hh")            // hh@1.33, sd@2.67, bd@0 (in 4-beat cycle)
```

### `slow(factor, pattern)`

Stretches a pattern by `factor`. `slow(2, "bd*4")` doubles the cycle length from 4 to 8 beats.

```
slow(2, "bd*4 sd")
slow(0.5, "bd*4")   // same as fast(2, ...)
```

### `fast(factor, pattern)`

Compresses a pattern by `factor`. `fast(2, "bd*4")` halves the cycle length from 4 to 2 beats.

```
fast(2, "bd*4 sd")
```

### `degradeBy(probability, pattern)`

Randomly removes events with the given probability. Optional seed for determinism.

```
degradeBy(0.5, "bd*8")          // ~50% of events removed
degradeBy(0.5, 42, "bd*8")     // same as above, deterministic with seed 42
degradeBy(1.0, "bd*8")         // removes all events
degradeBy(0.0, "bd*8")         // removes nothing
```

---

## Method Chains (Parameter Overrides)

Method chains attach static or time-varying parameters to every event in a pattern. Methods are dot-chained after any expression.

| Method | Arguments | Effect | Target Param |
|--------|-----------|--------|-------------|
| `.gain(n)` | `float` | Sets event gain (0–1+) | Gain |
| `.pan(n)` | `float` | Sets stereo pan (-1=left, 0=center, 1=right) | Pan |
| `.pitch(n)` | `float` | Sets pitch multiplier (1=normal) | Pitch |
| `.lpf(min, max, period)` | `float, float, double` | Sine LPF sweep (Hz, Hz, beats) | LpfCutoff |
| `.hpf(min, max, period)` | `float, float, double` | Sine HPF sweep (Hz, Hz, beats) | HpfCutoff |
| `.sine(min, max, period)` | `float, float, double` | Sine wave gain modulation | Gain |
| `.saw(min, max, period)` | `float, float, double` | Sawtooth gain modulation | Gain |
| `.square(min, max, period)` | `float, float, double` | Square wave gain modulation | Gain |
| `.tri(min, max, period)` | `float, float, double` | Triangle wave gain modulation | Gain |
| `.perlin(min, max, period)` | `float, float, double` | Smooth noise gain modulation | Gain |

### Method Chain Examples

```
"bd*4".gain(0.8)
"bd*4".gain(0.8).pan(-0.3)
"hh*8".lpf(800, 12000, 4.0)
stack("bd*4", "sd").gain(0.9).pan(0.1)
slow(2, "bd*4").gain(0.5)
"bd*4".sine(0.2, 0.8, 2.0)       // gain oscillates between 0.2 and 0.8
"bd*4".saw(0.1, 0.9, 1.0)        // ramp up gain each beat
"bd*4".square(0.0, 1.0, 4.0)     // on/off every 2 beats
"bd*4".tri(0.3, 0.7, 2.0)        // triangle-shaped gain
"bd*4".perlin(0.0, 1.0, 8.0)     // random-ish gain wandering
```

---

## Complex Examples

```
// Classic drum beat with layered elements
stack(
    "bd*4",
    "sd".gain(0.9),
    "hh(7,16)".lpf(800, 12000, 4.0)
)

// Slowed-down beat with reduced gain
slow(2, sound("bd*4 sd")).gain(0.7)

// Stochastic hihat pattern
stack(
    "bd*4",
    degradeBy(0.3, "hh*16").gain(0.6)
)

// Euclidean rhythm with filter sweep
sound("hh(3,8)").lpf(200, 8000, 2.0).pan(0.3)

// Slow cat with parallel hihat
sound("<bd sd>, hh*8")

// Concatenated sections
cat("bd*4", "sd", "hh*8").gain(0.8)

// Reversed pattern
rev("bd sd hh")

// Gain tremolo
sound("bd*8").square(0.3, 0.9, 1.0)
```

---

## Strudel.cc Compatibility Matrix

### ✅ Fully Supported

| Strudel Feature | Our Syntax | Notes |
|-----------------|------------|-------|
| Mini-notation sequences | `"a b c"` | Identical |
| Repetition `*` | `"a*4"` | Identical |
| Grouping `[]` | `"[a b] c"` | Identical |
| Euclidean `(n,m)` | `"a(3,8)"` | Identical |
| Rest `~` / `.` | `"a ~ b"` | Identical |
| Slow cat `< >` | `"<a b c>"` | Expands pattern length |
| Parallel `,` | `"a,b"` | Same as `stack()` |
| Slow `/n` | `"a/2"` | Weight-based time allocation |
| Probability `?` | `"a?"` / `"a?0.3"` | Sets `event.probability` |
| `sound()` | `sound("...")` | Entry point |
| `stack()` | `stack("a", "b")` | Uses max child length |
| `cat()` | `cat("a", "b")` | Concatenates end-to-end |
| `rev()` | `rev("a b")` | Reverses in time |
| `slow()` | `slow(2, "a")` | Identical |
| `fast()` | `fast(2, "a")` | Identical |
| `.gain()` | `.gain(0.8)` | Identical |
| `.pan()` | `.pan(0.3)` | Our range is -1..1, strudel is 0..1 |
| `.lpf()` | `.lpf(min, max, period)` | Sine sweep with min/max/period |
| `.hpf()` | `.hpf(min, max, period)` | Same as above |

### ⚠️ Supported with Differences

| Strudel Feature | Our Syntax | Difference |
|-----------------|------------|------------|
| `s("...")` | `sound("...")` | We use `sound()` instead of `s()` |
| `degradeBy()` | `degradeBy(prob, seed?, pattern)` | Strudel uses `.degradeBy()` as method chain; we use it as a function |
| `.pitch()` | `.pitch(n)` | Our pitch is a multiplier (1=normal); strudel uses semitone offsets |
| `.sine/.saw/.square/.tri/.perlin` | Same names | Strudel uses these for audio signals; we attach them as gain modulation curves |

### ❌ Not Supported

| Strudel Feature | Strudel Syntax | Why Not / Workaround |
|-----------------|---------------|---------------------|
| `note()` | `note("c3 e3 g3")` | No pitch/MIDI sequencer — use source labels mapped to pitched samples |
| `n()` | `n("0 2 4")` | Same as above |
| `scale()` | `scale("C:minor")` | No scale system — pre-compose pitched samples |
| `.rev()` | `"a b c".rev()` | Use `rev("a b c")` instead |
| `.sometimes()` | `"a".sometimes(x=>x.fast(2))` | No higher-order transforms |
| `.every()` | `"a".every(4, rev)` | No higher-order transforms |
| `.room()` / `.delay()` | `"a".room(0.5)` | No built-in reverb/delay effects in compiler (use filters + bus chains at engine level) |
| `.speed()` | `"a".speed(2)` | Use `.pitch(2)` for rate multiplication |
| `.cut()` | `"a".cut(1)` | No cut groups |
| `.bank()` | `"a".bank("tr808")` | No sample bank system — register sources individually |
| `.begin()` / `.end()` | `"a".begin(0.25)` | No sample slicing |
| Continuous signals | `sine`, `saw`, `perlin` | Available as `.sine()`, `.saw()`, `.perlin()` gain modulation curves |
| `off()` / `jux()` | `off(1/8, x=>x.add(7))` | No structural transforms |
| `when()` / `if()` | `when("<1 0>", x=>x.rev())` | No conditional transforms |
| `setcps()` / `cpm()` | `setcps(0.5)` | Tempo is set on `PatternTrack` at runtime, not in pattern expressions |

---

## Architecture Notes

**Why these limitations?** The `PatternCompiler` is designed for a **game audio engine**, not a live-coding environment. Key differences from strudel:

1. **No browser/JS runtime** — Patterns compile to a static `std::vector<PatternEvent>` timeline, not a lazy query function. There are no higher-order transforms because the compiled output must be deterministic and lock-free queryable from the audio thread.

2. **No built-in synthesis** — Events reference pre-registered `AudioSource` objects (WAV/OGG/MP3 samples or tone generators). There is no `note()` because pitch is a property of the source, not a synthesizer parameter.

3. **No continuous signals** — Parameter curves are pre-baked `ParameterCurve` objects (sine, saw, square, triangle, perlin) evaluated per-buffer. There are no `sine`/`saw` signal generators in the pattern language — instead they modulate gain/pan/pitch/filter via curves.

4. **Tempo is external** — BPM is set on the `PatternTrack` at runtime. The pattern expression only describes the structure of one cycle.

---

## Adding New Functions

To extend the compiler with a new function (e.g. `echo()`):

1. Add the function name to `evalCall()` in `src/pi/pattern/compiler.cpp`
2. Implement the transform logic on `Pattern` objects
3. Add a universal test in `tests/universal/test_pattern_compiler.cpp`
4. Update this document

To extend mini-notation with new operators (e.g. `?`):

1. Add the token type to `Tokenizer` in `src/pi/pattern/mini_notation.cpp`
2. Update `parseFactor()` or `parseElem()` to handle it
3. Update `compileNode()` to emit the right `PatternEvent` data
4. Add a universal test in `tests/universal/test_mini_notation.cpp`
