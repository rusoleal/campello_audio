//
//  CampelloAudioEngine.h
//  SwiftUI Demo for campello_audio
//
//  Objective-C public interface. Swift imports this header via a bridging header.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface CampelloAudioEngine : NSObject

/// Initialize the audio engine. Returns NO on failure.
- (BOOL)initialize;

/// Shut down the audio engine and release all resources.
- (void)shutdown;

/// Play a short sine tone (440 Hz, 0.5 sec).
- (void)playTone;

/// Play a WAV file at the given path.
- (void)playWav:(NSString *)path;

/// Stop all playing voices.
- (void)stopAll;

/// Set master volume (0.0 … 1.0).
- (void)setMasterVolume:(float)volume;

/// Register an RTPC parameter.
- (void)registerParameter:(NSString *)name min:(float)minValue max:(float)maxValue;

/// Set an RTPC parameter value.
- (void)setParameter:(NSString *)name value:(float)value;

/// Start a simple PatternTrack demo (4-beat loop with tones).
- (void)playPatternTrack;

/// Compile a mini-notation expression and start playing it.
/// Returns nil on success, or an error message string on failure.
- (nullable NSString *)compileAndPlayPattern:(NSString *)expression;

/// Stop the currently playing user pattern.
- (void)stopUserPattern;

/// Number of voices currently being mixed.
- (NSUInteger)activeVoiceCount;

/// YES after successful initialize and before shutdown.
@property (readonly, nonatomic) BOOL isRunning;

@end

NS_ASSUME_NONNULL_END
