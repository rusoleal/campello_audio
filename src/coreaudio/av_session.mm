/// @file av_session.mm
/// @brief iOS AVAudioSession configuration and interruption handling.
///
/// Compiled only when CAMPELLO_AUDIO_IOS is defined (see ios.cmake).
/// Uses Objective-C++ so that NSNotificationCenter and AVAudioSession
/// are accessible from the C++ engine backend.

#include "common.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

// ---------------------------------------------------------------------------
// Module-scope data needed by the interruption observer block.
// One engine instance per process is the expected usage.
// ---------------------------------------------------------------------------
static systems::leal::campello_audio::coreaudio::CoreAudioData* g_interruptionData = nullptr;
static id g_interruptionObserver = nil;

namespace systems::leal::campello_audio::coreaudio {

void setupAVAudioSession() {
    NSError* error = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];

    // Playback category: audio continues when the screen is locked and the
    // silent switch is on (appropriate for games).
    [session setCategory:AVAudioSessionCategoryPlayback error:&error];
    if (error) {
        // Non-fatal: audio may still work without the category set.
        error = nil;
    }

    [session setActive:YES error:&error];
    // Ignore activation errors — device may already be active.
}

void teardownAVAudioSession() {
    // Remove the interruption observer before deactivating the session.
    if (g_interruptionObserver) {
        [[NSNotificationCenter defaultCenter]
            removeObserver:g_interruptionObserver];
        g_interruptionObserver = nil;
    }
    g_interruptionData = nullptr;

    NSError* error = nil;
    [[AVAudioSession sharedInstance] setActive:NO
                                   withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                         error:&error];
}

void registerInterruptionHandler(CoreAudioData* data) {
    g_interruptionData = data;

    g_interruptionObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
        NSDictionary* info = note.userInfo;
        if (!info) return;

        NSUInteger typeVal = [info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        AVAudioSessionInterruptionType type =
            static_cast<AVAudioSessionInterruptionType>(typeVal);

        CoreAudioData* d = g_interruptionData;
        if (!d || !d->audioUnit) return;

        if (type == AVAudioSessionInterruptionTypeBegan) {
            // Phone call / Siri / alarm started — suspend the output unit.
            if (d->running) {
                AudioOutputUnitStop(d->audioUnit);
                d->running = false;
            }
        } else if (type == AVAudioSessionInterruptionTypeEnded) {
            // Interruption over — resume if the OS recommends it.
            NSUInteger optVal = [info[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
            AVAudioSessionInterruptionOptions opts =
                static_cast<AVAudioSessionInterruptionOptions>(optVal);

            if (opts & AVAudioSessionInterruptionOptionShouldResume) {
                NSError* err = nil;
                [[AVAudioSession sharedInstance] setActive:YES error:&err];
                AudioOutputUnitStart(d->audioUnit);
                d->running = true;
            }
        }
    }];
}

} // namespace systems::leal::campello_audio::coreaudio
