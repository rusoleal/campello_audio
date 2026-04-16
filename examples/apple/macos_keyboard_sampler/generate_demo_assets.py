#!/usr/bin/env python3

import math
import random
import struct
import sys
import wave
from pathlib import Path


SAMPLE_RATE = 44100
CHANNELS = 2
MASTER_GAIN = 0.35
RNG = random.Random(1337)


def clamp_sample(value: float) -> int:
    value = max(-1.0, min(1.0, value))
    return int(value * 32767.0)


def envelope(index: int, total: int, attack: float, release: float) -> float:
    if total <= 1:
        return 1.0
    pos = index / float(total - 1)
    if pos < attack:
        return pos / max(attack, 1e-6)
    if pos > 1.0 - release:
        return (1.0 - pos) / max(release, 1e-6)
    return 1.0


def render_wave(path: Path, duration: float, synth_fn) -> None:
    frame_count = max(1, int(duration * SAMPLE_RATE))
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(CHANNELS)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for frame in range(frame_count):
            t = frame / float(SAMPLE_RATE)
            left, right = synth_fn(t, frame, frame_count)
            frames += struct.pack("<hh", clamp_sample(left), clamp_sample(right))
        wav_file.writeframes(frames)


def sine(freq: float, t: float) -> float:
    return math.sin(2.0 * math.pi * freq * t)


def square(freq: float, t: float) -> float:
    return 1.0 if sine(freq, t) >= 0.0 else -1.0


def saw(freq: float, t: float) -> float:
    phase = (t * freq) % 1.0
    return (phase * 2.0) - 1.0


def noise() -> float:
    return RNG.uniform(-1.0, 1.0)


def percussive_tone(base_freq: float, pan: float, waveform: str):
    def synth(t: float, frame: int, total: int):
        env = envelope(frame, total, 0.02, 0.35)
        pitch = base_freq * (1.0 + 0.18 * (1.0 - env))
        if waveform == "sine":
            sample = sine(pitch, t)
        elif waveform == "square":
            sample = square(pitch, t)
        else:
            sample = saw(pitch, t)
        sample *= env * MASTER_GAIN
        left = sample * (1.0 - max(0.0, pan))
        right = sample * (1.0 + min(0.0, pan))
        return left, right

    return synth


def noisy_hit(base_freq: float, pan: float):
    def synth(t: float, frame: int, total: int):
        env = envelope(frame, total, 0.005, 0.45)
        tone = sine(base_freq, t) * 0.55 + noise() * 0.45
        tone *= env * MASTER_GAIN
        left = tone * (1.0 - max(0.0, pan))
        right = tone * (1.0 + min(0.0, pan))
        return left, right

    return synth


def music_loop():
    chords = [
        (261.63, 329.63, 392.00),
        (293.66, 369.99, 440.00),
        (329.63, 415.30, 493.88),
        (220.00, 329.63, 440.00),
    ]
    bpm = 96.0
    beat_len = 60.0 / bpm

    def synth(t: float, frame: int, total: int):
        beat = int(t / beat_len)
        chord = chords[(beat // 4) % len(chords)]
        local = t % beat_len
        bass_env = max(0.0, 1.0 - (local / beat_len) * 1.8)
        bass = sine(chord[0] * 0.5, t) * bass_env * 0.45

        arp_index = beat % 3
        arp_freq = chord[arp_index]
        arp_on = beat_len * 0.65
        arp_fade = 0.015
        if local < max(0.0, arp_on - arp_fade):
            arp_gate = 1.0
        elif local < arp_on:
            arp_gate = max(0.0, (arp_on - local) / arp_fade)
        else:
            arp_gate = 0.0
        arp = sine(arp_freq, t) * arp_gate * 0.25

        pad = sum(sine(freq, t) for freq in chord) / 3.0
        pad *= 0.18

        sample = (bass + arp + pad) * MASTER_GAIN

        shimmer = sine(arp_freq * 2.0, t) * 0.05 * arp_gate
        left = sample + shimmer
        right = sample - shimmer
        return left, right

    return synth


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: generate_demo_assets.py <output-dir>", file=sys.stderr)
        return 1

    output_dir = Path(sys.argv[1])
    output_dir.mkdir(parents=True, exist_ok=True)

    renders = [
        ("sample_0_kick.wav", 0.28, percussive_tone(110.0, 0.0, "sine")),
        ("sample_1_snare.wav", 0.24, noisy_hit(220.0, -0.15)),
        ("sample_2_hat.wav", 0.16, noisy_hit(420.0, 0.2)),
        ("sample_3_blip.wav", 0.22, percussive_tone(523.25, -0.5, "square")),
        ("sample_4_chime.wav", 0.60, percussive_tone(659.25, 0.5, "sine")),
        ("sample_5_bass.wav", 0.35, percussive_tone(82.41, -0.2, "saw")),
        ("sample_6_clap.wav", 0.18, noisy_hit(310.0, 0.1)),
        ("sample_7_tom.wav", 0.30, percussive_tone(146.83, 0.25, "sine")),
        ("sample_8_arcade.wav", 0.40, percussive_tone(784.00, -0.4, "square")),
        ("sample_9_rise.wav", 0.55, percussive_tone(392.00, 0.35, "saw")),
        ("music_loop.wav", 8.0, music_loop()),
    ]

    for name, duration, synth in renders:
        render_wave(output_dir / name, duration, synth)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
