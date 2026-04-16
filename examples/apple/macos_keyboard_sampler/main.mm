#import <AppKit/AppKit.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <campello_audio/audio_bus.hpp>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/echo_filter.hpp>
#include <campello_audio/high_pass_filter.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <campello_audio/mp3_source.hpp>
#include <campello_audio/ogg_source.hpp>
#include <campello_audio/wav_source.hpp>

namespace fs = std::filesystem;
namespace audio = systems::leal::campello_audio;

namespace {

struct LoadedClip {
    std::string                   filename;
    std::unique_ptr<audio::AudioSource> source;
};

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::unique_ptr<audio::AudioSource> makeSourceForPath(const fs::path& path) {
    const std::string ext = toLower(path.extension().string());
    if (ext == ".wav") {
        auto source = std::make_unique<audio::WavSource>();
        return source->load(path.string()) ? std::move(source) : nullptr;
    }
    if (ext == ".ogg") {
        auto source = std::make_unique<audio::OggSource>();
        return source->load(path.string()) ? std::move(source) : nullptr;
    }
    if (ext == ".mp3") {
        auto source = std::make_unique<audio::Mp3Source>();
        return source->load(path.string()) ? std::move(source) : nullptr;
    }
    return nullptr;
}

class DemoAudioState {
public:
    bool initialize(const fs::path& audioDir) {
        audio::AudioEngineDescriptor descriptor;
        descriptor.sampleRate = 44100;
        descriptor.bufferSize = 512;
        descriptor.channels   = 2;
        if (!engine_.init(descriptor)) {
            status_ = "AudioEngine::init() failed.";
            return false;
        }

        audioDirectory_ = audioDir;
        engine_.play(sfxBus_);
        engine_.play(musicBus_);
        engine_.setSidechain(&sfxBus_, &musicBus_, -10.0f, 0.03f, 0.35f);
        sidechainEnabled_ = true;
        listener_.position = {0.0f, 0.0f, 0.0f};
        engine_.set3dListenerParameters(listener_);
        return reloadAssets();
    }

    void shutdown() {
        musicHandle_ = {};
        lastHandle_ = {};
        threeDHandle_ = {};
        engine_.deinit();
    }

