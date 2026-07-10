/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <memory>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_audio_meter_source.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/song/i_thumbnail_factory.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
backing track for the displayed arrangement and an instrument track for the selected mono input.
Most public methods must be called on the message thread; plugin catalog scans are the explicit
non-realtime worker-thread exceptions because discovery can run slow third-party code.

\see ITransport
\see IPlaybackClock
\see ISongAudio
\see IAudioMeterSource
\see IPluginHost
\see ILiveInput
\see ILiveRig
\see IThumbnailFactory
*/
class Engine : public ITransport,
               public IPlaybackClock,
               public ISongAudio,
               public IAudioDeviceConfiguration,
               public IAudioMeterSource,
               public IPluginHost,
               public ILiveInput,
               public ILiveRig,
               public IToneAutomation,
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
    \brief Reads the most recently published playback-time snapshot.

    Unlike position(), this read is wait-free and callable from any thread: it copies
    RockHero-owned atomic storage and never traverses Tracktion state.

    \return Copy of the latest published playback-clock snapshot.
    */
    [[nodiscard]] PlaybackClockSnapshot snapshot() const noexcept override;

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
    \return Empty success when every arrangement has usable positive-duration audio, or failure.
    */
    [[nodiscard]] std::expected<void, SongAudioError> prepareSong(
        common::core::Song& song) override;

    /*!
    \brief Makes an already-prepared arrangement active in the Tracktion edit.

    The current adapter replaces media only on the Tracktion backing track. The instrument
    track and its input assignment remain separate.

    \param arrangement Prepared arrangement to make active.
    \return Empty success when the playback backend made the arrangement playable, or failure.
    */
    [[nodiscard]] std::expected<void, SongAudioError> setActiveArrangement(
        const common::core::Arrangement& arrangement) override;

    /*!
    \brief Clears the active arrangement from the Tracktion edit and resets playback state.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, SongAudioError> clearActiveArrangement() override;

    /*!
    \brief Mirrors the song tempo map into the edit's tempo sequence, one way.

    Write-only derived output: hosted plugins read host tempo from the edit, and RockHero never
    reads the edit's tempo back. Uses non-remapping, non-clamping inserts with flat step curves,
    matching RockHero's changes-only-at-anchors tempo model exactly in shape.

    \param tempo_map Song tempo map to mirror.
    */
    void mirrorTempoMap(const common::core::TempoMap& tempo_map) override;

    /*!
    \brief Scans conventional VST3 catalog locations through Tracktion's plugin scanner.
    \param progress_callback Optional callback for countable metadata-scan progress.
    \param cancel Cooperative cancellation handle that stops the scan at the next candidate.
    \return Success after the refresh, or a typed failure.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] std::expected<void, PluginHostError> scanPluginCatalog(
        PluginCatalogScanProgressCallback progress_callback = {},
        const common::core::CancellationToken& cancel = {}) override;

    /*!
    \brief Scans supplied plugin files or directories through Tracktion's plugin scanner.
    \param roots Files or directories to inspect for plugin candidates.
    \param progress_callback Optional callback for countable metadata-scan progress.
    \return Discovered plugin candidates, or a typed failure.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginLocations(
        const std::vector<std::filesystem::path>& roots,
        PluginCatalogScanProgressCallback progress_callback = {}) override;

    /*!
    \brief Returns plugin candidates already known by Tracktion.
    \return Plugin candidates without running a plugin scan.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const override;

    /*!
    \brief Inserts a previously discovered plugin candidate into the hosted Tracktion chain.

    The adapter stops and rebuilds backend graph state around the mutation. The instrument input
    route is rebound afterward so monitoring continues through the updated plugin chain. Insertion
    fails once the chain already contains g_max_signal_chain_plugins user plugins.

    \param plugin_candidate Candidate returned by knownPluginCatalog() or a scan method.
    \param chain_index User-visible insertion index in [0, plugin_count] before the chain is full.
    \return Authoritative post-mutation chain snapshot plus inserted runtime ID, or a failure.
    */
    [[nodiscard]] std::expected<PluginInsertResult, PluginHostError> insertPlugin(
        const PluginCandidate& plugin_candidate, std::size_t chain_index) override;

    /*!
    \brief Moves a hosted plugin instance to a final user-visible chain index.
    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \param destination_index Final user-visible chain index for the instance.
    \return Authoritative post-mutation chain snapshot, or a typed failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
        const std::string& instance_id, std::size_t destination_index) override;

    /*!
    \brief Removes a loaded plugin instance from the hosted Tracktion chain.

    The adapter stops and rebuilds backend graph state around the mutation. The instrument input
    route is rebound afterward so monitoring continues through the updated plugin chain.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Authoritative post-mutation chain snapshot, or a typed failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
        const std::string& instance_id) override;

    /*!
    \brief Captures a full opaque state chunk for a hosted plugin instance.
    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Captured plugin state, or a typed failure.
    */
    [[nodiscard]] std::expected<PluginInstanceState, PluginHostError> capturePluginState(
        const std::string& instance_id) override;

    /*!
    \brief Recreates a captured plugin state under its encoded runtime instance ID.
    \param state Opaque plugin state previously captured from this boundary.
    \param chain_index User-visible insertion index.
    \return Authoritative post-recreate snapshot, or a typed failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError>
    recreatePluginStatePreservingId(
        const PluginInstanceState& state, std::size_t chain_index) override;

    /*!
    \brief Restores a full opaque state chunk onto an existing plugin instance.
    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \param state Opaque plugin state previously captured from this boundary.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, PluginHostError> setPluginState(
        const std::string& instance_id, const PluginInstanceState& state) override;

    /*! \brief Flushes pending user plugin edits into completed before/after values. */
    void flushPendingPluginEdits() override;

    /*!
    \brief Reports whether any user plugin edit is waiting to settle or flush.
    \return True while a plugin edit is pending.
    */
    [[nodiscard]] bool hasPendingPluginEdits() const override;

    /*!
    \brief Installs callbacks for pending user plugin edit notifications.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginEditObserver(PluginEditObserver observer) override;

    /*!
    \brief Installs callbacks for completed plugin-wide state edit notifications.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginStateEditObserver(PluginStateEditObserver observer) override;

    /*!
    \brief Installs callbacks for shortcuts received by hosted plugin editor windows.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginWindowCommandObserver(PluginWindowCommandObserver observer) override;

    /*!
    \brief Opens a hosted plugin editor window for the requested runtime instance.
    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, PluginHostError> openPluginWindow(
        const std::string& instance_id) override;

    /*!
    \brief Captures the active live rig chain into a package-relative tone document.
    \param request Song workspace and arrangement identity for the capture.
    \return Written tone document reference and display chain, or a typed failure.
    */
    [[nodiscard]] std::expected<LiveRigSnapshot, LiveRigError> captureActiveRig(
        const LiveRigCaptureRequest& request) override;

    /*!
    \brief Writes a fresh empty tone document into the song workspace.
    \param song_directory Native song workspace directory that owns package-relative tone files.
    \return The new tone's package-relative document reference, or a typed failure.
    */
    /*!
    \brief Appends an empty passthrough branch for a tone to the loaded multi-tone rack.
    \param tone_document_ref Tone the new branch represents.
    \return Empty success, or a typed failure when no rig is loaded or wiring fails.
    */
    [[nodiscard]] std::expected<void, LiveRigError> addEmptyToneBranch(
        const std::string& tone_document_ref) override;

    [[nodiscard]] std::expected<std::string, LiveRigError> mintEmptyTone(
        const std::filesystem::path& song_directory) override;

    /*!
    \brief Loads a package-relative tone document into the active live rig chain.

    Plugin restoration is driven cooperatively via the message loop: each plugin is restored in
    its own message-loop turn so paint and input can run between plugins. The completion callback
    fires on the message thread once the chain is fully restored or the operation fails.

    \param request Song workspace, tone document reference, and optional progress callback.
    \param completion Callback invoked once the operation finishes or fails.
    */
    void loadLiveRig(LiveRigLoadRequest request, LiveRigLoadResultCallback completion) override;

    /*!
    \brief Clears the active live rig chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveRigError> clearLiveRig() override;

    /*!
    \brief Switches which preloaded tone is audible and bound to the signal-chain panel.
    \param tone_document_ref One of the tone references supplied to the last loadLiveRig call.
    \return The now-audible tone's chain and output gain, or a typed failure.
    */
    [[nodiscard]] std::expected<LiveRigLoadResult, LiveRigError> setAudibleTone(
        const std::string& tone_document_ref) override;

    /*!
    \brief Reads the current input gain applied before the signal chain.
    \return Current input gain, or the default when no structural gain plugin exists.
    */
    [[nodiscard]] Gain inputGain() const override;

    /*!
    \brief Reads the current output gain applied after the signal chain.
    \return Current output gain, or the default when no structural gain plugin exists.
    */
    [[nodiscard]] Gain outputGain() const override;

    /*!
    \brief Sets the calibrated input gain applied before the signal chain.
    \param gain Desired input gain; clamped to the accepted range.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setInputGain(Gain gain) override;

    /*!
    \brief Returns the raw input peak meter used for input calibration.
    \return Most recent raw input peak level, or silence when no meter is active.
    */
    [[nodiscard]] AudioMeterLevel rawInputMeterLevel() const override;

    /*!
    \brief Reports whether processed live input monitoring is currently enabled.
    \return True when calibrated live guitar is routed through the chain.
    */
    [[nodiscard]] bool liveInputMonitoringEnabled() const override;

    /*!
    \brief Enables or disables processed live input monitoring explicitly.
    \param enabled True to route calibrated live guitar through the chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setLiveInputMonitoringEnabled(
        bool enabled) override;

    /*!
    \brief Reports whether unprocessed calibration monitoring is currently enabled.
    \return True when live guitar is routed directly to output for calibration.
    */
    [[nodiscard]] bool calibrationInputMonitoringEnabled() const override;

    /*!
    \brief Enables or disables unprocessed calibration monitoring explicitly.
    \param enabled True to route live guitar directly to output for calibration.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setCalibrationInputMonitoringEnabled(
        bool enabled) override;

    /*!
    \brief Sets the output gain applied after the signal chain.
    \param gain Desired output gain; clamped to the accepted range.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, LiveRigError> setOutputGain(Gain gain) override;

    /*!
    \brief Lists the automatable parameters of every plugin in a loaded tone's chain.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \return The tone's automatable parameters, or a typed failure when the tone is not loaded.
    */
    [[nodiscard]] std::expected<std::vector<AutomatableParamInfo>, ToneAutomationError>
    listAutomatableParameters(const std::string& tone_document_ref) const override;

    /*!
    \brief Reads the current automation curve points for one tone-chain plugin parameter.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is read.
    \param param_id Parameter id within that plugin.
    \return The curve points in ascending time, or a typed failure.
    */
    [[nodiscard]] std::expected<std::vector<AutomationCurvePoint>, ToneAutomationError>
    readParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const override;

    /*!
    \brief Replaces one tone-chain plugin parameter's automation curve with the supplied points.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is written.
    \param param_id Parameter id within that plugin.
    \param points Replacement curve points, normalised value, in ascending time.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] std::expected<void, ToneAutomationError> writeParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, std::span<const AutomationCurvePoint> points) override;

    /*!
    \brief Reads one tone-chain plugin parameter's current live value, normalised to `[0, 1]`.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is read.
    \param param_id Parameter id within that plugin.
    \return The current normalised value, or a typed failure.
    */
    [[nodiscard]] std::expected<float, ToneAutomationError> readParameterNormValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const override;

    /*!
    \brief Formats a normalised value as one tone-chain parameter's native display text.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is formatted.
    \param param_id Parameter id within that plugin.
    \param norm_value Value to format, normalised to `[0, 1]`.
    \return The parameter's display text, or a typed failure.
    */
    [[nodiscard]] std::expected<std::string, ToneAutomationError> formatParameterValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, float norm_value) const override;

    /*!
    \brief Parses one tone-chain parameter's display text into a normalised value.
    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter parses the text.
    \param param_id Parameter id within that plugin.
    \param text Display text to parse, in the parameter's native units.
    \return The parsed value normalised to `[0, 1]`, or a typed failure.
    */
    [[nodiscard]] std::expected<float, ToneAutomationError> parseParameterValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, const std::string& text) const override;

    /*!
    \brief Returns the JUCE audio device manager backing the engine.
    \return Reference to the active device manager owned by the audio backend.
    */
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override;

    /*!
    \brief Restores an opaque serialized audio-device state on the message thread.
    \param serialized_state State string previously returned by serializedDeviceState().
    \return Empty success when the state was decoded and restored, or a typed failure.
    */
    [[nodiscard]] std::expected<void, AudioDeviceConfigurationError> restoreSerializedDeviceState(
        const std::string& serialized_state) override;

    /*!
    \brief Captures the current audio-device route as an opaque serialized state string.
    \return Serialized state, or empty when no state can be captured.
    \note Must be called on the message thread.
    */
    [[nodiscard]] std::optional<std::string> serializedDeviceState() const override;

    /*!
    \brief Returns the currently open audio-device route and hardware timing details.
    \return Current device status, or a closed status when no device is open.
    */
    [[nodiscard]] AudioDeviceStatus currentDeviceStatus() const override;

    /*!
    \brief Returns the active one-channel physical input route used by input calibration.
    \return Current input identity, or empty when no valid mono input route is active.
    */
    [[nodiscard]] std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const override;

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
    \brief Reads current live-rig and final-mix peak meters for display.
    \return Current meter snapshot, or silent meters when no playback graph is active.
    */
    [[nodiscard]] AudioMeterSnapshot audioMeterSnapshot() const override;

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
