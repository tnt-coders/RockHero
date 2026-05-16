/*!
\file editor_controller.h
\brief Headless editor workflow coordinator backed by core, transport, and audio activation.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/scoped_listener.h>
#include <rock_hero/common/core/session.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/i_editor_task_runner.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <rock_hero/editor/core/inline_editor_task_runner.h>
#include <rock_hero/editor/core/project.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

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
class EditorController final : public IEditorController,
                               private common::audio::ITransport::Listener,
                               private common::audio::IAudioDeviceConfiguration::Listener
{
public:
    /*! \brief Opens an editor project package into a project context. */
    using OpenFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path)>;

    /*! \brief Imports a song source into a project context. */
    using ImportFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path)>;

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

    /*!
    \brief Scans a plugin file and appends the first candidate to the plugin chain.

    The initial UI path intentionally handles the common one-candidate VST3 case. Future chooser
    state can branch on multiple scanned candidates without changing the controller/view boundary.

    \param file Filesystem path selected by the user.
    */
    void onAddPluginRequested(std::filesystem::path file) override;

private:
    // Supplies a named default-argument target after Services has been declared.
    [[nodiscard]] static Services defaultServices();

    // Shared constructor body used by public overloads.
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration* audio_devices,
        common::audio::IPluginHost* plugin_host, Services services);

    class ProjectCommand final
    {
    public:
        [[nodiscard]] static ProjectCommand open(std::filesystem::path file);
        [[nodiscard]] static ProjectCommand importSong(std::filesystem::path file);
        [[nodiscard]] static ProjectCommand close() noexcept;
        [[nodiscard]] static ProjectCommand exit() noexcept;

        [[nodiscard]] ProjectCommandId id() const noexcept;
        [[nodiscard]] std::filesystem::path takeFile() noexcept;

    private:
        explicit ProjectCommand(ProjectCommandId id) noexcept;
        ProjectCommand(ProjectCommandId id, std::filesystem::path file);

        ProjectCommandId m_id{ProjectCommandId::Close};
        std::filesystem::path m_file{};
    };

    // Controller action value used by the private dispatch policy and action router.
    class EditorAction final
    {
    public:
        enum class Id : std::uint8_t
        {
            OpenProject,
            RestoreProject,
            ImportProject,
            SaveProject,
            SaveProjectAs,
            PublishProject,
            CloseProject,
            ExitApplication,
            ResolveUnsavedChangesPrompt,
            CancelSaveAsPrompt,
            PlayPause,
            Stop,
            SeekWaveform,
            AddPlugin,
        };

        [[nodiscard]] static EditorAction openProject(std::filesystem::path file);
        [[nodiscard]] static EditorAction restoreProject(std::filesystem::path file);
        [[nodiscard]] static EditorAction importProject(std::filesystem::path file);
        [[nodiscard]] static EditorAction saveProject() noexcept;
        [[nodiscard]] static EditorAction saveProjectAs(std::filesystem::path file);
        [[nodiscard]] static EditorAction publishProject(std::filesystem::path file);
        [[nodiscard]] static EditorAction closeProject() noexcept;
        [[nodiscard]] static EditorAction exitApplication() noexcept;
        [[nodiscard]] static EditorAction resolveUnsavedChangesPrompt(
            UnsavedChangesDecision decision) noexcept;
        [[nodiscard]] static EditorAction cancelSaveAsPrompt() noexcept;
        [[nodiscard]] static EditorAction playPause() noexcept;
        [[nodiscard]] static EditorAction stop() noexcept;
        [[nodiscard]] static EditorAction seekWaveform(double normalized_x) noexcept;
        [[nodiscard]] static EditorAction addPlugin(std::filesystem::path file);

        [[nodiscard]] Id id() const noexcept;
        [[nodiscard]] UnsavedChangesDecision decision() const noexcept;
        [[nodiscard]] double normalizedX() const noexcept;
        [[nodiscard]] std::filesystem::path takeFile() noexcept;

    private:
        explicit EditorAction(Id id) noexcept;
        EditorAction(Id id, std::filesystem::path file);
        EditorAction(Id id, UnsavedChangesDecision decision) noexcept;
        EditorAction(Id id, double normalized_x) noexcept;

        Id m_id{Id::SaveProject};
        std::filesystem::path m_file{};
        UnsavedChangesDecision m_decision{UnsavedChangesDecision::Cancel};
        double m_normalized_x{};
    };

    // Busy policy for actions that enter through the controller action gate.
    enum class ActionBusyPolicy : std::uint8_t
    {
        // Normal mutating actions are blocked until the active busy operation finishes.
        BlockedByBusy,

        // Superseding actions intentionally invalidate the active busy operation before running.
        SupersedesBusy,

        // Cooperative actions may run during busy without clearing or invalidating it.
        AllowedWhileBusy,
    };

    // Forward-declared per-operation task states; full definitions live in the .cpp file because
    // they are private implementation details of the open/import paths.
    struct OpenTaskState;
    struct ImportTaskState;

    // Transport listener entry point; receives only coarse transition-shaped callbacks.
    void onTransportStateChanged(common::audio::TransportState state) override;

    // Audio-device listener entry point; called after the device manager state changes.
    void onAudioDeviceConfigurationChanged() override;

    // Runs a controller action after applying the shared action gate.
    void runAction(EditorAction action);

    // Applies controller availability and busy policy before an action body runs.
    [[nodiscard]] bool prepareAction(EditorAction::Id action);

    // Executes an accepted controller action.
    void performAction(EditorAction action);

    // Reports whether the action is available in the current controller state.
    [[nodiscard]] bool canRunAction(EditorAction::Id action) const;

    // Reports whether an action is available before applying any busy-state override.
    [[nodiscard]] bool actionAvailableWhenIdle(EditorAction::Id action) const;

    // Returns how the action interacts with active busy state.
    [[nodiscard]] static ActionBusyPolicy actionBusyPolicy(EditorAction::Id action) noexcept;

    // Requests a project-level command, prompting first when unsaved changes are present.
    void requestProjectCommand(ProjectCommand command);

    // Runs a project-level command after prompts and save requirements are satisfied.
    void runProjectCommand(ProjectCommand command);

    // Opens an editor project package without first checking unsaved-change state. Begins busy,
    // dispatches package IO to the task runner, and returns immediately. Final commit happens
    // in completeOpenProject() on the message thread.
    void openProject(const std::filesystem::path& file, bool clear_last_open_project_on_failure);

    // Message-thread completion for openProject(). Honors the busy-generation token: a stale
    // completion (token != current generation) returns without touching session, project, or
    // busy state because Close, Exit, or a superseding operation already owns the live state.
    void completeOpenProject(std::uint64_t token, const std::shared_ptr<OpenTaskState>& state);

    // Imports a song source without first checking unsaved-change state. Begins busy, dispatches
    // import IO to the task runner, and returns immediately. Final commit happens in
    // completeImportSongSource() on the message thread.
    void importSongSource(const std::filesystem::path& file);

    // Message-thread completion for importSongSource(). Same stale-generation semantics as
    // completeOpenProject().
    void completeImportSongSource(
        std::uint64_t token, const std::shared_ptr<ImportTaskState>& state);

    // Closes the current project context, session, and backend audio state.
    [[nodiscard]] bool closeProject();

    // Saves to the current destination and updates dirty tracking on success.
    [[nodiscard]] bool saveProject();

    // Continues the stored deferred command after a successful prompted save.
    void continueDeferredProjectCommand();

    // Clears any in-progress unsaved-change or Save As prompt.
    void clearDeferredProjectCommand() noexcept;

    // Builds the editor-only project state persisted by Save and Save As.
    [[nodiscard]] ProjectEditorState projectEditorStateForSave() const;

    // Prepares project audio, activates the selected arrangement, and commits to Session.
    [[nodiscard]] bool loadSessionSong(
        common::core::Song song, const std::optional<std::string>& selected_arrangement);

    // Builds a fresh EditorViewState from the current session and transport state.
    [[nodiscard]] EditorViewState deriveViewState() const;

    // Reports whether a busy operation is currently active.
    [[nodiscard]] bool isBusy() const noexcept;

    // Stores a new busy state, increments the generation token, and fills the default message.
    // Every operation captures the returned token and compares it before committing state so a
    // superseded completion can be detected and discarded.
    [[nodiscard]] std::uint64_t beginBusy(BusyOperation operation);

    // Finishes the active busy operation after its matching completion commits or fails.
    void finishBusyOperation();

    // Invalidates an in-flight busy operation before a superseding action runs.
    void supersedeBusyOperation();

    // Low-level busy-state primitive shared by the semantic finish/supersede helpers. Stale
    // completions must never call this: the live busy state belongs to the newer operation.
    void endBusy();

    // Restores the saved audio-device manager state on startup when a backend is available.
    void restoreAudioDeviceState();

    // Persists the current audio-device manager state through settings, if both are available.
    void persistAudioDeviceState();

    // Derives a fresh state, caches it, and pushes it to the attached view if any.
    void deriveAndPush();

    // Sends a one-shot workflow error to the attached view.
    void reportError(const std::string& message);

    // Reports whether the controller has committed a backend-accepted arrangement.
    [[nodiscard]] bool hasLoadedArrangement() const;

    // Reports whether closing or replacing the current project would discard user work.
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

    // Reports whether Stop would either stop playback or reset a non-start cursor position.
    [[nodiscard]] bool canStopTransport(const common::audio::TransportState& transport_state) const;

    // Transport port used for control intents and coarse listener delivery.
    common::audio::ITransport& m_transport;

    // Audio port used for project audio validation and selected-arrangement loading.
    common::audio::IAudio& m_audio;

    // Optional audio-device port used for ASIO input/output routing.
    common::audio::IAudioDeviceConfiguration* m_audio_devices{};

    // Optional plugin-host port used to mutate the processing chain.
    common::audio::IPluginHost* m_plugin_host{};

    // Song aggregate and selected arrangement state currently loaded in the editor.
    common::core::Session m_session;

    // Opens .rhp packages into temporary project contexts.
    OpenFunction m_open_function;

    // Imports song sources into temporary unsaved project contexts.
    ImportFunction m_import_function;

    // Saves the current session song to the current destination.
    SaveFunction m_save_function;

    // Saves the current session song to a chosen destination.
    SaveAsFunction m_save_as_function;

    // Publishes the current session song to a native song package destination.
    PublishFunction m_publish_function;

    // Requests application exit from the composition host.
    ExitFunction m_exit_function;

    // Optional app-local settings used to restore startup state and persist exit state.
    EditorSettings* m_settings;

    // Non-owning view binding installed by attachView(); null before the first attachment.
    IEditorView* m_view{nullptr};

    // Most recently derived view state used as the seed push at view attachment.
    EditorViewState m_last_state{};

    // Runtime plugin chain shown by the view until durable tone persistence is introduced.
    std::vector<PluginViewState> m_plugins;

    // Set true while a session load is in flight so reentrant transport callbacks defer pushing.
    bool m_session_load_in_progress{false};

    // Currently loaded or imported project context; keeps workspace files alive.
    std::optional<Project> m_project{};

    // User-selected editor project path used for project-name-derived UI suggestions.
    std::filesystem::path m_project_file{};

    // True when Save must first collect an editor project package path, such as after import.
    bool m_save_requires_destination{false};

    // True once current session changes need to be saved or discarded before replacement.
    bool m_has_unsaved_changes{false};

    // Project command waiting for either unsaved-change confirmation or a prompted Save As path.
    std::optional<ProjectCommand> m_pending_project_command{};

    // True while the view should present an unsaved-changes prompt.
    bool m_unsaved_changes_prompt_visible{false};

    // True while the view should present a Save As chooser for a deferred command.
    bool m_save_as_prompt_visible{false};

    // Active busy state pushed to the view; empty while no slow operation is in flight.
    std::optional<BusyViewState> m_busy{};

    // Monotonic token incremented at every beginBusy(). Operations capture this value at start
    // and compare against the live generation on completion so stale completions can be
    // discarded without disturbing whichever operation superseded them.
    std::uint64_t m_busy_generation{0};

    // Liveness flag reset by the controller destructor so background task completions running
    // after teardown can detect that the controller is gone and skip touching dangling state.
    // Each submit captures a weak_ptr to this and checks it before doing anything else.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // Fallback task runner used when Services::task_runner is null. Synchronous so headless
    // tests do not start worker threads.
    InlineEditorTaskRunner m_inline_task_runner{};

    // Non-owning pointer to the active task runner. Points at the Services-supplied runner when
    // provided, otherwise at m_inline_task_runner.
    IEditorTaskRunner* m_task_runner{};

    // Declared last so transport callbacks are detached before controller state is destroyed.
    common::audio::ScopedListener<common::audio::ITransport, common::audio::ITransport::Listener>
        m_transport_listener;

    // Optional audio-device-configuration listener registration; null when no backend was provided.
    std::unique_ptr<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>
        m_audio_device_listener;
};

} // namespace rock_hero::editor::core
