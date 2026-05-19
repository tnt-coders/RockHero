/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <string>
#include <string_view>

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

Owns the tracktion::Engine and the tracktion::Edit used for playback. The current adapter keeps a
backing track for the displayed arrangement and an instrument track for the selected mono
input. Most public methods must be called on the message thread; scanPluginFile() is the explicit
non-realtime worker-thread exception because selected-file inspection can run slow third-party
code.

\see ITransport
\see IAudio
\see IPluginHost
\see ILiveRig
\see IThumbnailFactory
*/
class Engine : public ITransport,
               public IAudio,
               public IAudioDeviceConfiguration,
               public IPluginHost,
               public ILiveRig,
               public IThumbnailFactory
{
public:
    /*!
    \brief Handles a Tracktion plugin-scan child process command line.
    \param command_line Command line supplied by JUCE application startup.
    \return True when the command line was consumed by the scanner child process.
    */
    [[nodiscard]] static bool startPluginScanChildProcess(std::string_view command_line);

    /*!
    \brief Reports whether a command line targets Tracktion plugin scanning.
    \param command_line Command line supplied by JUCE application startup.
    \return True when the command line is for a plugin scan child process.
    */
    [[nodiscard]] static bool isPluginScanChildProcessCommandLine(std::string_view command_line);

    /*!
    \brief Creates the Tracktion Engine instance and two-track Edit for playback.

    Initialises the device manager with one instrument input and stereo output. The selected
    app-local audio-device route is rebound to the instrument track whenever device settings
    change.
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

    The current adapter replaces media only on the Tracktion backing track. The instrument
    track and its input assignment remain separate.

    \param arrangement Prepared arrangement to make active.
    \return True when the playback backend made the arrangement playable.
    */
    [[nodiscard]] bool setActiveArrangement(const common::core::Arrangement& arrangement) override;

    /*! \brief Clears the active arrangement from the Tracktion edit and resets playback state. */
    void clearActiveArrangement() override;

    /*!
    \brief Scans one plugin file or bundle for candidates loadable by the plugin chain.
    \param plugin_path Path to a plugin file or plugin bundle.
    \return Discovered plugin candidates, or a typed failure.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginFile(
        const std::filesystem::path& plugin_path) override;

    /*!
    \brief Appends a previously scanned plugin candidate to the hosted Tracktion chain.

    The adapter stops and rebuilds backend graph state around the mutation. The instrument input
    route is rebound afterward so monitoring continues through the updated plugin chain.

    \param plugin_id Opaque candidate ID returned by scanPluginFile().
    \return Handle for the inserted plugin instance, or a typed failure.
    */
    [[nodiscard]] std::expected<PluginHandle, PluginHostError> addPlugin(
        const std::string& plugin_id) override;

    /*!
    \brief Removes a loaded plugin instance from the hosted Tracktion chain.

    The adapter stops and rebuilds backend graph state around the mutation. The instrument input
    route is rebound afterward so monitoring continues through the updated plugin chain.

    \param instance_id Opaque instance ID returned by addPlugin().
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, PluginHostError> removePlugin(
        const std::string& instance_id) override;

    /*!
    \brief Captures the active live rig chain into a package-relative tone document.
    \param request Song workspace and arrangement identity for the capture.
    \return Written tone document reference and display chain, or a typed failure.
    */
    [[nodiscard]] std::expected<LiveRigSnapshot, LiveRigError> captureActiveRig(
        const LiveRigCaptureRequest& request) override;

    /*!
    \brief Loads a package-relative tone document into the active live rig chain.

    Plugin restoration is driven cooperatively via the message loop: each plugin is restored in
    its own message-loop turn so paint and input can run between plugins. The completion callback
    fires on the message thread once the chain is fully restored or the operation fails.

    \param request Song workspace, tone document reference, and optional progress callback.
    \param completion Callback invoked once the operation finishes or fails.
    */
    void loadRig(LiveRigLoadRequest request, LiveRigLoadResultCallback on_result) override;

    /*!
    \brief Clears the active live rig chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveRigError> clearRig() override;

    /*!
    \brief Returns the JUCE audio device manager backing the engine.
    \return Reference to the active device manager owned by the audio backend.
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