    bool reloadAssets() {
        if (musicHandle_.isValid()) {
            engine_.stop(musicHandle_);
        }
        sampleClips_.clear();
        musicClip_.reset();
        musicHandle_ = {};
        lastHandle_ = {};
        threeDHandle_ = {};
        musicPaused_ = false;
        threeDActive_ = false;
        threeDPosition_ = {2.0f, 0.0f, -4.0f};

        std::error_code ec;
        if (!fs::exists(audioDirectory_, ec) || !fs::is_directory(audioDirectory_, ec)) {
            status_ = "Audio directory not found: " + audioDirectory_.string();
            return false;
        }

        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(audioDirectory_, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (const auto& path : files) {
            const std::string stem = toLower(path.stem().string());
            auto source = makeSourceForPath(path);
            if (!source) {
                continue;
            }

            if (stem.find("music") != std::string::npos && !musicClip_) {
                source->setLooping(true);
                musicClip_ = std::make_unique<LoadedClip>(LoadedClip{
                    path.filename().string(),
                    std::move(source)
                });
                continue;
            }

            if (sampleClips_.size() < 10) {
                sampleClips_.push_back(LoadedClip{
                    path.filename().string(),
                    std::move(source)
                });
            }
        }

        if (musicClip_) {
            audio::PlayDescriptor musicPlay;
            musicPlay.looping = true;
            musicPlay.protect = true;
            musicPlay.volume  = 0.7f;
            musicHandle_ = musicBus_.play(*musicClip_->source, musicPlay);
        }

        applyBusFilters();
        applyBusVolumes();

        std::ostringstream status;
        status << "Loaded " << sampleClips_.size() << " sample"
               << (sampleClips_.size() == 1 ? "" : "s");
        if (musicClip_) {
            status << " and looping music.";
        } else {
            status << ". No music file found.";
        }
        status_ = status.str();
        return true;
    }

    bool triggerDigit(int digit) {
        if (digit < 0 || digit >= static_cast<int>(sampleClips_.size())) {
            std::ostringstream status;
            status << "No clip mapped to key " << digit << ".";
            status_ = status.str();
            return false;
        }

        audio::PlayDescriptor play;
        play.volume = 0.9f;
        lastHandle_ = sfxBus_.play(*sampleClips_[digit].source, play);

        std::ostringstream status;
        status << "Key " << digit << " -> " << sampleClips_[digit].filename;
        status_ = status.str();
        return true;
    }

    void tick() {
        if (threeDActive_ && engine_.isValid(threeDHandle_)) {
            engine_.set3dSourceParameters(threeDHandle_,
                                          threeDPosition_.x(),
                                          threeDPosition_.y(),
                                          threeDPosition_.z());
        }
        engine_.set3dListenerParameters(listener_);
        engine_.update3d();
        engine_.tick();
    }

    void toggleMusic() {
        if (!musicHandle_.isValid() || !engine_.isValid(musicHandle_)) {
            status_ = "Music handle is not active.";
            return;
        }

        if (musicPaused_) {
            engine_.resume(musicHandle_);
            musicPaused_ = false;
            status_ = "Music resumed.";
        } else {
            engine_.pause(musicHandle_);
            musicPaused_ = true;
            status_ = "Music paused.";
        }
    }

    void toggleMusicLowPass() {
        musicLowPassEnabled_ = !musicLowPassEnabled_;
        applyBusFilters();
        status_ = musicLowPassEnabled_
                ? "Music bus low-pass enabled."
                : "Music bus low-pass disabled.";
    }

    void toggleSfxHighPass() {
        sfxHighPassEnabled_ = !sfxHighPassEnabled_;
        applyBusFilters();
        status_ = sfxHighPassEnabled_
                ? "SFX bus high-pass enabled."
                : "SFX bus high-pass disabled.";
    }

    void toggleSfxEcho() {
        sfxEchoEnabled_ = !sfxEchoEnabled_;
        applyBusFilters();
        status_ = sfxEchoEnabled_
                ? "SFX bus echo enabled."
                : "SFX bus echo disabled.";
    }

    void adjustSfxBusVolume(float delta) {
        sfxBusVolume_ = std::clamp(sfxBusVolume_ + delta, 0.0f, 1.0f);
        applyBusVolumes();
        std::ostringstream status;
        status << "SFX bus volume: " << sfxBusVolume_;
        status_ = status.str();
    }

    void adjustMusicBusVolume(float delta) {
        musicBusVolume_ = std::clamp(musicBusVolume_ + delta, 0.0f, 1.0f);
        applyBusVolumes();
        std::ostringstream status;
        status << "Music bus volume: " << musicBusVolume_;
        status_ = status.str();
    }

    void toggleSidechain() {
        sidechainEnabled_ = !sidechainEnabled_;
        if (sidechainEnabled_) {
            engine_.setSidechain(&sfxBus_, &musicBus_, -10.0f, 0.03f, 0.35f);
            status_ = "Sidechain ducking enabled.";
        } else {
            engine_.clearSidechain(&musicBus_);
            status_ = "Sidechain ducking disabled.";
        }
    }

    void triggerThreeDVoice() {
        if (sampleClips_.empty()) {
            status_ = "No samples loaded for 3D demo.";
            return;
        }

        audio::PlayDescriptor play;
        play.enable3d = true;
        play.position = threeDPosition_;
        play.volume   = 0.9f;
        threeDHandle_ = engine_.play(*sampleClips_.front().source, play);
        lastHandle_   = threeDHandle_;
        threeDActive_ = threeDHandle_.isValid();

        if (!threeDActive_) {
            status_ = "Failed to create 3D voice.";
            return;
        }

        std::ostringstream status;
        status << "3D voice started at x=" << threeDPosition_.x()
               << " z=" << threeDPosition_.z() << ".";
        status_ = status.str();
    }

    void moveThreeD(float dx, float dz) {
        threeDPosition_.x() += dx;
        threeDPosition_.z() += dz;
        if (threeDActive_ && engine_.isValid(threeDHandle_)) {
            engine_.set3dSourceParameters(threeDHandle_,
                                          threeDPosition_.x(),
                                          threeDPosition_.y(),
                                          threeDPosition_.z());
            engine_.update3d();
        }
        std::ostringstream status;
        status << "3D source moved to x=" << threeDPosition_.x()
               << " z=" << threeDPosition_.z() << ".";
        status_ = status.str();
    }

    void resetThreeD() {
        threeDPosition_ = {2.0f, 0.0f, -4.0f};
        if (threeDActive_ && engine_.isValid(threeDHandle_)) {
            engine_.set3dSourceParameters(threeDHandle_,
                                          threeDPosition_.x(),
                                          threeDPosition_.y(),
                                          threeDPosition_.z());
            engine_.update3d();
        }
        status_ = "3D source reset.";
    }

    void adjustLastVolume(float delta) {
        if (!isLastHandleUsable()) return;
        lastVolume_ = std::clamp(lastVolume_ + delta, 0.0f, 1.5f);
        engine_.setVolume(lastHandle_, lastVolume_);
        std::ostringstream status;
        status << "Last voice volume: " << lastVolume_;
        status_ = status.str();
    }

    void adjustLastPan(float delta) {
        if (!isLastHandleUsable()) return;
        lastPan_ = std::clamp(lastPan_ + delta, -1.0f, 1.0f);
        engine_.setPan(lastHandle_, lastPan_);
        std::ostringstream status;
        status << "Last voice pan: " << lastPan_;
        status_ = status.str();
    }

    void adjustLastPitch(float delta) {
        if (!isLastHandleUsable()) return;
        lastPitch_ = std::clamp(lastPitch_ + delta, 0.25f, 2.0f);
        engine_.setPitch(lastHandle_, lastPitch_);
        std::ostringstream status;
        status << "Last voice pitch: " << lastPitch_;
        status_ = status.str();
    }

    void toggleLastPause() {
        if (!isLastHandleUsable()) return;
        if (engine_.isPaused(lastHandle_)) {
            engine_.resume(lastHandle_);
            status_ = "Last voice resumed.";
        } else {
            engine_.pause(lastHandle_);
            status_ = "Last voice paused.";
        }
    }

    void stopLastVoice() {
        if (!isLastHandleUsable()) return;
        engine_.stop(lastHandle_);
        status_ = "Last voice stopped.";
    }

    std::string buildOverlayText() const {
        std::ostringstream text;
        text << "Campello Audio macOS Sampler\n\n";
        text << "Auto-loaded from:\n" << audioDirectory_.string() << "\n\n";
        text << "Controls\n";
        text << "0-9 : trigger samples\n";
        text << "M   : pause/resume music\n";
        text << "R   : reload audio folder\n\n";
        text << "U/J : SFX bus volume +/-\n";
        text << "I/K : music bus volume +/-\n";
        text << "L   : toggle music low-pass\n";
        text << "H   : toggle SFX high-pass\n";
        text << "E   : toggle SFX echo\n";
        text << "D   : toggle sidechain ducking\n";
        text << "T   : start 3D demo voice\n";
        text << "G   : reset 3D voice position\n";
        text << "Arrows: move 3D voice\n";
        text << "[/] : last voice volume -/+\n";
        text << ",/. : last voice pan left/right\n";
        text << "-/= : last voice pitch -/+\n";
        text << "Space: pause/resume last voice\n";
        text << "Delete: stop last voice\n\n";
        text << "Mappings\n";
        for (int digit = 0; digit < 10; ++digit) {
            text << digit << " -> ";
            if (digit < static_cast<int>(sampleClips_.size())) {
                text << sampleClips_[digit].filename;
            } else {
                text << "(unassigned)";
            }
            text << "\n";
        }
        text << "\nMusic\n";
        if (musicClip_) {
            text << musicClip_->filename << " [" << (musicPaused_ ? "paused" : "playing") << "]";
        } else {
            text << "(missing)";
        }
        text << "\n\nBuses\n";
        text << "SFX volume: " << sfxBusVolume_
             << " | HPF: " << (sfxHighPassEnabled_ ? "on" : "off")
             << " | Echo: " << (sfxEchoEnabled_ ? "on" : "off") << "\n";
        text << "Music volume: " << musicBusVolume_
             << " | LPF: " << (musicLowPassEnabled_ ? "on" : "off")
             << " | Sidechain: " << (sidechainEnabled_ ? "on" : "off") << "\n\n";
        text << "Last voice\n";
        text << "Valid: " << (engine_.isValid(lastHandle_) ? "yes" : "no")
             << " | paused: " << (engine_.isPaused(lastHandle_) ? "yes" : "no") << "\n";
        text << "Volume: " << lastVolume_
             << " | pan: " << lastPan_
             << " | pitch: " << lastPitch_ << "\n\n";
        text << "3D demo\n";
        text << "Active: " << (engine_.isValid(threeDHandle_) ? "yes" : "no")
             << " | x: " << threeDPosition_.x()
             << " | z: " << threeDPosition_.z() << "\n\n";
        text << "Engine\n";
        text << "Sample rate: " << engine_.getSampleRate() << " Hz\n";
        text << "Active voices: " << engine_.getActiveVoiceCount() << "\n";
        text << "Virtual voices: " << engine_.getVirtualVoiceCount() << "\n\n";
        text << "Status\n" << status_ << "\n";
        return text.str();
    }

private:
    bool isLastHandleUsable() {
        if (!lastHandle_.isValid() || !engine_.isValid(lastHandle_)) {
            status_ = "No active last voice.";
            return false;
        }
        return true;
    }

    void applyBusFilters() {
        if (musicLowPassEnabled_) {
            if (!musicLowPass_) {
                musicLowPass_ = std::make_shared<audio::LowPassFilter>(900.0f, 0.85f);
            }
            musicBus_.addFilter(musicLowPass_, 0);
        } else {
            musicBus_.removeFilter(0);
        }

        if (sfxHighPassEnabled_) {
            if (!sfxHighPass_) {
                sfxHighPass_ = std::make_shared<audio::HighPassFilter>(900.0f, 0.8f);
            }
            sfxBus_.addFilter(sfxHighPass_, 0);
        } else {
            sfxBus_.removeFilter(0);
        }

        if (sfxEchoEnabled_) {
            if (!sfxEcho_) {
                sfxEcho_ = std::make_shared<audio::EchoFilter>(0.18f, 0.25f, 0.2f);
            }
            sfxBus_.addFilter(sfxEcho_, 1);
        } else {
            sfxBus_.removeFilter(1);
        }
    }

    void applyBusVolumes() {
        sfxBus_.setVolume(sfxBusVolume_);
        musicBus_.setVolume(musicBusVolume_);
    }

    audio::AudioEngine               engine_;
    audio::AudioBus                  sfxBus_;
    audio::AudioBus                  musicBus_;
    audio::ListenerDescriptor        listener_;
    fs::path                         audioDirectory_;
    std::vector<LoadedClip>          sampleClips_;
    std::unique_ptr<LoadedClip>      musicClip_;
    audio::SoundHandle               musicHandle_;
    audio::SoundHandle               lastHandle_;
    audio::SoundHandle               threeDHandle_;
    std::shared_ptr<audio::LowPassFilter>  musicLowPass_;
    std::shared_ptr<audio::HighPassFilter> sfxHighPass_;
    std::shared_ptr<audio::EchoFilter>     sfxEcho_;
    bool                             musicPaused_ = false;
    bool                             musicLowPassEnabled_ = false;
    bool                             sfxHighPassEnabled_ = false;
    bool                             sfxEchoEnabled_ = false;
    bool                             sidechainEnabled_ = false;
    bool                             threeDActive_ = false;
    float                            sfxBusVolume_ = 1.0f;
    float                            musicBusVolume_ = 1.0f;
    float                            lastVolume_ = 0.9f;
    float                            lastPan_ = 0.0f;
    float                            lastPitch_ = 1.0f;
    audio::Vec3                      threeDPosition_ = {2.0f, 0.0f, -4.0f};
    std::string                      status_;
};

fs::path bundledAudioDirectory() {
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* resources = [bundle resourcePath];
    if (!resources) {
        return {};
    }
    return fs::path([[resources stringByAppendingPathComponent:@"audio"] UTF8String]);
}

int digitFromEvent(NSEvent* event) {
    NSString* chars = [event charactersIgnoringModifiers];
    if (!chars || chars.length == 0) {
        return -1;
    }
    const unichar ch = [chars characterAtIndex:0];
    if (ch >= '0' && ch <= '9') {
        return static_cast<int>(ch - '0');
    }
    return -1;
}

} // namespace

@interface DemoView : NSView
- (instancetype)initWithFrame:(NSRect)frame audioState:(DemoAudioState*)audioState;
@end

@interface DemoView ()
@property(nonatomic, assign) DemoAudioState* audioState;
@property(nonatomic, strong) NSTextView* statusTextView;
@property(nonatomic, strong) NSTimer* refreshTimer;
@end

@implementation DemoView

- (instancetype)initWithFrame:(NSRect)frame audioState:(DemoAudioState*)audioState {
    self = [super initWithFrame:frame];
    if (!self) {
        return nil;
    }

    _audioState = audioState;
    self.wantsLayer = YES;
    self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:self.bounds];
    scrollView.hasVerticalScroller = YES;
    scrollView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSTextView* textView = [[NSTextView alloc] initWithFrame:self.bounds];
    textView.editable = NO;
    textView.selectable = NO;
    textView.drawsBackground = NO;
    textView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    textView.font = [NSFont monospacedSystemFontOfSize:15.0 weight:NSFontWeightRegular];
    scrollView.documentView = textView;

