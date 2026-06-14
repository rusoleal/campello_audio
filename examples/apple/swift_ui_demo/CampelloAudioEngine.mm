//
//  CampelloAudioEngine.mm
//  SwiftUI Demo for campello_audio
//
//  Objective-C++ wrapper around the C++ engine API.
//

#import "CampelloAudioEngine.h"

#include <campello_audio/audio_engine.hpp>
#include <campello_audio/audio_parameter.hpp>
#include <campello_audio/tone_source.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/pattern_track.hpp>
#include <pattern/pattern.hpp>
#include <pattern/pattern_bank.hpp>
#include <pattern/pattern_event.hpp>
#include <pattern/compiler.hpp>
#include <memory>
#include <vector>
#include <cmath>
#include <random>

using namespace systems::leal::campello_audio;
using systems::leal::campello_audio::pi::Pattern;
using systems::leal::campello_audio::pi::PatternEvent;
using systems::leal::campello_audio::pi::PatternBank;
using systems::leal::campello_audio::pi::ParameterCurve;
using systems::leal::campello_audio::pi::PatternParam;
using systems::leal::campello_audio::pi::PatternCompiler;

namespace {

// ---------------------------------------------------------------------------
// Minimal in-memory WAV synthesis
// ---------------------------------------------------------------------------

static constexpr uint32_t kSampleRate = 44100;
static constexpr uint16_t kChannels   = 2;

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static std::vector<uint8_t> makeWavHeader(uint32_t dataBytes) {
    std::vector<uint8_t> header(44, 0);
    auto w32 = [&](uint8_t* dst, uint32_t v) {
        dst[0] = v & 0xFF; dst[1] = (v >> 8) & 0xFF;
        dst[2] = (v >> 16) & 0xFF; dst[3] = (v >> 24) & 0xFF;
    };
    auto w16 = [&](uint8_t* dst, uint16_t v) {
        dst[0] = v & 0xFF; dst[1] = (v >> 8) & 0xFF;
    };
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    w32(header.data() + 4, 36 + dataBytes);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    w32(header.data() + 16, 16);
    w16(header.data() + 20, 1);                 // PCM
    w16(header.data() + 22, kChannels);         // stereo
    w32(header.data() + 24, kSampleRate);
    w32(header.data() + 28, kSampleRate * kChannels * 2);
    w16(header.data() + 32, kChannels * 2);     // block align
    w16(header.data() + 34, 16);                // bits per sample
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    w32(header.data() + 40, dataBytes);
    return header;
}

static std::vector<uint8_t> buildWav(const std::vector<float>& interleavedSamples) {
    const uint32_t dataBytes = static_cast<uint32_t>(interleavedSamples.size() * sizeof(int16_t));
    auto wav = makeWavHeader(dataBytes);
    wav.reserve(wav.size() + dataBytes);
    for (float s : interleavedSamples) {
        int16_t v = static_cast<int16_t>(clampf(s, -1.0f, 1.0f) * 32767.0f);
        wav.push_back(v & 0xFF);
        wav.push_back((v >> 8) & 0xFF);
    }
    return wav;
}

static std::vector<float> generateKick() {
    const float duration = 0.25f;
    const uint32_t frames = static_cast<uint32_t>(duration * kSampleRate);
    std::vector<float> out;
    out.reserve(frames * kChannels);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = i / static_cast<float>(kSampleRate);
        float env = std::exp(-t / 0.08f);
        float freq = 150.0f * std::exp(-t / 0.06f) + 50.0f;
        float sample = std::sin(2.0f * 3.14159265f * freq * t) * env * 0.9f;
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

static std::vector<float> generateSnare() {
    const float duration = 0.18f;
    const uint32_t frames = static_cast<uint32_t>(duration * kSampleRate);
    std::vector<float> out;
    out.reserve(frames * kChannels);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = i / static_cast<float>(kSampleRate);
        float env = std::exp(-t / 0.05f);
        float tone = std::sin(2.0f * 3.14159265f * 200.0f * t) * 0.5f;
        float noise = dist(rng) * 0.5f;
        float sample = (tone + noise) * env * 0.8f;
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

static std::vector<float> generateHihat() {
    const float duration = 0.08f;
    const uint32_t frames = static_cast<uint32_t>(duration * kSampleRate);
    std::vector<float> out;
    out.reserve(frames * kChannels);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = i / static_cast<float>(kSampleRate);
        float env = std::exp(-t / 0.015f);
        float sample = dist(rng) * env * 0.5f;
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

static std::vector<float> generateClap() {
    const float duration = 0.15f;
    const uint32_t frames = static_cast<uint32_t>(duration * kSampleRate);
    std::vector<float> out;
    out.reserve(frames * kChannels);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = i / static_cast<float>(kSampleRate);
        float env = std::exp(-t / 0.04f);
        float sample = dist(rng) * env * 0.6f;
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

static std::vector<float> generatePianoTone(float freq) {
    const float duration = 0.6f;
    const uint32_t frames = static_cast<uint32_t>(duration * kSampleRate);
    std::vector<float> out;
    out.reserve(frames * kChannels);
    for (uint32_t i = 0; i < frames; ++i) {
        float t = i / static_cast<float>(kSampleRate);
        float attack = std::min(t / 0.005f, 1.0f);
        float decay = std::exp(-t / 0.4f);
        float env = attack * decay;
        float sample = std::sin(2.0f * 3.14159265f * freq * t) * env * 0.6f;
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

} // anonymous namespace

@interface CampelloAudioEngine () {
    std::unique_ptr<AudioEngine> _engine;
    std::shared_ptr<ToneSource>  _toneSource;
    std::shared_ptr<PatternBank> _patternBank;
    std::unordered_map<std::string, std::shared_ptr<WavSource>> _soundBank;
    bool _soundBankReady;
}
@end

@implementation CampelloAudioEngine

- (BOOL)initialize {
    if (_engine) return YES;

    _engine = std::make_unique<AudioEngine>();
    AudioEngineDescriptor desc;
    desc.sampleRate = kSampleRate;
    desc.bufferSize = 512;
    desc.channels   = 2;

    if (!_engine->init(desc)) {
        _engine.reset();
        return NO;
    }

    _toneSource = std::make_shared<ToneSource>(WaveForm::Sine, 440.0f);
    _soundBankReady = false;
    return YES;
}

- (void)shutdown {
    if (!_engine) return;
    _engine->deinit();
    _engine.reset();
    _toneSource.reset();
    _patternBank.reset();
    _soundBank.clear();
}

- (BOOL)isRunning {
    return _engine != nullptr;
}

- (void)ensureSoundBank {
    if (_soundBankReady) return;
    _soundBankReady = true;

    auto add = [&](const std::string& label, const std::vector<float>& samples) {
        auto wav = buildWav(samples);
        auto source = std::make_shared<WavSource>();
        if (source->loadMem(wav.data(), static_cast<uint32_t>(wav.size()))) {
            _soundBank[label] = source;
        }
    };

    add("bd", generateKick());
    add("sd", generateSnare());
    add("hh", generateHihat());
    add("cp", generateClap());
    add("c4", generatePianoTone(261.63f));
    add("d4", generatePianoTone(293.66f));
    add("e4", generatePianoTone(329.63f));
    add("f4", generatePianoTone(349.23f));
    add("g4", generatePianoTone(392.00f));
    add("a4", generatePianoTone(440.00f));
}

- (void)playTone {
    if (!_engine) return;
    _engine->play(*_toneSource);
}

- (void)playWav:(NSString *)path {
    if (!_engine) return;
    auto source = std::make_shared<WavSource>();
    if (!source->load([path UTF8String])) return;
    _engine->play(*source);
}

- (void)stopAll {
    if (!_engine) return;
    _engine->stopAll();
}

- (void)setMasterVolume:(float)volume {
    if (!_engine) return;
    _engine->setGlobalVolume(volume);
}

- (void)registerParameter:(NSString *)name min:(float)minValue max:(float)maxValue {
    if (!_engine) return;
    auto param = std::make_shared<AudioParameter>([name UTF8String], minValue, maxValue);
    _engine->registerParameter(param);
}

- (void)setParameter:(NSString *)name value:(float)value {
    if (!_engine) return;
    _engine->setParameter([name UTF8String], value);
}

- (void)playPatternTrack {
    if (!_engine) return;
    [self ensureSoundBank];

    auto pattern = std::make_shared<Pattern>();
    pattern->lengthInBeats = 4.0;

    float frequencies[4] = { 440.0f, 554.37f, 659.25f, 440.0f };
    for (int i = 0; i < 4; ++i) {
        PatternEvent ev;
        ev.beat     = static_cast<double>(i);
        ev.duration = 0.8;
        ev.sourceLabel = "tone";
        ev.gain  = 0.5f;
        ev.pitch = 1.0f;
        ev.pan   = 0.0f;

        if (i == 0) {
            ParameterCurve curve;
            curve.targetParam = PatternParam::Gain;
            curve.type        = CurveType::Linear;
            curve.periodBeats = 1.0;
            curve.minValue    = 0.0f;
            curve.maxValue    = 1.0f;
            curve.rtpcName    = "intensity";
            ev.paramCurves.push_back(curve);
        }

        pattern->events.push_back(ev);
    }

    _patternBank = std::make_shared<PatternBank>();
    _patternBank->registerSource("tone", _toneSource);
    _patternBank->addPattern("demo", pattern);

    PatternTrack track;
    track.setPatternBank(_patternBank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("demo", "main");

    _engine->play(track);
}

- (NSString *)compileAndPlayPattern:(NSString *)expression {
    if (!_engine) return @"Engine not initialized";
    if (!expression || expression.length == 0) return @"Empty expression";

    [self ensureSoundBank];

    PatternCompiler compiler;
    for (const auto& [label, source] : _soundBank) {
        compiler.registerSource(label, source);
    }

    auto pat = compiler.compile([expression UTF8String], 4.0);
    if (!pat) {
        return [NSString stringWithUTF8String:compiler.getLastError().c_str()];
    }

    _patternBank = std::make_shared<PatternBank>();
    for (const auto& [label, source] : _soundBank) {
        _patternBank->registerSource(label, source);
    }
    _patternBank->addPattern("user", std::move(pat));

    PatternTrack track;
    track.setPatternBank(_patternBank);
    track.setBpm(120.0f);
    track.setTimeSignature(4, 4);
    track.addSection("user", "main");

    _engine->play(track);
    return nil;
}

- (void)stopUserPattern {
    if (!_engine) return;
    _engine->stopAll();
}

- (NSUInteger)activeVoiceCount {
    if (!_engine) return 0;
    return static_cast<NSUInteger>(_engine->getActiveVoiceCount());
}

@end
