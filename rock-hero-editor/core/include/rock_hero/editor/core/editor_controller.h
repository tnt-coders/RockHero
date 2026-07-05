/*!
\file editor_controller.h
\brief Headless editor workflow coordinator backed by core, transport, and audio activation.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/core/session.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/project/project.h>
#include <rock_hero/editor/core/signal_chain/plugin_block_assignment.h>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{
class IAudioDeviceConfiguration;
class ILiveInput;
class ILiveRig;
class IPluginHost;
class ISongAudio;
class ITransport;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::core
{

class IEditorSettings;
class IEditorTaskRunner;
class IEditorView;
class IMessageThreadScheduler;

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

The referenced audio ports must outlive the controller.
*/
class EditorController final : public IEditorController
{
public:
    /*! \brief Worker-reported phase for a project operation in progress. */
    enum class ProjectOperationPhase : std::uint8_t
    {
        /*! \brief Backing-audio normalization analysis has started. */
        AnalyzingBackingAudio,
    };

    /*! \brief Reports coarse progress from project operations back to the controller. */
    using ProjectOperationProgress = std::function<void(ProjectOperationPhase phase)>;

    /*! \brief Opens an editor project package into a project context. */
    using OpenFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path,
        const ProjectOperationProgress& report_progress)>;

    /*! \brief Imports a song source into a project context. */
    using ImportFunction = std::function<std::expected<common::core::Song, ProjectError>(
        Project& project, const std::filesystem::path& path,
        const ProjectOperationProgress& report_progress)>;

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
    \brief Optional project IO operations used by the editor controller.

    Default-constructed functions are replaced with production project IO behavior. Tests can
    replace only the operations they need without relying on positional callback arguments.
    */
    struct ProjectOperations final
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
    };

    /*!
    \brief Required services used by the editor controller.

    Services are long-lived non-audio collaborators supplied by app composition or tests.
    */
    struct Services final
    {
        /*! \brief Settings store used for startup restore and exit persistence. */
        IEditorSettings& settings;

        /*!
        \brief Task runner used for off-thread project IO.

        Production composition supplies the JUCE-backed runner so open/import/save/publish work can
        complete off-thread. Tests supply a deterministic runner for synchronous or deferred
        completion.
        */
        IEditorTaskRunner& task_runner;

        /*! \brief Message-thread scheduler used for busy-presentation ordering. */
        IMessageThreadScheduler& message_thread_scheduler;
    };

    /*!
    \brief Audio ports consumed by the editor controller.

    All editor audio capabilities are required by production composition. Runtime unavailability
    such as a closed device or missing active input route is represented by port state, not by
    omitting a port.
    */
    struct AudioPorts final
    {
        /*! \brief Transport port used for play/pause/stop/seek and coarse listener delivery. */
        common::audio::ITransport& transport;

        /*! \brief Song-audio port used to validate and load arrangement audio. */
        common::audio::ISongAudio& song_audio;

        /*! \brief Audio-device configuration port for hardware input/output routing. */
        common::audio::IAudioDeviceConfiguration& audio_devices;

        /*! \brief Plugin-host port used to mutate the plugin chain. */
        common::audio::IPluginHost& plugin_host;

        /*! \brief Live rig port used to save and restore tone documents. */
        common::audio::ILiveRig& live_rig;

        /*! \brief Live-input port used for monitoring and calibration. */
        common::audio::ILiveInput& live_input;
    };

    /*!
    \brief Builds the controller with default production project IO operations.

    Delegates to the four-argument constructor with a default-constructed ProjectOperations. A
    separate overload avoids a `= {}` default argument, which clang rejects for a nested aggregate
    that carries default member initializers.

    \param audio_ports Required audio ports consumed by controller workflows.
    \param services Required settings and task-runner services.
    \param exit_function Host-exit callback invoked after guarded controller shutdown succeeds.
    */
    explicit EditorController(
        AudioPorts audio_ports, Services services, ExitFunction exit_function);

    /*!
    \brief Builds the controller, subscribes to transport, and captures initial view state.

    The owned session starts empty until the user opens a project. The controller does not push
    state during construction because no view is attached yet. The initial cached state becomes the
    first push delivered to attachView().

    \param audio_ports Required audio ports consumed by controller workflows.
    \param services Required settings and task-runner services.
    \param exit_function Host-exit callback invoked after guarded controller shutdown succeeds.
    \param project_operations Project IO operation overrides.
    */
    explicit EditorController(
        AudioPorts audio_ports, Services services, ExitFunction exit_function,
        ProjectOperations project_operations);

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

    /*! \brief Detaches any bound view and releases view-capturing presentation callbacks. */
    void detachView();

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

    /*! \brief Handles a request to cancel the active cancellable busy operation. */
    void onBusyCancelRequested() override;

    /*! \brief Handles a request to undo the most recent editor history entry. */
    void onUndoRequested() override;

    /*! \brief Handles a request to redo the next editor history entry. */
    void onRedoRequested() override;

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
    \brief Handles a decision from the interrupted startup restore prompt.
    \param decision Decision selected by the user.
    */
    void onRestoreInterruptedDecision(RestoreInterruptedDecision decision) override;

    /*!
    \brief Handles a play/pause button press from the editor UI.

    The intent is ignored when no arrangement is loaded. Otherwise, plays or pauses
    based on the current transport state.
    */
    void onPlayPausePressed() override;

    /*!
    \brief Handles a stop button press from the editor UI.

    The intent is ignored when the transport is not currently playing and is already at the start
    of the loaded timeline, mirroring the published EditorViewState transport stop gate.
    */
    void onStopPressed() override;

    /*!
    \brief Handles a timeline seek request at an absolute timeline position.

    The position is clamped into the session timeline range before seeking, so out-of-range view
    intents cannot move the cursor outside the loaded content. A timeline with no duration seeks
    to its start.

    \param position Requested seek position on the song timeline.
    */
    void onTimelineSeekRequested(common::core::TimePosition position) override;

    /*!
    \brief Handles a request to change the timeline grid note value.

    Note values outside the supported bounds are ignored. Accepted values are published in view
    state for the grid, ruler, and snapping, and persisted as app-local per-project editor state.

    \param note_value Grid step as a fraction of a whole note.
    */
    void onGridNoteValueChangeRequested(common::core::Fraction note_value) override;

    /*! \brief Shows the scanned plugin browser and starts an initial catalog scan when needed. */
    void onPluginBrowserRequested() override;

    /*!
    \brief Handles selection of a signal-chain slot where a plugin may be inserted.
    \param chain_index User-visible insertion slot while the current chain has capacity.
    \param block_index Fixed visual block the inserted plugin should occupy.
    */
    void onPluginInsertSlotSelected(std::size_t chain_index, std::size_t block_index) override;

    /*! \brief Hides the scanned plugin browser. */
    void onPluginBrowserClosed() override;

    /*! \brief Starts a plugin catalog scan for the browser. */
    void onPluginCatalogScanRequested() override;

    /*!
    \brief Inserts the selected browser plugin into the runtime plugin chain.
    \param plugin_id Opaque plugin ID selected by the user.
    */
    void onSelectedPluginInsertRequested(std::string plugin_id) override;

    /*!
    \brief Removes a plugin instance from the current runtime plugin chain.

    Removal marks the project dirty when a persistent live rig port is available.

    \param instance_id Opaque plugin instance ID selected by the user.
    */
    void onRemovePluginRequested(std::string instance_id) override;

    /*!
    \brief Moves a plugin instance within the current runtime plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    \param destination_index Final user-visible chain index for the instance.
    \param placement Fixed visual block assignments after the move.
    */
    void onMovePluginRequested(
        std::string instance_id, std::size_t destination_index,
        std::vector<PluginBlockAssignment> placement) override;

    /*!
    \brief Stores the editor-authored visual block placement so it persists with the project.
    \param placement Fixed visual block assignments for current plugin instances.
    */
    void onSignalChainPlacementChanged(std::vector<PluginBlockAssignment> placement) override;

    /*!
    \brief Sets or clears a plugin instance's manual signal-chain display type override.
    \param instance_id Opaque plugin instance ID selected by the user.
    \param display_type Manual display type, or empty to use automatic classification.
    */
    void onPluginDisplayTypeOverrideChanged(
        std::string instance_id, std::optional<PluginDisplayType> display_type) override;

    /*!
    \brief Opens a plugin instance editor window for the current runtime plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    void onOpenPluginRequested(std::string instance_id) override;

    /*! \brief Handles a request to manually calibrate the current input route. */
    void onInputCalibrationRequested() override;

    /*!
    \brief Prepares the live input route for raw input calibration measurement.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() override;

    /*! \brief Stops an active calibration measurement while leaving the prompt open. */
    void onInputCalibrationMeasurementCancelled() override;

    /*!
    \brief Applies and stores a completed input calibration gain.
    \param gain_db Calibrated input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db) override;

    /*!
    \brief Applies and stores a manually entered input calibration gain.
    \param gain_db Input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db) override;

    /*! \brief Handles the calibration prompt closing without a new successful calibration. */
    void onInputCalibrationDismissed() override;

    /*!
    \brief Handles a preview-only output gain change while the user is dragging the slider.
    \param gain_db Desired output gain in decibels.
    */
    void onOutputGainPreviewChanged(double gain_db) override;

    /*!
    \brief Handles a committed change to the output gain slider.
    \param gain_db Desired output gain in decibels.
    */
    void onOutputGainChanged(double gain_db) override;

    /*!
    \brief Schedules audio-device open work behind the editor's busy overlay.
    \param change_audio_device Callable run after the busy overlay paints.
    \param after_busy_cleared Callable run after the busy overlay clears.
    */
    void onAudioDeviceChangeRequested(
        std::function<void()> change_audio_device,
        std::function<void()> after_busy_cleared) override;

    /*!
    \brief Requests opening the audio-device settings window.
    \return True when the caller may open the window and must later report closure exactly once.
    */
    [[nodiscard]] bool onAudioDeviceSettingsOpenRequested() override;

    /*! \brief Handles the audio-device settings window closing. */
    void onAudioDeviceSettingsClosed() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::editor::core
