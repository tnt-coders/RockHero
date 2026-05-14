/*!
\file editor_controller.h
\brief Headless editor workflow coordinator backed by core, transport, and audio activation.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/scoped_listener.h>
#include <rock_hero/common/core/session.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <rock_hero/editor/core/project.h>
#include <string>

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

The referenced transport and audio ports must outlive the controller.
*/
class EditorController final : public IEditorController, private common::audio::ITransport::Listener
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
    };

    /*!
    \brief Builds the controller, subscribes to transport, and captures initial view state.

    The owned session starts empty until the user opens a project. The controller does not push
    state during construction because no view is attached yet. The initial cached state becomes the
    first push delivered to attachView().

    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param audio Audio port used to validate and load arrangement audio.
    \param services Optional project IO, settings, and host-exit services.
    */
    EditorController(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
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

private:
    // Supplies a named default-argument target after Services has been declared.
    [[nodiscard]] static Services defaultServices();

    struct PendingProjectRequest
    {
        PendingProjectAction action{PendingProjectAction::Close};
        std::filesystem::path file;
    };

    // Transport listener entry point; receives only coarse transition-shaped callbacks.
    void onTransportStateChanged(common::audio::TransportState state) override;

    // Requests a project-level action, prompting first when unsaved changes are present.
    void requestProjectAction(PendingProjectRequest request);

    // Runs a project-level action after prompts and save requirements are satisfied.
    void performProjectAction(const PendingProjectRequest& request);

    // Opens an editor project package without first checking unsaved-change state.
    void openProject(const std::filesystem::path& file);

    // Imports a song source without first checking unsaved-change state.
    void importSongSource(const std::filesystem::path& file);

    // Closes the current project context, session, and backend audio state.
    [[nodiscard]] bool closeProject();

    // Saves to the current destination and updates dirty tracking on success.
    [[nodiscard]] bool saveProject();

    // Continues the stored pending action after a successful prompted save.
    void continuePendingProjectAction();

    // Clears any in-progress unsaved-change or Save As prompt.
    void clearPendingProjectAction() noexcept;

    // Builds the editor-only project state persisted by Save and Save As.
    [[nodiscard]] ProjectEditorState projectEditorStateForSave() const;

    // Prepares project audio, activates the selected arrangement, and commits to Session.
    [[nodiscard]] bool loadSessionSong(
        common::core::Song song, const std::optional<std::string>& selected_arrangement);

    // Builds a fresh EditorViewState from the current session and transport state.
    [[nodiscard]] EditorViewState deriveViewState() const;

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

    // Action waiting for either unsaved-change confirmation or a prompted Save As path.
    std::optional<PendingProjectRequest> m_pending_project_action{};

    // True while the view should present an unsaved-changes prompt.
    bool m_unsaved_changes_prompt_visible{false};

    // True while the view should present a Save As chooser for a pending action.
    bool m_save_as_prompt_visible{false};

    // Declared last so transport callbacks are detached before controller state is destroyed.
    common::audio::ScopedListener<common::audio::ITransport, common::audio::ITransport::Listener>
        m_transport_listener;
};

} // namespace rock_hero::editor::core
