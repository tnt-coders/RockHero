/** @file audio_engine.h
    @brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <atomic>
#include <memory>

#include <juce_core/juce_core.h>

// Forward declarations; callers need not include Tracktion headers.
namespace tracktion
{
inline namespace engine
{
class Engine;
class Edit;
} // namespace engine
} // namespace tracktion

namespace rock_hero
{

/** Isolation layer between Tracktion Engine and the rest of the application.

    All other code depends on this interface rather than on Tracktion directly. This boundary
    enables a fallback-to-raw-JUCE strategy: only audio_engine.cpp and waveform_display.cpp (with a
    documented TODO to fix) ever include Tracktion headers.

    Owns the tracktion::Engine and the single tracktion::Edit used for playback. All public methods
    except getTransportPosition() must be called on the message thread.

    @see EditorWindow, WaveformDisplay
*/
class AudioEngine
{
public:
    /** Creates the Tracktion Engine instance and a single-track Edit for playback. Initialises the
        device manager with stereo output only.
    */
    AudioEngine();

    /** Stops any active transport and tears down the Edit and Engine in the correct order. */
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    /** Loads an audio file onto track 0, replacing any existing clip.

        Stops playback before mutating clips to avoid mid-stream graph rebuilds.
        Must be called on the message thread.

        @param file  the audio file to load (wav, mp3, aiff, ogg, flac)
        @return true if the clip was inserted successfully
    */
    bool loadFile(const juce::File& file);

    /** Starts transport playback. Must be called on the message thread. */
    void play();

    /** Stops transport playback. Must be called on the message thread. */
    void stop();

    /** Returns true if the transport is currently playing.
        Must be called on the message thread.
    */
    [[nodiscard]] bool isPlaying() const;

    /** Returns the current transport position in seconds.

        Lock-free; safe to call from any thread. The value is backed by a std::atomic and currently
        written by the 60 Hz UI timer shim (updateTransportPositionCache). Will be moved to an
        audio-thread callback once ASIO input is wired.
    */
    [[nodiscard]] double getTransportPosition() const noexcept;

    /** Mirrors the Tracktion transport position into the atomic cache.

        Called by WaveformDisplay's 60 Hz timer. Must be called on the message thread.

        @note Temporary shim -- will be removed once the audio thread owns the write to the
              transport position cache.
    */
    void updateTransportPositionCache();

    /** Returns a reference to the underlying tracktion::Engine.

        This is an intentional bounded Tracktion type leak. Only audio_engine.cpp and
        waveform_display.cpp should call this.
    */
    [[nodiscard]] tracktion::Engine& getEngine() noexcept;

private:
    std::unique_ptr<tracktion::Engine> m_engine;
    std::unique_ptr<tracktion::Edit> m_edit;
    std::atomic<double> m_transport_position{0.0};
};

} // namespace rock_hero
