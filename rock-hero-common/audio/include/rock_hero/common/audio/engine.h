/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_guitar_input.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <string>

namespace juce
{
// Forward declaration for UI owners that request engine-created thumbnail adapters.
class Component;

// Forward declaration for the audio device manager exposed through the configuration port.
class AudioDeviceManager;
} // namespace juce

namespace rock_hero::common::audio
{

// Forward declaration of the Tracktion-free thumbnail interface returned by Engine.
class IThumbnail;

/*!
\brief Isolation layer between Tracktion Engine and the rest of the application.

All other code depends on the project-owned audio interfaces rather than on Tracktion directly.
This boundary enables a fallback-to-raw-JUCE strategy: only common/audio implementation files
include Tracktion headers.

Owns the tracktion::Engine and the single tracktion::Edit used for playback. The current adapter
uses one Tracktion audio track for the currently displayed arrangement. Live guitar monitoring
adds a dry monitor signal of the currently open input through JUCE's shared device callback.
All public methods must be called on the message thread.

\see ITransport
\see IAudio
\see IGuitarInput
\see IThumbnailFactory
*/
class Engine : public ITransport,
               public IAudio,
               public IAudioDeviceConfiguration,
               public IGuitarInput,
               public IThumbnailFactory
{
public:
    /*!
    \brief Creates the Tracktion Engine instance and a single-track Edit for playback.

    Initialises the device manager with stereo output only. The selected ASIO input/output route is
    opened after the user chooses an app-local audio-device configuration.
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

    /*! \brief Stops playback, clears backend playback state, and resets the position. */
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
    void seek(common::core::TimePosition position) override;

    /*!
    \brief Reads the current coarse transport state.
    \return Current message-thread coarse playback state.
    */
    [[nodiscard]] TransportState state() const noexcept override;

    /*!
    \brief Reads the current playback position used for render-cadence cursor drawing.
    \return Current playback position.
    */
    [[nodiscard]] common::core::TimePosition position() const noexcept override;

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
    \brief Validates song arrangement audio and fills accepted durations.

    The current adapter opens every referenced audio file through Tracktion long enough to verify
    that it is readable and has a positive duration.

    \param song Candidate song to prepare for session loading.
    \return True when every arrangement has usable positive-duration audio.
    */
    [[nodiscard]] bool prepareSong(common::core::Song& song) override;

    /*!
    \brief Makes an already-prepared arrangement active in the Tracktion edit.

    The current adapter supports one Tracktion audio track and replaces its media with the
    arrangement's full-source asset.

    \param arrangement Prepared arrangement to make active.
    \return True when the playback backend made the arrangement playable.
    */
    [[nodiscard]] bool setActiveArrangement(const common::core::Arrangement& arrangement) override;

    /*! \brief Clears the active arrangement from the Tracktion edit and resets playback state. */
    void clearActiveArrangement() override;

    /*!
    \brief Returns the JUCE audio device manager backing the engine.
    \return Reference to the live device manager owned by the audio backend.
    */
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override;

    /*!
    \brief Returns the name of the currently open audio device, if any.
    \return Current device name, or empty when no device is open.
    */
    [[nodiscard]] std::optional<std::string> currentDeviceName() const override;

    /*!
    \brief Registers a listener notified after audio device configuration changes.
    \param listener Listener that should be notified until it is removed.
    */
    void addListener(IAudioDeviceConfiguration::Listener& listener) override;

    /*!
    \brief Removes a previously registered audio-device-configuration listener.
    \param listener Listener previously registered with addListener().
    */
    void removeListener(IAudioDeviceConfiguration::Listener& listener) override;

    /*! \brief Enables live monitoring of the currently configured input. */
    void enableGuitarMonitoring() override;

    /*! \brief Disables live monitoring without affecting device configuration. */
    void disableGuitarMonitoring() override;

    /*!
    \brief Reports whether live guitar monitoring is currently enabled.
    \return True when the configured input is being routed to the audio output.
    */
    [[nodiscard]] bool isGuitarMonitoringEnabled() const noexcept override;

    /*!
    \brief Creates an IThumbnail bound to this engine.

    Factory method that passes the internal Tracktion Engine to the thumbnail without exposing it
    through the public API.

    \param owner The component that should be repainted when the proxy finishes generating.
    \return A new IThumbnail instance.
    \note The returned thumbnail must be destroyed before the owner component and this Engine.
    */
    [[nodiscard]] std::unique_ptr<IThumbnail> createThumbnail(juce::Component& owner) override;

private:
    // Opaque Tracktion/JUCE implementation keeps third-party headers out of this public header.
    struct Impl;

    // Owns all Tracktion runtime objects and listener state.
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::common::audio
