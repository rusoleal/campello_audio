#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <campello_audio/descriptors/audio_engine_descriptor.hpp>
#include <campello_audio/descriptors/listener_descriptor.hpp>
#include <campello_audio/descriptors/play_descriptor.hpp>
#include <campello_audio/constants/attenuation_model.hpp>
#include <campello_audio/types/sound_handle.hpp>
#include <campello_audio/types/loader.hpp>

namespace systems::leal::campello_audio {

class AudioSource;
class AudioBus;
class AudioParameter;
class AudioSnapshot;

/// @brief Central audio engine.
///
/// Manages the audio hardware backend, mixer, voice pool, 3D engine,
/// RTPC parameter system, mix snapshots, and adaptive music.
/// Typically one engine instance lives for the lifetime of the application.
///
/// ### Minimal usage
/// @code
/// AudioEngine engine;
/// engine.init({});
///
/// WavSource shot;
/// shot.load("gunshot.wav");
/// engine.play(shot);          // fire and forget
///
/// engine.deinit();
/// @endcode
///
/// ### Explicit voice control
/// @code
/// WavSource music;
/// music.load("theme.wav");
///
/// PlayDescriptor pd;
/// pd.looping = true;
/// pd.volume  = 0.8f;
///
/// SoundHandle h = engine.play(music, pd);
/// engine.fadeVolume(h, 0.0f, 2.0);   // fade out over 2 seconds
/// engine.stop(h);
/// @endcode
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// @brief Initialise the audio engine and open the hardware device.
    /// @return true on success.
    bool init(const AudioEngineDescriptor& descriptor = {});

    /// @brief Flush all voices and release the hardware device.
    void deinit();

    /// @brief Process deferred callbacks (async load completions, parameter
    ///        updates, etc.). Call once per game frame on the main thread.
    void tick();

    /// @brief Begin loading @p source asynchronously.
    ///
    /// @p loader is called **immediately** on the calling thread to obtain the
    /// underlying future; the actual I/O work runs wherever the caller arranges
    /// (std::async, custom thread pool, coroutine, etc.).
    ///
    /// When the future resolves, the next call to tick() decodes the bytes into
    /// @p source and fires @p onDone on the calling thread. Calling engine.play()
    /// from inside @p onDone is safe.
    ///
    /// @p source must remain alive until @p onDone fires (or until deinit()).
    ///
    /// @code
    /// engine.loadAsync(src,
    ///     []() -> std::future<std::vector<uint8_t>> {
    ///         return std::async(std::launch::async,
    ///             []{ return readFile("shot.wav"); });
    ///     },
    ///     [&](bool ok) { if (ok) engine.play(src); }
    /// );
    /// @endcode
    void loadAsync(AudioSource& source,
                   AsyncByteLoader loader,
                   LoadCallback onDone = {});

    // -----------------------------------------------------------------------
    // Playback
    // -----------------------------------------------------------------------

    /// @brief Play @p source with default parameters (fire and forget).
    SoundHandle play(AudioSource& source);

    /// @brief Play @p source with explicit parameters.
    SoundHandle play(AudioSource& source, const PlayDescriptor& descriptor);

    /// @brief Shortcut to start a 3D-positioned voice at (@p x, @p y, @p z).
    SoundHandle play3d(AudioSource& source, float x, float y, float z);

    /// @brief Play @p source unattenuated at full volume (suitable for music/UI).
    SoundHandle playBackground(AudioSource& source);

    // -----------------------------------------------------------------------
    // Voice control
    // -----------------------------------------------------------------------

    void stop(SoundHandle handle);
    void stopAll();
    void pause(SoundHandle handle);
    void resume(SoundHandle handle);
    bool isPaused(SoundHandle handle) const;
    bool isValid(SoundHandle handle)  const;

    // -----------------------------------------------------------------------
    // Per-voice parameters
    // -----------------------------------------------------------------------

    void setVolume(SoundHandle handle, float volume);
    void setPan(SoundHandle handle, float pan);
    void setPitch(SoundHandle handle, float pitch);
    void setLooping(SoundHandle handle, bool loop);
    void setProtect(SoundHandle handle, bool protect);

    // -----------------------------------------------------------------------
    // Seek / position
    // -----------------------------------------------------------------------

    void   seek(SoundHandle handle, double seconds);
    double getPosition(SoundHandle handle) const;

    // -----------------------------------------------------------------------
    // Fades
    // -----------------------------------------------------------------------

    void fadeVolume(SoundHandle handle, float to, double timeSecs);
    void fadePan(SoundHandle handle, float to, double timeSecs);
    void fadePitch(SoundHandle handle, float to, double timeSecs);
    void fadeGlobalVolume(float to, double timeSecs);

