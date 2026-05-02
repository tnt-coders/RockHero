/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <memory>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/audio/i_transport.h>

namespace juce
{
// Forward declaration for UI owners that request engine-created thumbnail adapters.
class Component;

// Forward declaration for file loading without including JUCE headers in this public adapter.
class File;
} // namespace juce

namespace rock_hero::audio
{

// Forward declaration of the Tracktion-free thumbnail interface returned by Engine.
class IThumbnail;

/*!
\brief Isolation layer between Tracktion Engine and the rest of the application.

All other code depends on the project-owned audio interfaces rather than on Tracktion directly.
This boundary enables a fallback-to-raw-JUCE strategy: only rock-hero-audio implementation files
include Tracktion headers.

Owns the tracktion::Engine and the single tracktion::Edit used for playback. All public methods
must be called on the message thread.

\see rock_hero::MainWindow
*/
class Engine : public ITransport, public IEdit, public IThumbnailFactory
{
public:
    /*!
    \brief Creates the Tracktion Engine instance and a single-track Edit for playback.

    Initialises the device manager with stereo output only.
    */
    Engine();

    /*! \brief Stops transport and tears down Tracktion objects in dependency order. */
    ~Engine() override;

    /*! \brief Copying is disabled because Tracktion runtime state has unique ownership. */
    Engine(const Engine&) = delete;

    /*! \brief Copy assignment is disabled because Tracktion runtime state has unique ownership. */
    Engine& operator=(const Engine&) = delete;

    /*! \brief Moving is disabled so listener registrations and adapter references stay stable. */
    Engine(Engine&&) = delete;

    /*!
    \brief Move assignment is disabled so listener registrations and adapter references stay
    stable.
    */
    Engine& operator=(Engine&&) = delete;

    /*! \brief Starts transport playback. */
    void play() override;

    /*! \brief Stops transport playback and resets the position to the beginning. */
    void stop() override;

    /*!
    \brief Pauses transport playback, preserving the current position.

    Use this when the user wants to resume from the same point. Contrast with stop(), which
    resets the position to the beginning.
    */
    void pause() override;

    /*!
    \brief Moves the transport to the given timeline position.
    \param position Target playback position on the session timeline.
    */
    void seek(core::TimePosition position) override;

    /*!
    \brief Reads the current coarse transport state.
    \return Current message-thread coarse playback state.
    */
    [[nodiscard]] TransportState state() const noexcept override;

    /*!
    \brief Reads the current Tracktion position.
    \return Current position.
    */
    [[nodiscard]] core::TimePosition position() const noexcept override;

    /*!
    \brief Registers a project-owned transport listener.
    \param listener The listener to notify until it is removed.
    */
    void addListener(ITransport::Listener& listener) override;

    /*!
    \brief Removes a previously registered project-owned transport listener.
    \param listener The same listener previously registered with addListener().
    */
    void removeListener(ITransport::Listener& listener) override;

    /*!
    \brief Loads a framework-free audio asset into the current single-track playback edit.

    This is the first concrete implementation of audio::IEdit. It adapts the existing single-file
    playback path and therefore binds the single Tracktion audio track to the first successfully
    loaded track id. Later loads for a different track id are rejected until multi-track playback
    exists.

    \param track_id Track whose clip should be updated.
    \param audio_asset Framework-free asset reference to load.
    \param position Requested start position on the session timeline.
    \return Accepted clip when the playback backend loaded the asset.
    */
    [[nodiscard]] std::optional<core::AudioClip> loadAudioAsset(
        core::TrackId track_id, const core::AudioAsset& audio_asset,
        core::TimePosition position) override;

    /*!
    \brief Creates an IThumbnail bound to this engine.

    Factory method that passes the internal Tracktion Engine to the thumbnail without exposing it
    through the public API.

    \param owner The component that should be repainted when the proxy finishes generating.
    \return A new IThumbnail instance.
    */
    [[nodiscard]] std::unique_ptr<IThumbnail> createThumbnail(juce::Component& owner) override;

private:
    // Opaque Tracktion/JUCE implementation keeps third-party headers out of this public header.
    struct Impl;

    // Owns all Tracktion runtime objects and listener state.
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::audio