    self.statusTextView = textView;
    [self addSubview:scrollView];
    [self refresh];

    __weak DemoView* weakSelf = self;
    self.refreshTimer = [NSTimer scheduledTimerWithTimeInterval:0.2 repeats:YES block:^(__unused NSTimer* timer) {
        [weakSelf refresh];
    }];

    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self.window makeFirstResponder:self];
}

- (void)keyDown:(NSEvent*)event {
    if (event.keyCode == 123) {
        self.audioState->moveThreeD(-0.5f, 0.0f);
        [self refresh];
        return;
    }
    if (event.keyCode == 124) {
        self.audioState->moveThreeD(0.5f, 0.0f);
        [self refresh];
        return;
    }
    if (event.keyCode == 125) {
        self.audioState->moveThreeD(0.0f, 0.5f);
        [self refresh];
        return;
    }
    if (event.keyCode == 126) {
        self.audioState->moveThreeD(0.0f, -0.5f);
        [self refresh];
        return;
    }
    if (event.keyCode == 49) {
        self.audioState->toggleLastPause();
        [self refresh];
        return;
    }
    if (event.keyCode == 51 || event.keyCode == 117) {
        self.audioState->stopLastVoice();
        [self refresh];
        return;
    }

    const int digit = digitFromEvent(event);
    if (digit >= 0) {
        self.audioState->triggerDigit(digit);
        [self refresh];
        return;
    }

    NSString* chars = [event charactersIgnoringModifiers];
    if (chars.length == 0) {
        [super keyDown:event];
        return;
    }

    const unichar ch = [[chars lowercaseString] characterAtIndex:0];
    if (ch == 'm') {
        self.audioState->toggleMusic();
        [self refresh];
        return;
    }

    if (ch == 'r') {
        self.audioState->reloadAssets();
        [self refresh];
        return;
    }
    if (ch == 'u') {
        self.audioState->adjustSfxBusVolume(0.1f);
        [self refresh];
        return;
    }
    if (ch == 'j') {
        self.audioState->adjustSfxBusVolume(-0.1f);
        [self refresh];
        return;
    }
    if (ch == 'i') {
        self.audioState->adjustMusicBusVolume(0.1f);
        [self refresh];
        return;
    }
    if (ch == 'k') {
        self.audioState->adjustMusicBusVolume(-0.1f);
        [self refresh];
        return;
    }
    if (ch == 'l') {
        self.audioState->toggleMusicLowPass();
        [self refresh];
        return;
    }
    if (ch == 'h') {
        self.audioState->toggleSfxHighPass();
        [self refresh];
        return;
    }
    if (ch == 'e') {
        self.audioState->toggleSfxEcho();
        [self refresh];
        return;
    }
    if (ch == 'd') {
        self.audioState->toggleSidechain();
        [self refresh];
        return;
    }
    if (ch == 't') {
        self.audioState->triggerThreeDVoice();
        [self refresh];
        return;
    }
    if (ch == 'g') {
        self.audioState->resetThreeD();
        [self refresh];
        return;
    }
    if (ch == '[') {
        self.audioState->adjustLastVolume(-0.1f);
        [self refresh];
        return;
    }
    if (ch == ']') {
        self.audioState->adjustLastVolume(0.1f);
        [self refresh];
        return;
    }
    if (ch == ',') {
        self.audioState->adjustLastPan(-0.15f);
        [self refresh];
        return;
    }
    if (ch == '.') {
        self.audioState->adjustLastPan(0.15f);
        [self refresh];
        return;
    }
    if (ch == '-') {
        self.audioState->adjustLastPitch(-0.1f);
        [self refresh];
        return;
    }
    if (ch == '=') {
        self.audioState->adjustLastPitch(0.1f);
        [self refresh];
        return;
    }

    [super keyDown:event];
}

- (void)refresh {
    if (!self.audioState) {
        return;
    }
    self.audioState->tick();
    const std::string text = self.audioState->buildOverlayText();
    self.statusTextView.string = [NSString stringWithUTF8String:text.c_str()];
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* _window;
    DemoAudioState _audioState;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    const fs::path audioDir = bundledAudioDirectory();
    _audioState.initialize(audioDir);

    NSRect frame = NSMakeRect(0, 0, 760, 560);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable |
                                                     NSWindowStyleMaskResizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Campello Audio Sampler";
    _window.contentView = [[DemoView alloc] initWithFrame:frame audioState:&_audioState];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    _audioState.shutdown();
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