    // -----------------------------------------------------------------------
    // Oscillation (LFO)
    // -----------------------------------------------------------------------

    void oscillateVolume(SoundHandle handle, float from, float to, double timeSecs);
    void oscillatePan(SoundHandle handle, float from, float to, double timeSecs);

    // -----------------------------------------------------------------------
    // Global parameters
    // -----------------------------------------------------------------------

    float    getGlobalVolume()     const;
    void     setGlobalVolume(float volume);
    uint32_t getActiveVoiceCount() const;
    uint32_t getVirtualVoiceCount() const;
    uint32_t getTotalVoiceCount()  const;

    // -----------------------------------------------------------------------
    // 3D audio
    // -----------------------------------------------------------------------

    /// @brief Update listener state for the primary listener (index 0).
    void set3dListenerParameters(const ListenerDescriptor& listener);

    /// @brief Update listener state for a specific listener (split-screen).
    /// @param listenerIndex  Must be < AudioEngineDescriptor::listenerCount.
    void set3dListenerParameters(uint32_t listenerIndex, const ListenerDescriptor& listener);

    void set3dSourceParameters(SoundHandle handle, float x, float y, float z);
    void set3dSourceVelocity(SoundHandle handle, float x, float y, float z);
    void set3dSourceAttenuation(SoundHandle handle, AttenuationModel model, float rolloffFactor);
    void set3dSourceDopplerFactor(SoundHandle handle, float factor);
    void set3dSourceMinMaxDistance(SoundHandle handle, float minDist, float maxDist);
    void set3dSoundSpeed(float speedOfSound);

    /// @brief Recalculate 3D panning / attenuation for all active voices.
    /// Call once per game frame before rendering audio.
    void update3d();

    // -----------------------------------------------------------------------
    // Sidechain / ducking
    // -----------------------------------------------------------------------

    /// @brief Configure automatic ducking: when @p trigger plays, lower @p target.
    ///
    /// @param trigger       Bus whose output level drives the sidechain.
    /// @param target        Bus whose volume is automatically ducked.
    /// @param duckDb        Amount to reduce @p target volume (negative dB, e.g. -12).
    /// @param attackSecs    Time to reach full ducking once trigger is active.
    /// @param releaseSecs   Time to restore target volume after trigger quiets.
    void setSidechain(AudioBus* trigger, AudioBus* target,
                      float duckDb, float attackSecs, float releaseSecs);

    /// @brief Remove the sidechain from @p target.
    void clearSidechain(AudioBus* target);

    // -----------------------------------------------------------------------
    // RTPC — Real-Time Parameter Control
    // -----------------------------------------------------------------------

    /// @brief Register a named parameter so the engine can route it to sources.
    void registerParameter(std::shared_ptr<AudioParameter> parameter);

    /// @brief Remove a registered parameter.
    void unregisterParameter(const std::string& name);

    /// @brief Set a parameter value by name (clamped to the parameter's range).
    /// Propagates immediately to all sources bound to this parameter.
    void setParameter(const std::string& name, float value);

    /// @return Current value of a registered parameter, or 0.0f if not found.
    float getParameter(const std::string& name) const;

    // -----------------------------------------------------------------------
    // Mix snapshots
    // -----------------------------------------------------------------------

    /// @brief Register a snapshot so the engine can look it up by name.
    void registerSnapshot(std::shared_ptr<AudioSnapshot> snapshot);

    /// @brief Blend in a snapshot by name over @p blendTimeSecs.
    /// If another snapshot is already active it is blended out simultaneously.
    void applySnapshot(const std::string& name, double blendTimeSecs = 0.0);

    /// @brief Revert to the baseline mix over @p blendTimeSecs.
    void revertSnapshot(double blendTimeSecs = 0.0);

    // -----------------------------------------------------------------------
    // Interactive / adaptive music
    // -----------------------------------------------------------------------

    /// @brief Request a transition to the named section in the currently
    ///        playing MusicTrack. The transition executes according to the
    ///        rule defined in MusicTrack::addTransition().
    /// @param sectionLabel  Target section name.
    void requestMusicTransition(const std::string& sectionLabel);

    // -----------------------------------------------------------------------
    // Visualization
    // -----------------------------------------------------------------------

    void         enableVisualization(bool enable);
    const float* getVisualizationData(uint32_t& sampleCount) const;

    // -----------------------------------------------------------------------
    // Metadata
    // -----------------------------------------------------------------------

    uint32_t getSampleRate()  const;
    uint32_t getMaxVoices()   const;
    uint32_t getChannels()    const;
    uint32_t getListenerCount() const;

private:
    void* native = nullptr;
};

} // namespace systems::leal::campello_audio
