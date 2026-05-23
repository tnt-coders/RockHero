/*!
\file editor_controller.h
\brief Headless editor workflow coordinator backed by core, transport, and audio activation.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/audio_loudness_metadata.h>
#include <rock_hero/common/core/session.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/project.h>
#include <string>

namespace rock_hero::common::audio
{
class IAudio;
class IAudioDeviceConfiguration;
class ILiveRig;
class IPluginHost;
class ITransport;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::core
{

class EditorSettings;
class IEditorTaskRunner;
class IEditorView;

/*!
\brief Concrete editor workflow coordinator.

Translates editor user intents into transport, audio, and project requests without exposing JUCE
types. Subscribes to the transport listener surface for coarse transition-shaped updates and
re-derives EditorViewState whenever a real change has occurred. The controller owns
error-reporting policy, play/pause/stop gating, and seek normalization. Continuous playhead motion
is not the controller's responsibility; the editor view pulls position from ITransport::position()
at its own render cadence. The controller samples position only for discrete workflow gates such
as whether Stop can reset the cursor. It provides only discrete cursor mapping state, such as
visible timeline range, through EditorViewState.

The referenced transport, audio, optional audio-device configuration, and optional plugin-host
ports must outlive the controller.
*/
class EditorController final : public IEditorController
{
public:
    /*! \brief Opens an editor project package into a project context. */
    using OpenFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path)>;

    /*!
    \brief Imports a song source into a project context.

    The controller supplies the analysis function so the import worker can update busy-state
    copy when Project::import reaches the audio-analysis phase.
    */
    using ImportFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path,
        const AudioAnalyzeForGainFunction& analyze_audio)>;

    /*! \brief Saves the current song through the project context. */
    using SaveFunction = std::function<std::expected<void, ProjectError>(
        Project& project, const common::core::Song& song, ProjectEditorState editor_state)>;

    /*! \brief Saves the current song to a chosen path through the project context. */
    using SaveAsFunction = std::function<std::expected<void, ProjectError>(
        Project& project, const std::filesystem::path& path, const common::core::Song& song,
        ProjectEditorState editor_state)>;

    /*! \brief Publishes the current song to a chosen native song package path. */
    using PublishFunction = std::function<std::expected<void, ProjectError>(
        Project& project, const std::filesystem::path& path, const common::core::Song& song)>;

    /*! \brief Requests host exit after controller-level shutdown policy has completed. */
    using ExitFunction = std::function<void()>;

    /*!
    \brief Measures the loudness of an opened project's backing audio asset.

    Called on a background thread after project open succeeds. The controller compares the
    returned analysis against the asset's persisted loudness metadata and the configured target
    to decide whether to publish a BackingAudioNormalizationPrompt.
    */
    using AudioAnalyzeFunction = std::function<
        std::expected<common::core::AudioLoudnessAnalysis, common::audio::AudioNormalizationError>(
            const std::filesystem::path& input)>;

    /*!
    \brief Optional services used by the editor controller.

    Default-constructed functions are replaced with production project IO behavior. Tests and app
    composition can replace only the services they need without relying on positional callback
    arguments.
    */
    struct Services final
    {
        /*! \brief Opens editor project packages. */
        OpenFunction open_function{};

        /*! \brief Imports song sources into editor workspaces. */
        ImportFunction import_function{};

        /*! \brief Saves the current editor project. */
        SaveFunction save_function{};

        /*! \brief Saves the current editor project to a chosen path. */
        SaveAsFunction save_as_function{};

        /*! \brief Publishes the current song as a native song package. */
        PublishFunction publish_function{};

        /*! \brief Requests host shutdown after guarded editor exit succeeds. */
        ExitFunction exit_function{};

        /*!
        \brief Measures backing audio loudness on a background thread after project open.

        Default-constructed in production composition wraps
        common::audio::measureAudioLoudness; tests inject fakes that return canned analyses to
        control the open-time prompt flow without running the real analyzer.
        */
        AudioAnalyzeFunction audio_analyze_function{};

        /*! \brief Optional settings store used for startup restore and exit persistence. */
        EditorSettings* settings{};

        /*!
        \brief Optional task runner for off-thread project IO.

        When null, the controller falls back to an inline runner that executes work and
        completion synchronously on the message thread. Production composition supplies the
        JUCE-backed runner so open and import complete off-thread; tests typically pass null and
        rely on the inline default.
        */
        IEditorTaskRunner* task_runner{};
    };

    /*!
    \brief Builds the controller, subscribes to transport, and captures initial view state.

    The owned session starts empty until the user opens a project. The controller does not push
    state during construction because no view is attached yet. The initial cached state becomes the
    first push delivered to attachView().

    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param audio_devices Audio-device configuration port used for ASIO input/output routing.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration& audio_devices,
        Services services = defaultServices());

    /*!
    \brief Builds the controller with a plugin-host backend.

    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param audio_devices Audio-device configuration port used for ASIO input/output routing.
    \param plugin_host Plugin-host port used to mutate the plugin chain.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration& audio_devices,
        common::audio::IPluginHost& plugin_host, Services services = defaultServices());

    /*!
    \brief Builds the controller with plugin hosting and persistent live rig storage.

    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param audio_devices Audio-device configuration port used for ASIO input/output routing.
    \param plugin_host Plugin-host port used to mutate the plugin chain.
    \param live_rig Live rig port used to save and restore tone documents.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration& audio_devices,
        common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
        Services services = defaultServices());

    /*!
    \brief Builds the controller without an audio-device backend.

    This overload is used by tests and temporary hosts that do not expose audio-device
    configuration.

    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        Services services = defaultServices());

    /*!
    \brief Builds the controller with plugin hosting but without audio-device settings UI.
    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param plugin_host Plugin-host port used to mutate the plugin chain.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IPluginHost& plugin_host, Services services = defaultServices());

    /*!
    \brief Builds the controller with plugin hosting and tone storage but no device settings UI.
    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param plugin_host Plugin-host port used to mutate the plugin chain.
    \param live_rig Live rig port used to save and restore tone documents.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
        Services services = defaultServices());

    /*! \brief Releases the transport listener registration before owned references go away. */
    ~EditorController() override;

    /*! \brief Copying is disabled because the controller owns listener registration. */
    EditorController(const EditorController&) = delete;

    /*!
    \brief Copy assignment is disabled because the controller owns a transport listener
    registration.
    */
    EditorController& operator=(const EditorController&) = delete;

    /*! \brief Moving is disabled so the listener registration stays bound to one controller. */
    EditorController(EditorController&&) = delete;

    /*!
    \brief Move assignment is disabled so the listener registration stays bound to one controller.
    */
    EditorController& operator=(EditorController&&) = delete;

    /*!
    \brief Binds a view non-owningly and pushes the cached editor state once.

    Subsequent transport transitions and intent results push fresh state through the same view.
    Calling attachView() with a different view replaces the binding; the previous view stops
    receiving updates.

    \param view View to receive future state pushes.
    */
    void attachView(IEditorView& view);

    /*!
    \brief Returns read-only access to the loaded editor session.
    \return Session state owned by this controller.
    */
    [[nodiscard]] const common::core::Session& session() const noexcept;

    /*!
    \brief Returns the editor project file that can be reopened on the next launch.
    \return Current `.rhp` project path, or empty when the loaded work has no project file.
    */
    [[nodiscard]] std::optional<std::filesystem::path> currentProjectFile() const;

    /*! \brief Restores the previously open project when settings contain a still-valid path. */
    void restoreLastOpenProject();

    /*!
    \brief Handles a request to open an editor project package.

    On success, the controller stores the project context. On failure, the old session/context are
    preserved and a transient view error is emitted. Reentrant transport
    notifications received during backend arrangement activation are coalesced into one final push.

    \param file Filesystem path selected by the user.
    */
    void onOpenRequested(std::filesystem::path file) override;

    /*!
    \brief Handles a request to import a song source.

    On success, the controller stores an unsaved workspace. On failure, the old session/context are
    preserved and a transient view error is emitted. Reentrant transport
    notifications received during backend arrangement activation are coalesced into one final push.

    \param file Filesystem path selected by the user.
    */
    void onImportRequested(std::filesystem::path file) override;

    /*! \brief Handles a request to save to the current destination. */
    void onSaveRequested() override;

    /*!
    \brief Handles a request to save to a chosen destination.
    \param file Filesystem path selected by the user.
    */
    void onSaveAsRequested(std::filesystem::path file) override;

    /*!
    \brief Handles a request to publish a native song package.
    \param file Filesystem path selected by the user.
    */
    void onPublishRequested(std::filesystem::path file) override;

    /*! \brief Handles cancellation of a controller-requested Save As chooser. */
    void onSaveAsCancelled() override;

    /*! \brief Handles a request to close the current project. */
    void onCloseRequested() override;

    /*! \brief Handles a request to exit the editor application. */
    void onExitRequested() override;

    /*!
    \brief Handles a decision from the unsaved-changes prompt.
    \param decision Decision selected by the user.
    */
    void onUnsavedChangesDecision(UnsavedChangesDecision decision) override;

    /*!
    \brief Handles a decision from the backing-audio normalization prompt.
    \param decision Decision selected by the user.
    */
    void onBackingAudioNormalizationDecision(BackingAudioNormalizationDecision decision) override;

    /*!
    \brief Handles a play/pause button press from the editor UI.

    The intent is ignored when no arrangement is loaded. Otherwise, plays or pauses
    based on the current transport state.
    */
    void onPlayPausePressed() override;

    /*!
    \brief Handles a stop button press from the editor UI.

    The intent is ignored when the transport is not currently playing and is already at the start
    of the loaded timeline, mirroring the published EditorViewState.stop_enabled value.
    */
    void onStopPressed() override;

    /*!
    \brief Handles a click on a waveform at a normalized horizontal position.

    The input is clamped to [0, 1] and converted through the session timeline range. A duration
    of zero results in a seek to the start of the timeline.

    \param normalized_x Click position normalized to the interval [0, 1].
    */
    void onWaveformClicked(double normalized_x) override;

    /*! \brief Shows the scanned plugin browser and starts an initial catalog scan when needed. */
    void onPluginBrowserRequested() override;

    /*! \brief Hides the scanned plugin browser. */
    void onPluginBrowserClosed() override;

    /*! \brief Starts a plugin catalog scan for the browser. */
    void onPluginCatalogScanRequested() override;

    /*!
    \brief Adds a selected browser plugin to the runtime plugin chain.
    \param plugin_id Opaque plugin ID selected by the user.
    */
    void onAddPluginRequested(std::string plugin_id) override;

    /*!
    \brief Removes a plugin instance from the current runtime plugin chain.

    Removal marks the project dirty when a persistent live rig port is available.

    \param instance_id Opaque plugin instance ID selected by the user.
    */
    void onRemovePluginRequested(std::string instance_id) override;

    /*!
    \brief Opens a plugin instance editor window for the current runtime plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    void onOpenPluginRequested(std::string instance_id) override;

    /*!
    \brief Schedules audio-device open work behind the editor's busy overlay.
    \param change_audio_device Callable run after the busy overlay paints.
    */
    void onAudioDeviceChangeRequested(std::function<void()> change_audio_device) override;

private:
    // Supplies a named default-argument target after Services has been declared.
    [[nodiscard]] static Services defaultServices();

    // Shared constructor body used by public overloads.
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration* audio_devices,
        common::audio::IPluginHost* plugin_host, common::audio::ILiveRig* live_rig,
        Services services);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::editor::core
