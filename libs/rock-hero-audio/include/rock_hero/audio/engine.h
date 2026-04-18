/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <filesystem>
#include <memory>

namespace juce
{
class Component;
} // namespace juce

namespace rock_hero::audio
{

class Thumbnail;

/*!
\brief Isolation layer between Tracktion Engine and the rest of the application.

All other code depends on this interface rather than on Tracktion directly. This boundary
enables a fallback-to-raw-JUCE strategy: only rock-hero-audio implementation files
include Tracktion headers.

Owns the tracktion::Engine and the single tracktion::Edit used for playback.
All public methods except getTransportPosition() must be called on the message thread.

\see rock_hero::MainWindow
\see rock_hero::ui::WaveformDisplay
*/
class Engine
{
public:
    /*!
    \brief Creates the Tracktion Engine instance and a single-track Edit for playback.

    Initialises the device manager with stereo output only.
    */
    Engine();

    /*!
    \brief Stops any active transport and tears down the Edit and Engine in the correct order.
    */
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /*!
    \brief Loads an audio file onto track 0, replacing any existing clip.

    Stops playback before mutating clips to avoid mid-stream graph rebuilds. Must be called on
    the message thread.

    \param file The audio file to load.
    \return True if the clip was inserted successfully.
    */
    bool loadFile(const std::filesystem::path& file);

    /*!
    \brief Starts transport playback.
    */
    void play();

    /*!
    \brief Stops transport playback and resets the position to the beginning.
    */
    void stop();

    /*!
    \brief Pauses transport playback, preserving the current position.

    Use this when the user wants to resume from the same point. Contrast with stop(), which
    resets the position to the beginning.
    */
    void pause();

    /*!
    \brief Seeks the transport to the given position in seconds.

    Safe to call while playing (playback continues from the new position) or while stopped/paused
    (only the cursor moves). Clamps to [0, edit length] internally by Tracktion.

    \param seconds Target position in seconds.
    */
    void seek(double seconds);

    /*!
    \brief Reports whether the transport is currently playing.
    \return True when the transport is currently playing.
    */
    [[nodiscard]] bool isPlaying() const;

    /*!
    \brief Returns the current transport position in seconds.

    Lock-free; safe to call from any thread. The value is backed by a std::atomic and currently
    written by the 60 Hz UI timer shim (updateTransportPositionCache). It will be moved to an
    audio-thread callback once ASIO input is wired.

    \return The cached transport position in seconds.
    */
    [[nodiscard]] double getTransportPosition() const noexcept;

    /*!
    \brief Mirrors the Tracktion transport position into the atomic cache.

    Called by WaveformDisplay's 60 Hz timer. Must be called on the message thread.

    \note Temporary shim that will be removed once the audio thread owns the write to the
    transport position cache.
    */
    void updateTransportPositionCache();

    /*!
    \brief Creates a Thumbnail bound to this engine.

    Factory method that passes the internal Tracktion Engine to the thumbnail without exposing
    it through the public API.

    \param owner The component that should be repainted when the proxy finishes generating.
    \return A new Thumbnail instance.
    */
    [[nodiscard]] std::unique_ptr<Thumbnail> createThumbnail(juce::Component& owner);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::audio