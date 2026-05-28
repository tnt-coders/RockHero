#include "editor_controller.h"

#include "audio_device_status_text.h"
#include "editor_action.h"
#include "inline_editor_task_runner.h"
#include "project_io.h"
#include "psarc_song_importer.h"
#include "rock_song_importer.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <optional>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_song_audio.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/scoped_listener.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Production open path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultOpen(
    Project& project, const std::filesystem::path& file,
    const AudioAnalyzeForGainFunction& analyze_audio)
{
    return project.load(file, {}, analyze_audio);
}

// Production import path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultImport(
    Project& project, const std::filesystem::path& file,
    const AudioAnalyzeForGainFunction& analyze_audio)
{
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension == ".rock")
    {
        RockSongImporter importer;
        return project.import(file, importer, {}, analyze_audio);
    }

    if (extension == ".psarc")
    {
        PsarcSongImporter importer;
        return project.import(file, importer, {}, analyze_audio);
    }

    return std::unexpected{ProjectError{
        ProjectErrorCode::SongImportFailed,
        "Unsupported song source extension: " + file.extension().string()
    }};
}

// Production save path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultSave(
    Project& project, const common::core::Song& song, ProjectEditorState editor_state)
{
    return project.save(song, std::move(editor_state));
}

// Production save-as path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultSaveAs(
    Project& project, const std::filesystem::path& file, const common::core::Song& song,
    ProjectEditorState editor_state)
{
    return project.saveAs(file, song, std::move(editor_state));
}

// Production publish path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultPublish(
    Project& project, const std::filesystem::path& file, const common::core::Song& song)
{
    return project.publish(file, song);
}

// Closes an optional project after the caller has released subsystem references into its workspace.
[[nodiscard]] std::expected<void, ProjectError> closeExistingProject(
    std::optional<Project>& project)
{
    if (!project.has_value())
    {
        return {};
    }

    return project->close();
}

// Production exit fallback used when a composition host does not provide an exit callback.
void defaultExit()
{}

// Uses the real filesystem to avoid adding another startup-restore callback seam.
[[nodiscard]] bool projectFileExists(const std::filesystem::path& project_file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(project_file, error);
}

// Resolves the extracted native-song directory owned by an editor project workspace.
[[nodiscard]] std::filesystem::path songDirectoryForProject(const Project& project)
{
    return project.workspaceDirectory() /
           std::filesystem::path{std::string{project_io::g_song_directory_name}};
}

// Resolves persisted arrangement IDs to the current song order, falling back to the first item.
[[nodiscard]] std::size_t getSelectedArrangementIndex(
    const common::core::Song& song, const std::optional<std::string>& selected_arrangement)
{
    if (!selected_arrangement.has_value())
    {
        return 0;
    }

    const auto found = std::ranges::find_if(
        song.arrangements, [&selected_arrangement](const common::core::Arrangement& arrangement) {
            return arrangement.id == *selected_arrangement;
        });
    if (found == song.arrangements.end())
    {
        return 0;
    }

    return static_cast<std::size_t>(std::distance(song.arrangements.begin(), found));
}

// Converts scanner metadata into stable, framework-free state for the signal-chain panel.
[[nodiscard]] PluginViewState makePluginViewState(
    const common::audio::PluginCandidate& plugin_candidate,
    const common::audio::PluginHandle& handle)
{
    return PluginViewState{
        .instance_id = handle.instance_id,
        .plugin_id = handle.plugin_id,
        .name = plugin_candidate.name,
        .manufacturer = plugin_candidate.manufacturer,
        .format_name = plugin_candidate.format_name,
        .chain_index = handle.chain_index,
    };
}

// Keeps the browser catalog stable and readable after any scan source updates it.
void sortPluginCatalog(std::vector<common::audio::PluginCandidate>& plugin_candidates)
{
    std::ranges::sort(
        plugin_candidates,
        [](const common::audio::PluginCandidate& lhs, const common::audio::PluginCandidate& rhs) {
            if (lhs.name != rhs.name)
            {
                return lhs.name < rhs.name;
            }
            if (lhs.manufacturer != rhs.manufacturer)
            {
                return lhs.manufacturer < rhs.manufacturer;
            }
            return lhs.id < rhs.id;
        });
}

// Lifts audio-boundary plugin metadata into editor-core workflow state. The conversion is the
// single seam between common::audio::PluginCandidate and editor-core, so backend-shaped fields
// added to the audio-boundary type cannot reach editor-ui without going through this helper.
[[nodiscard]] PluginCandidateViewState makePluginCandidateViewState(
    const common::audio::PluginCandidate& plugin_candidate)
{
    return PluginCandidateViewState{
        .id = plugin_candidate.id,
        .name = plugin_candidate.name,
        .manufacturer = plugin_candidate.manufacturer,
        .format_name = plugin_candidate.format_name,
        .file_path = plugin_candidate.file_path,
    };
}

// Lifts the controller's in-memory catalog into editor-core workflow state for the view.
[[nodiscard]] std::vector<PluginCandidateViewState> makePluginCandidateViewStates(
    const std::vector<common::audio::PluginCandidate>& plugin_candidates)
{
    std::vector<PluginCandidateViewState> states;
    states.reserve(plugin_candidates.size());
    for (const common::audio::PluginCandidate& plugin_candidate : plugin_candidates)
    {
        states.push_back(makePluginCandidateViewState(plugin_candidate));
    }
    return states;
}

// Converts restored or captured live rig state into the signal-chain panel's view model.
[[nodiscard]] PluginViewState makePluginViewState(const common::audio::LiveRigPlugin& plugin)
{
    return PluginViewState{
        .instance_id = plugin.instance_id,
        .plugin_id = plugin.plugin_id,
        .name = plugin.name,
        .manufacturer = plugin.manufacturer,
        .format_name = plugin.format_name,
        .chain_index = plugin.chain_index,
    };
}

// Converts a full live rig result vector into editor view state in one place.
[[nodiscard]] std::vector<PluginViewState> makePluginViewStates(
    const std::vector<common::audio::LiveRigPlugin>& plugins)
{
    std::vector<PluginViewState> states;
    states.reserve(plugins.size());
    for (const common::audio::LiveRigPlugin& plugin : plugins)
    {
        states.push_back(makePluginViewState(plugin));
    }

    return states;
}

// Maps write actions to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(EditorAction::Id action) noexcept
{
    switch (action)
    {
        case EditorAction::Id::SaveProject:
        {
            return BusyOperation::SavingProject;
        }
        case EditorAction::Id::SaveProjectAs:
        {
            return BusyOperation::SavingProjectAs;
        }
        case EditorAction::Id::PublishProject:
        {
            return BusyOperation::PublishingProject;
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::CloseProject:
        case EditorAction::Id::ExitApplication:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekWaveform:
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::ScanPluginCatalog:
        case EditorAction::Id::AddPlugin:
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::OpenPlugin:
        {
            assert(false);
            return BusyOperation::SavingProject;
        }
    }

    assert(false);
    return BusyOperation::SavingProject;
}

// Keeps write failure prefixes coupled to the action identity rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(EditorAction::Id action) noexcept
{
    switch (action)
    {
        case EditorAction::Id::SaveProject:
        {
            return "Could not save: ";
        }
        case EditorAction::Id::SaveProjectAs:
        {
            return "Could not save as: ";
        }
        case EditorAction::Id::PublishProject:
        {
            return "Could not publish: ";
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::CloseProject:
        case EditorAction::Id::ExitApplication:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekWaveform:
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::ScanPluginCatalog:
        case EditorAction::Id::AddPlugin:
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::OpenPlugin:
        {
            assert(false);
            return "Could not write project: ";
        }
    }

    assert(false);
    return "Could not write project: ";
}

// Converts plugin-level live rig progress into the percentage value consumed by BusyOverlay.
[[nodiscard]] double liveRigProgressFraction(
    const common::audio::LiveRigLoadProgress& progress) noexcept
{
    if (progress.total_plugins == 0)
    {
        return 1.0;
    }

    const auto completed =
        static_cast<double>(std::min(progress.completed_plugins, progress.total_plugins));
    return completed / static_cast<double>(progress.total_plugins);
}

// Builds clear progress text for the currently restoring plugin.
[[nodiscard]] std::string liveRigProgressMessage(const common::audio::LiveRigLoadProgress& progress)
{
    if (progress.total_plugins == 0 || progress.active_plugin_name.empty())
    {
        return busyMessage(BusyOperation::LoadingLiveRig);
    }

    const std::size_t display_index =
        std::min(progress.active_plugin_index + 1, progress.total_plugins);
    return "Loading " + progress.active_plugin_name + " (" + std::to_string(display_index) +
           " of " + std::to_string(progress.total_plugins) + ")...";
}

// Lets a worker wait briefly for a message-thread busy-state paint without ever blocking the
// message thread itself. The timeout prevents a hidden, minimized, or tearing-down view from
// turning a cosmetic paint fence into a stuck project load.
class AnalysisPaintGate final
{
public:
    void release()
    {
        {
            const std::lock_guard lock{m_mutex};
            m_released = true;
        }
        m_condition.notify_one();
    }

    void wait()
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait_for(lock, std::chrono::milliseconds{250}, [this] { return m_released; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_released{false};
};

} // namespace

// Owns every implementation detail that does not need to be part of the public controller type.
struct EditorController::Impl final : private common::audio::ITransport::Listener,
                                      private common::audio::IAudioDeviceConfiguration::Listener
{
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

    struct OpenTaskState;
    struct ImportTaskState;
    struct AddPluginTaskState;
    struct PluginCatalogTaskState;
    struct ProjectWriteTaskState;
    struct ProjectLoadLiveRigStage;

    Impl(
        common::audio::ITransport& transport, common::audio::ISongAudio& song_audio,
        common::audio::IAudioDeviceConfiguration& audio_devices,
        common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
        common::audio::ILiveInput& live_input, EditorController::Services services);
    ~Impl() override;

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void attachView(IEditorView& view);
    [[nodiscard]] const common::core::Session& session() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> currentProjectFile() const;
    void restoreLastOpenProject();
    void onOpenRequested(std::filesystem::path file);
    void onImportRequested(std::filesystem::path file);
    void onSaveRequested();
    void onSaveAsRequested(std::filesystem::path file);
    void onPublishRequested(std::filesystem::path file);
    void onSaveAsCancelled();
    void onCloseRequested();
    void onExitRequested();
    void onUnsavedChangesDecision(UnsavedChangesDecision decision);
    void onRestoreInterruptedDecision(RestoreInterruptedDecision decision);
    void onPlayPausePressed();
    void onStopPressed();
    void onWaveformClicked(double normalized_x);
    void onPluginBrowserRequested();
    void onPluginBrowserClosed();
    void onPluginCatalogScanRequested();
    void onAddPluginRequested(std::string plugin_id);
    void onRemovePluginRequested(std::string instance_id);
    void onOpenPluginRequested(std::string instance_id);
    void onInputCalibrationRequested();
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted();
    void onInputCalibrationMeasurementCancelled();
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db);
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db);
    void onInputCalibrationDismissed();
    void onOutputGainChanged(double gain_db);
    void onAudioDeviceChangeRequested(std::function<void()> change_audio_device);
    void onAudioDeviceSettingsOpened();
    void onAudioDeviceSettingsClosed();
    void onTransportStateChanged(common::audio::TransportState state) override;
    void onAudioDeviceConfigurationChanged() override;

    void runAction(EditorAction::Action action);
    [[nodiscard]] bool prepareAction(EditorAction::Id action);
    void performAction(EditorAction::Action action);
    void performActionImpl(EditorAction::OpenProject action);
    void performActionImpl(EditorAction::RestoreProject action);
    void performActionImpl(EditorAction::ImportSong action);
    void performActionImpl(EditorAction::SaveProject action);
    void performActionImpl(EditorAction::SaveProjectAs action);
    void performActionImpl(EditorAction::PublishProject action);
    void performActionImpl(EditorAction::CloseProject action);
    void performActionImpl(EditorAction::ExitApplication action);
    void performActionImpl(EditorAction::ResolveUnsavedChangesPrompt action);
    void performActionImpl(EditorAction::CancelSaveAsPrompt action);
    void performActionImpl(EditorAction::PlayPause action);
    void performActionImpl(EditorAction::Stop action);
    void performActionImpl(EditorAction::SeekWaveform action);
    void performActionImpl(EditorAction::ShowPluginBrowser action);
    void performActionImpl(EditorAction::ScanPluginCatalog action);
    void performActionImpl(const EditorAction::AddPlugin& action);
    void performActionImpl(const EditorAction::RemovePlugin& action);
    void performActionImpl(const EditorAction::OpenPlugin& action);
    [[nodiscard]] bool canRunAction(EditorAction::Id action) const;
    [[nodiscard]] bool actionAvailableWhenIdle(EditorAction::Id action) const;
    [[nodiscard]] static ActionBusyPolicy actionBusyPolicy(EditorAction::Id action) noexcept;

    void requestProjectAction(EditorAction::ProjectAction action);
    void runProjectAction(EditorAction::ProjectAction action);
    void openProject(const std::filesystem::path& file, bool clear_last_open_project_on_failure);
    void completeOpenProject(const std::shared_ptr<OpenTaskState>& state);
    void finishOpenProjectAfterLiveRigLoad(
        const std::shared_ptr<OpenTaskState>& state, const ProjectEditorState& editor_state,
        std::expected<void, std::string> rig_result);
    void importSongSource(const std::filesystem::path& file);
    void completeImportSongSource(const std::shared_ptr<ImportTaskState>& state);
    void finishImportSongSourceAfterLiveRigLoad(
        const std::shared_ptr<ImportTaskState>& state, std::expected<void, std::string> rig_result);
    [[nodiscard]] AudioAnalyzeForGainFunction makeBusyAudioAnalyzeForGainFunction(
        std::uint64_t token);
    void completeAddPluginLoad(const std::shared_ptr<AddPluginTaskState>& state);
    void beginAddKnownPlugin(const common::audio::PluginCandidate& plugin_candidate);
    void completePluginCatalogScan(const std::shared_ptr<PluginCatalogTaskState>& state);
    void refreshKnownPluginCatalog();
    [[nodiscard]] bool closeProject();
    [[nodiscard]] std::shared_ptr<ProjectWriteTaskState> takeProjectForWrite(
        EditorAction::ProjectWriteAction action);
    [[nodiscard]] std::expected<void, std::string> captureLiveRigIntoSong(
        common::core::Song& song, const Project& project);
    void runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state);
    void startLiveRigLoadStage(ProjectLoadLiveRigStage stage_state, bool report_progress);
    void restoreLiveRig(
        const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
        std::function<void(std::expected<common::audio::LiveRigLoadResult, std::string>)>
            on_loaded);
    void clearLiveRig();
    void runProjectWriteAction(EditorAction::ProjectWriteAction&& action);
    void completeProjectWriteAction(const std::shared_ptr<ProjectWriteTaskState>& state);
    void continueDeferredAction();
    void clearDeferredAction() noexcept;
    void clearInterruptedRestoreMarker();
    void clearLastOpenProjectIfMatches(const std::filesystem::path& project_file);
    [[nodiscard]] ProjectEditorState projectEditorStateForSave() const;
    [[nodiscard]] std::optional<std::filesystem::path> restorableProjectFileForExit() const;
    [[nodiscard]] bool loadSessionSong(
        common::core::Song song, const std::optional<std::string>& selected_arrangement);
    [[nodiscard]] EditorViewState deriveViewState() const;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] std::uint64_t beginBusy(BusyOperation operation);
    void transitionBusyOperation(BusyOperation operation, std::uint64_t token);
    void transitionBusyOperationAfterPaint(
        BusyOperation operation, std::uint64_t token, std::shared_ptr<AnalysisPaintGate> gate);
    void finishBusyOperation();
    void supersedeBusyOperation();
    void endBusy();
    void setLiveRigLoadProgress(std::string message, double fraction);
    void beginLiveRigLoadProgress();
    void updateLiveRigLoadProgress(const common::audio::LiveRigLoadProgress& progress);
    void runAfterBusyOverlayPainted(std::function<void()>&& callback);
    void restoreAudioDeviceState();
    void persistAudioDeviceState();
    void loadInputCalibrationFromSettings();
    void persistInputCalibration();
    void clearInputCalibration();
    void syncCommittedInputDeviceIdentity();
    [[nodiscard]] std::optional<common::audio::InputDeviceIdentity> currentInputDeviceIdentity()
        const;
    [[nodiscard]] bool inputCalibrationMatchesCurrentInput() const;
    [[nodiscard]] double inputCalibrationPromptGainDb() const;
    [[nodiscard]] bool liveInputAuditionAvailable() const;
    [[nodiscard]] InputCalibrationStatus inputCalibrationStatus() const;
    [[nodiscard]] std::string inputCalibrationDisabledMessage() const;
    void applyLiveInputGate();
    // Snapshot of live-input routing values used to roll back failed calibration setup.
    struct InputCalibrationRouteState
    {
        common::audio::Gain input_gain;
        bool live_input_monitoring_enabled{false};
        bool calibration_input_monitoring_enabled{false};
    };
    [[nodiscard]] InputCalibrationRouteState currentInputCalibrationRouteState() const;
    void restoreInputCalibrationRouteStateBestEffort(const InputCalibrationRouteState& route_state);
    void closeInputCalibrationPrompt();
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> commitInputCalibration(
        double gain_db, std::optional<common::audio::InputDeviceIdentity> expected_identity);
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    restoreCalibrationMeasurementState();
    void updateView();
    void reportError(const std::string& message);

    // Wraps an async callback with a liveness guard against this Impl. Captures a weak_ptr to
    // m_alive at the call site; the returned callable checks expiry before invoking the
    // wrapped callback. Use this for any callback that may fire from the task runner / timer
    // / external scheduler after Impl is destroyed. For task-runner-driven busy operations,
    // use runBusyOperation(), which composes the liveness check with token validation.
    template <typename Callback> auto safeCallback(Callback&& callback)
    {
        return [wrapped_callback = std::forward<Callback>(callback),
                alive = std::weak_ptr<bool>{m_alive}](auto&&... args) mutable {
            if (alive.expired())
            {
                return;
            }
            wrapped_callback(std::forward<decltype(args)>(args)...);
        };
    }

    // Begins a busy operation, pushes the resulting view state, and submits a worker+completion
    // pair to the task runner. The completion is wrapped so it runs only if (a) this Impl is
    // still alive AND (b) the busy token captured at submission still matches the current
    // token. Stale completions from superseded or interrupted operations are dropped silently.
    // The completion receives the shared state pointer only, because the helper already
    // guarantees that m_current_busy_token still matches at the call site.
    template <typename TaskState, typename Worker, typename Completion>
    void runBusyOperation(
        BusyOperation operation, const std::shared_ptr<TaskState>& state, Worker&& worker,
        Completion&& completion)
    {
        const std::uint64_t token = beginBusy(operation);
        updateView();
        m_task_runner->submit(
            [state, captured_worker = std::forward<Worker>(worker)] { captured_worker(state); },
            safeCallback([this,
                          state,
                          token,
                          captured_completion = std::forward<Completion>(completion)]() mutable {
                if (token != m_current_busy_token)
                {
                    return;
                }
                captured_completion(state);
            }));
    }
    [[nodiscard]] bool hasLoadedArrangement() const;
    [[nodiscard]] bool shouldShowLiveRigLoadProgress() const;
    [[nodiscard]] bool hasLiveRigPersistence() const noexcept;
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;
    [[nodiscard]] bool canStopTransport(const common::audio::TransportState& transport_state) const;

    // Transport port used for control intents and coarse listener delivery.
    common::audio::ITransport& m_transport;

    // Song-audio port used for project audio validation and selected-arrangement loading.
    common::audio::ISongAudio& m_song_audio;

    // Audio-device port used for ASIO input/output routing.
    common::audio::IAudioDeviceConfiguration& m_audio_devices;

    // Plugin-host port used to mutate the processing chain.
    common::audio::IPluginHost& m_plugin_host;

    // Live rig port used to persist and restore arrangement-owned plugin state.
    common::audio::ILiveRig& m_live_rig;

    // Live input port used to apply app-local calibration and gate monitoring.
    common::audio::ILiveInput& m_live_input;

    // Song aggregate and selected arrangement state currently loaded in the editor.
    common::core::Session m_session;

    // Project IO and host-exit seams supplied by production composition or tests.
    EditorController::OpenFunction m_open_function;
    EditorController::ImportFunction m_import_function;
    AudioAnalyzeForGainFunction m_audio_analyze_for_gain_function;
    EditorController::SaveFunction m_save_function;
    EditorController::SaveAsFunction m_save_as_function;
    EditorController::PublishFunction m_publish_function;
    EditorController::ExitFunction m_exit_function;

    // Optional app-local settings used to restore startup state and persist exit state.
    EditorSettings* m_settings;

    // Non-owning view binding installed by attachView(); null before the first attachment.
    // updateView() and reportError() tolerate the null window because the constructor's
    // restoreAudioDeviceState() can synchronously fire onAudioDeviceConfigurationChanged()
    // before the host wires up a view. runAfterBusyOverlayPainted() does not, because its
    // callers are all downstream of user UI intents that run only after attachment.
    IEditorView* m_view{nullptr};

    // Most recently derived view state used as the seed push at view attachment.
    EditorViewState m_last_state{};

    // Runtime plugin chain shown by the view and refreshed from the live rig boundary.
    std::vector<PluginViewState> m_plugins;

    // Current output gain shown by the signal-chain panel and persisted in tone documents.
    double m_output_gain_db{0.0};

    // True only after arrangement audio and the live rig restore have both committed.
    bool m_project_audio_ready{false};

    // Calibration record loaded from app-local settings or written by a successful calibration.
    std::optional<common::audio::InputCalibrationState> m_input_calibration_state{};

    // Last committed active input route. Changes clear calibration.
    std::optional<common::audio::InputDeviceIdentity> m_committed_input_device_identity{};

    // True while the manual calibration prompt should be visible.
    bool m_input_calibration_prompt_visible{false};

    // True while the audio-device settings window is open.
    bool m_audio_device_settings_window_open{false};

    // Active calibration attempt metadata used to restore a previous calibration on dismissal.
    struct ActiveInputCalibration
    {
        common::audio::InputDeviceIdentity input_device_identity;
        std::optional<common::audio::InputCalibrationState> previous_calibration_state;
    };

    // Current automatic calibration attempt, if the popup has started one.
    std::optional<ActiveInputCalibration> m_active_input_calibration{};

    // Plugins shown in the browser and selected by opaque plugin ID.
    std::vector<common::audio::PluginCandidate> m_plugin_catalog;

    // True while the plugin browser should be visible.
    bool m_plugin_browser_visible{false};

    // Set true while a session load is in flight so reentrant transport callbacks defer pushing.
    bool m_session_load_in_progress{false};

    // Currently loaded or imported project context; keeps workspace files alive.
    std::optional<Project> m_project{};

    // User-selected editor project path used for project-name-derived UI suggestions.
    std::filesystem::path m_project_file{};

    // Settings-backed restore target currently being opened. Used only so app shutdown during
    // startup restore preserves the previous launch's restorable path until open commits or fails.
    std::optional<std::filesystem::path> m_pending_restore_project_file{};

    // Settings-backed restore target that should ask for user recovery input on this launch.
    std::optional<std::filesystem::path> m_restore_interrupted_prompt_file{};

    // True when Save must first collect an editor project package path, such as after import.
    bool m_save_requires_destination{false};

    // Project file that was open before an import replaced the session. Populated by the import
    // commit or by a discard-confirmed import that first closes a dirty saved project, so
    // close-with-discard can re-open the displaced project instead of leaving the editor empty.
    // Cleared on successful save, explicit open, or close of a saved project.
    std::filesystem::path m_displaced_project_file{};

    // True once current session changes need to be saved or discarded before replacement.
    bool m_has_unsaved_changes{false};

    // Project-lifecycle action waiting for either unsaved-change confirmation or a Save As path.
    std::optional<EditorAction::ProjectAction> m_deferred_action{};

    // True while the view should present an unsaved-changes prompt.
    bool m_unsaved_changes_prompt_visible{false};

    // True while the view should present a Save As chooser for a deferred command.
    bool m_save_as_prompt_visible{false};

    // Per-operation progress payload for LoadingLiveRig. Held separately from m_busy_operation
    // so the truth (what kind of work is in flight) is independent from per-op presentational
    // data (message override, completion fraction).
    struct LiveRigProgress
    {
        std::string message;
        double fraction;
    };

    // Identifies the current in-flight operation, if any. Empty while no slow operation runs.
    // BusyViewState shown to the view is derived from this field plus m_live_rig_progress.
    // No presentational state is stored here.
    std::optional<BusyOperation> m_busy_operation{};

    // Optional live-rig progress payload, populated only while LoadingLiveRig is reporting
    // progress. Cleared when busy state ends.
    std::optional<LiveRigProgress> m_live_rig_progress{};

    // Current busy-operation token used by async callbacks to reject stale work.
    std::uint64_t m_current_busy_token{0};

    // Reset during destruction so queued completions can detect that the controller is gone.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // Fallback task runner used when Services::task_runner is null.
    InlineEditorTaskRunner m_inline_task_runner{};

    // Non-owning pointer to the active task runner.
    IEditorTaskRunner* m_task_runner{};

    // Declared near the end so callback registration is detached before controller state dies.
    common::audio::ScopedListener<common::audio::ITransport, common::audio::ITransport::Listener>
        m_transport_listener;

    // Optional audio-device-configuration listener registration.
    std::unique_ptr<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>
        m_audio_device_listener;
};

// Per-operation worker state for openProject(). The worker thread writes the result; the
// message-thread completion reads it. Held through a shared_ptr captured by both lambdas so the
// Project's workspace files stay alive until either side is done with them.
struct EditorController::Impl::OpenTaskState
{
    std::filesystem::path file{};
    bool clear_last_open_project_on_failure{false};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Per-operation worker state for importSongSource(). Same shared_ptr ownership pattern as
// OpenTaskState.
struct EditorController::Impl::ImportTaskState
{
    std::filesystem::path file{};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Per-operation state for selected browser-plugin loading. Actual chain mutation happens on
// the message thread after the busy overlay has painted because Tracktion requires it.
struct EditorController::Impl::AddPluginTaskState
{
    common::audio::PluginCandidate plugin_candidate{};
};

// Per-operation worker state for plugin catalog scanning. The worker owns plugin inspection
// through the audio boundary; the completion replaces the browser catalog.
struct EditorController::Impl::PluginCatalogTaskState
{
    std::expected<void, common::audio::PluginHostError> scan_result{};
};

// Per-operation worker state for project package writes. The Project is moved out of the
// controller before worker execution so background save/publish code never shares mutable project
// ownership with message-thread controller actions.
struct EditorController::Impl::ProjectWriteTaskState
{
    explicit ProjectWriteTaskState(EditorAction::ProjectWriteAction action_value)
        : action(std::move(action_value))
    {}

    EditorAction::ProjectWriteAction action;
    Project project{};
    common::core::Song song{};
    ProjectEditorState editor_state{};
    std::expected<void, ProjectError> result{};
};

// Shared message-thread continuation for the live-rig restore stage of project open/import.
// The stage centralizes busy-token checks, paint-fenced progress startup, and routing into the
// final commit callback so open/import do not each hand-roll the same async choreography.
struct EditorController::Impl::ProjectLoadLiveRigStage
{
    std::uint64_t token{};
    std::filesystem::path song_directory{};
    std::function<void(std::expected<void, std::string>)> finish;
};

// Provides a default-argument target after the nested Services type is fully declared.
EditorController::Services EditorController::defaultServices()
{
    return Services{};
}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where a service seam is omitted.
EditorController::EditorController(AudioPorts audio_ports, EditorController::Services services)
    : m_impl(
          std::make_unique<Impl>(
              audio_ports.transport, audio_ports.song_audio, audio_ports.audio_devices,
              audio_ports.plugin_host, audio_ports.live_rig, audio_ports.live_input,
              std::move(services)))
{}

// Releases the pimpl after the public controller's listener callbacks can no longer be invoked.
EditorController::~EditorController() = default;

void EditorController::attachView(IEditorView& view)
{
    m_impl->attachView(view);
}

const common::core::Session& EditorController::session() const noexcept
{
    return m_impl->session();
}

std::optional<std::filesystem::path> EditorController::currentProjectFile() const
{
    return m_impl->currentProjectFile();
}

void EditorController::restoreLastOpenProject()
{
    m_impl->restoreLastOpenProject();
}

void EditorController::onOpenRequested(std::filesystem::path file)
{
    m_impl->onOpenRequested(std::move(file));
}

void EditorController::onImportRequested(std::filesystem::path file)
{
    m_impl->onImportRequested(std::move(file));
}

void EditorController::onSaveRequested()
{
    m_impl->onSaveRequested();
}

void EditorController::onSaveAsRequested(std::filesystem::path file)
{
    m_impl->onSaveAsRequested(std::move(file));
}

void EditorController::onPublishRequested(std::filesystem::path file)
{
    m_impl->onPublishRequested(std::move(file));
}

void EditorController::onSaveAsCancelled()
{
    m_impl->onSaveAsCancelled();
}

void EditorController::onCloseRequested()
{
    m_impl->onCloseRequested();
}

void EditorController::onExitRequested()
{
    m_impl->onExitRequested();
}

void EditorController::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    m_impl->onUnsavedChangesDecision(decision);
}

void EditorController::onRestoreInterruptedDecision(RestoreInterruptedDecision decision)
{
    m_impl->onRestoreInterruptedDecision(decision);
}

void EditorController::onPlayPausePressed()
{
    m_impl->onPlayPausePressed();
}

void EditorController::onStopPressed()
{
    m_impl->onStopPressed();
}

void EditorController::onWaveformClicked(double normalized_x)
{
    m_impl->onWaveformClicked(normalized_x);
}

void EditorController::onPluginBrowserRequested()
{
    m_impl->onPluginBrowserRequested();
}

void EditorController::onPluginBrowserClosed()
{
    m_impl->onPluginBrowserClosed();
}

void EditorController::onPluginCatalogScanRequested()
{
    m_impl->onPluginCatalogScanRequested();
}

void EditorController::onAddPluginRequested(std::string plugin_id)
{
    m_impl->onAddPluginRequested(std::move(plugin_id));
}

void EditorController::onRemovePluginRequested(std::string instance_id)
{
    m_impl->onRemovePluginRequested(std::move(instance_id));
}

void EditorController::onOpenPluginRequested(std::string instance_id)
{
    m_impl->onOpenPluginRequested(std::move(instance_id));
}

void EditorController::onInputCalibrationRequested()
{
    m_impl->onInputCalibrationRequested();
}

std::expected<void, common::audio::LiveInputError> EditorController::
    onInputCalibrationMeasurementStarted()
{
    return m_impl->onInputCalibrationMeasurementStarted();
}

void EditorController::onInputCalibrationMeasurementCancelled()
{
    m_impl->onInputCalibrationMeasurementCancelled();
}

std::expected<void, common::audio::LiveInputError> EditorController::onInputCalibrationSucceeded(
    double gain_db)
{
    return m_impl->onInputCalibrationSucceeded(gain_db);
}

std::expected<void, common::audio::LiveInputError> EditorController::onInputCalibrationManuallySet(
    double gain_db)
{
    return m_impl->onInputCalibrationManuallySet(gain_db);
}

void EditorController::onInputCalibrationDismissed()
{
    m_impl->onInputCalibrationDismissed();
}

void EditorController::onOutputGainChanged(double gain_db)
{
    m_impl->onOutputGainChanged(gain_db);
}

void EditorController::onAudioDeviceChangeRequested(std::function<void()> change_audio_device)
{
    m_impl->onAudioDeviceChangeRequested(std::move(change_audio_device));
}

void EditorController::onAudioDeviceSettingsOpened()
{
    m_impl->onAudioDeviceSettingsOpened();
}

void EditorController::onAudioDeviceSettingsClosed()
{
    m_impl->onAudioDeviceSettingsClosed();
}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where a service seam is omitted.
EditorController::Impl::Impl(
    common::audio::ITransport& transport, common::audio::ISongAudio& song_audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    common::audio::ILiveInput& live_input, EditorController::Services services)
    : m_transport(transport)
    , m_song_audio(song_audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_live_rig(live_rig)
    , m_live_input(live_input)
    , m_open_function(
          services.open_function ? std::move(services.open_function)
                                 : EditorController::OpenFunction{defaultOpen})
    , m_import_function(
          services.import_function ? std::move(services.import_function)
                                   : EditorController::ImportFunction{defaultImport})
    , m_audio_analyze_for_gain_function(
          services.audio_analyze_for_gain_function
              ? std::move(services.audio_analyze_for_gain_function)
              : AudioAnalyzeForGainFunction{common::audio::analyzeAudioForGainNormalization})
    , m_save_function(
          services.save_function ? std::move(services.save_function)
                                 : EditorController::SaveFunction{defaultSave})
    , m_save_as_function(
          services.save_as_function ? std::move(services.save_as_function)
                                    : EditorController::SaveAsFunction{defaultSaveAs})
    , m_publish_function(
          services.publish_function ? std::move(services.publish_function)
                                    : EditorController::PublishFunction{defaultPublish})
    , m_exit_function(
          services.exit_function ? std::move(services.exit_function)
                                 : EditorController::ExitFunction{defaultExit})
    , m_settings(services.settings)
    , m_task_runner(services.task_runner != nullptr ? services.task_runner : &m_inline_task_runner)
    , m_transport_listener(transport, *this)
{
    restoreAudioDeviceState();
    common::audio::IAudioDeviceConfiguration::Listener& self_as_listener = *this;
    m_audio_device_listener = std::make_unique<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>(m_audio_devices, self_as_listener);
    loadInputCalibrationFromSettings();
    m_committed_input_device_identity = currentInputDeviceIdentity();
    if (m_input_calibration_state.has_value() && !inputCalibrationMatchesCurrentInput())
    {
        clearInputCalibration();
    }
    applyLiveInputGate();
    m_last_state = deriveViewState();
}

// Resets the liveness flag first so any background task completion that fires after this point
// sees weak_ptr.expired() and skips touching now-destroyed members. JUCE's single-threaded
// message manager already serializes destruction with completion dispatch, so the window of
// concern is between this destructor returning and the MessageManager itself being torn down.
EditorController::Impl::~Impl()
{
    m_alive.reset();
}

// Stores the new view binding and immediately satisfies the "first push at attachment" contract
// using whatever state the controller has cached up to this point.
void EditorController::Impl::attachView(IEditorView& view)
{
    m_view = &view;
    view.setState(m_last_state);
}

// Opens an editor project package and stores it after audio and Session both accept the song.
void EditorController::Impl::onOpenRequested(std::filesystem::path file)
{
    runAction(EditorAction::OpenProject{std::move(file)});
}

// Opens an editor project package after any project-replacement prompt has been satisfied.
// Pushes busy state, dispatches package IO to the task runner, and returns immediately. The
// final commit runs in completeOpenProject() on the message thread once the worker reports the
// result.
void EditorController::Impl::openProject(
    const std::filesystem::path& file, bool clear_last_open_project_on_failure)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<OpenTaskState>();
    state->file = file;
    state->clear_last_open_project_on_failure = clear_last_open_project_on_failure;
    m_pending_restore_project_file = clear_last_open_project_on_failure
                                         ? std::optional<std::filesystem::path>{file}
                                         : std::nullopt;
    m_restore_interrupted_prompt_file.reset();
    if (m_settings != nullptr)
    {
        if (clear_last_open_project_on_failure)
        {
            m_settings->setInterruptedRestoreProject(file);
        }
        else
        {
            m_settings->setInterruptedRestoreProject(std::nullopt);
        }
    }
    const std::uint64_t token = beginBusy(BusyOperation::OpeningProject);
    AudioAnalyzeForGainFunction analyze_audio = makeBusyAudioAnalyzeForGainFunction(token);
    updateView();

    m_task_runner->submit(
        [state, open_function = m_open_function, analyze_audio = std::move(analyze_audio)] {
            state->result = open_function(state->project, state->file, analyze_audio);
        },
        safeCallback([this, state, token]() mutable {
            if (token != m_current_busy_token)
            {
                return;
            }
            completeOpenProject(state);
        }));
}

// Applies the worker's open result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeOpenProject(const std::shared_ptr<OpenTaskState>& state)
{
    assert(isBusy() && "completeOpenProject called outside a busy operation");

    if (!state->result.has_value())
    {
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);
    ProjectEditorState editor_state = state->project.editorState();

    if (!loadSessionSong(std::move(song), editor_state.selected_arrangement))
    {
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        finishBusyOperation();
        reportError(std::string{"Could not load audio from: "} + state->file.string());
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_current_busy_token,
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state, captured_editor_state = std::move(editor_state)](
                          std::expected<void, std::string> rig_result) {
                finishOpenProjectAfterLiveRigLoad(
                    state, captured_editor_state, std::move(rig_result));
            },
        });
}

// Commits a fully loaded editor project, or tears down the partial session on rig-load failure.
// Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before this
// finalizer runs.
void EditorController::Impl::finishOpenProjectAfterLiveRigLoad(
    const std::shared_ptr<OpenTaskState>& state, const ProjectEditorState& editor_state,
    std::expected<void, std::string> rig_result)
{
    assert(isBusy() && "finishOpenProjectAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        m_song_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        m_output_gain_db = 0.0;
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        finishBusyOperation();
        reportError(
            std::string{"Could not load live rig from: "} + state->file.string() + ": " +
            rig_result.error());
        return;
    }

    m_project = std::move(state->project);
    m_project_file = state->file;
    if (state->clear_last_open_project_on_failure)
    {
        clearInterruptedRestoreMarker();
    }
    m_pending_restore_project_file.reset();
    m_displaced_project_file.clear();
    m_transport.seek(session().timeline().clamp(editor_state.cursor_position));
    m_save_requires_destination = false;
    m_has_unsaved_changes = m_project->audioNormalizationUpdatedOnLoad();
    clearDeferredAction();
    m_project_audio_ready = true;
    applyLiveInputGate();

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Imports a song source and stores the workspace only after audio and Session accept the song.
void EditorController::Impl::onImportRequested(std::filesystem::path file)
{
    runAction(EditorAction::ImportSong{std::move(file)});
}

// Imports a song source after any current project-replacement prompt has been satisfied. Same
// shape as openProject(): busy + worker dispatch here, commit in completeImportSongSource().
void EditorController::Impl::importSongSource(const std::filesystem::path& file)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<ImportTaskState>();
    state->file = file;

    m_pending_restore_project_file.reset();
    m_restore_interrupted_prompt_file.reset();
    clearInterruptedRestoreMarker();
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    AudioAnalyzeForGainFunction analyze_audio = makeBusyAudioAnalyzeForGainFunction(token);
    updateView();

    m_task_runner->submit(
        [state, import_function = m_import_function, analyze_audio = std::move(analyze_audio)] {
            state->result = import_function(state->project, state->file, analyze_audio);
        },
        safeCallback([this, state, token]() mutable {
            if (token != m_current_busy_token)
            {
                return;
            }
            completeImportSongSource(state);
        }));
}

// Applies the worker's import result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeImportSongSource(const std::shared_ptr<ImportTaskState>& state)
{
    assert(isBusy() && "completeImportSongSource called outside a busy operation");

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not import: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    if (!loadSessionSong(std::move(song), std::nullopt))
    {
        finishBusyOperation();
        reportError(std::string{"Could not load imported audio from: "} + state->file.string());
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_current_busy_token,
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state](std::expected<void, std::string> rig_result) {
                finishImportSongSourceAfterLiveRigLoad(state, std::move(rig_result));
            },
        });
}

// Commits a fully imported editor workspace, or tears down the partial session on rig-load
// failure. Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before
// this finalizer runs.
void EditorController::Impl::finishImportSongSourceAfterLiveRigLoad(
    const std::shared_ptr<ImportTaskState>& state, std::expected<void, std::string> rig_result)
{
    assert(isBusy() && "finishImportSongSourceAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        m_song_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        m_output_gain_db = 0.0;
        finishBusyOperation();
        reportError(
            std::string{"Could not load imported live rig from: "} + state->file.string() + ": " +
            rig_result.error());
        return;
    }

    m_displaced_project_file = !m_project_file.empty() ? m_project_file : m_displaced_project_file;
    m_project = std::move(state->project);
    m_project_file.clear();
    m_save_requires_destination = true;
    m_has_unsaved_changes = true;
    clearDeferredAction();
    m_project_audio_ready = true;
    applyLiveInputGate();

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Wraps open/import analysis so the project worker can tell the message thread when it has
// entered the potentially slow LUFS-I phase without moving the analyzer itself onto the UI thread.
AudioAnalyzeForGainFunction EditorController::Impl::makeBusyAudioAnalyzeForGainFunction(
    std::uint64_t token)
{
    return [analyze_audio = m_audio_analyze_for_gain_function,
            alive = std::weak_ptr<bool>{m_alive},
            token,
            controller = this](
               const std::filesystem::path& input,
               const common::core::AudioNormalizationTarget& target) {
        auto publish_analysis_state = [alive, token, controller] {
            if (alive.expired())
            {
                return;
            }
            controller->transitionBusyOperation(BusyOperation::AnalyzingBackingAudio, token);
        };

        juce::MessageManager* const message_manager =
            juce::MessageManager::getInstanceWithoutCreating();
        if (message_manager == nullptr || message_manager->isThisTheMessageThread())
        {
            publish_analysis_state();
        }
        else
        {
            auto paint_gate = std::make_shared<AnalysisPaintGate>();
            const bool posted =
                juce::MessageManager::callAsync([alive, token, controller, paint_gate] {
                    if (alive.expired())
                    {
                        paint_gate->release();
                        return;
                    }
                    controller->transitionBusyOperationAfterPaint(
                        BusyOperation::AnalyzingBackingAudio, token, paint_gate);
                });
            if (!posted)
            {
                paint_gate->release();
            }
            paint_gate->wait();
        }

        return analyze_audio(input, target);
    };
}

// Runs the shared project-load live-rig stage. Tone-bearing arrangements switch the busy overlay
// into determinate progress and wait for that state to paint before live-rig restore starts.
void EditorController::Impl::runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state)
{
    if (!stage_state.finish || stage_state.token != m_current_busy_token)
    {
        return;
    }

    const bool report_progress = shouldShowLiveRigLoadProgress();
    if (!report_progress)
    {
        startLiveRigLoadStage(std::move(stage_state), false);
        return;
    }

    beginLiveRigLoadProgress();
    runAfterBusyOverlayPainted(
        safeCallback([this, captured_stage = std::move(stage_state)]() mutable {
            startLiveRigLoadStage(std::move(captured_stage), true);
        }));
}

// Starts the audio-boundary live-rig restore and routes only current-token completions to the
// stage finalizer. Signal-chain view state is updated only after the token check so a
// superseded restore cannot repopulate plugins after close or replacement.
void EditorController::Impl::startLiveRigLoadStage(
    ProjectLoadLiveRigStage stage_state, bool report_progress)
{
    if (!stage_state.finish || stage_state.token != m_current_busy_token)
    {
        return;
    }

    const std::uint64_t token = stage_state.token;

    // Resolve the directory before moving the stage into the completion lambda. MSVC may evaluate
    // later call arguments before earlier ones, so reading stage_state.song_directory inline would
    // risk reading after move.
    const std::filesystem::path song_directory = stage_state.song_directory;
    restoreLiveRig(
        song_directory,
        report_progress,
        token,
        safeCallback(
            [this, token, report_progress, captured_stage = std::move(stage_state)](
                std::expected<common::audio::LiveRigLoadResult, std::string> rig_result) mutable {
                if (token != m_current_busy_token)
                {
                    return;
                }
                if (!rig_result.has_value())
                {
                    captured_stage.finish(std::unexpected{std::move(rig_result.error())});
                    return;
                }

                m_plugins = makePluginViewStates(rig_result->plugins);
                m_output_gain_db = rig_result->output_gain.db;
                applyLiveInputGate();
                if (!report_progress)
                {
                    captured_stage.finish({});
                    return;
                }

                setLiveRigLoadProgress("Live rig loaded.", 1.0);
                updateView();

                // No message thread in unit tests; finish synchronously so tests don't deadlock.
                if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
                {
                    captured_stage.finish({});
                    return;
                }

                // Wall-clock delay so the 100% state stays visible long enough for the user to see.
                // The delay gives JUCE plenty of message-loop room to paint before the timer fires.
                constexpr std::chrono::milliseconds minimum_completion_display_time{500};
                juce::Timer::callAfterDelay(
                    static_cast<int>(minimum_completion_display_time.count()),
                    safeCallback([this, token, timer_stage = std::move(captured_stage)]() mutable {
                        if (token != m_current_busy_token)
                        {
                            return;
                        }
                        timer_stage.finish({});
                    }));
            }));
}

// Saves to the current destination when one exists; Save As is responsible for destination choice.
void EditorController::Impl::onSaveRequested()
{
    runAction(EditorAction::SaveProject{});
}

// Saves to a chosen destination and promotes future Save commands to direct saves.
void EditorController::Impl::onSaveAsRequested(std::filesystem::path file)
{
    runAction(EditorAction::SaveProjectAs{std::move(file)});
}

// Publishes the current project as a native song package without changing save destination or
// dirty state.
void EditorController::Impl::onPublishRequested(std::filesystem::path file)
{
    runAction(EditorAction::PublishProject{std::move(file)});
}

// Cancels only a Save As chooser that was opened to continue a deferred project command.
void EditorController::Impl::onSaveAsCancelled()
{
    runAction(EditorAction::CancelSaveAsPrompt{});
}

// Closes the current project after prompting for unsaved changes when needed.
void EditorController::Impl::onCloseRequested()
{
    runAction(EditorAction::CloseProject{});
}

// Exits through the composition host after prompting for unsaved changes when needed.
void EditorController::Impl::onExitRequested()
{
    runAction(EditorAction::ExitApplication{});
}

// Applies the user's unsaved-changes choice to the stored deferred project command.
void EditorController::Impl::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    runAction(EditorAction::ResolveUnsavedChangesPrompt{decision});
}

// Resolves the next-launch recovery prompt shown after startup restore was interrupted.
void EditorController::Impl::onRestoreInterruptedDecision(RestoreInterruptedDecision decision)
{
    if (!m_restore_interrupted_prompt_file.has_value())
    {
        updateView();
        return;
    }

    const std::filesystem::path project_file = *m_restore_interrupted_prompt_file;
    m_restore_interrupted_prompt_file.reset();
    switch (decision)
    {
        case RestoreInterruptedDecision::Retry:
        {
            if (!projectFileExists(project_file))
            {
                clearLastOpenProjectIfMatches(project_file);
                clearInterruptedRestoreMarker();
                updateView();
                return;
            }

            if (m_settings != nullptr)
            {
                m_settings->setLastOpenProject(project_file);
            }
            runAction(EditorAction::RestoreProject{project_file});
            break;
        }
        case RestoreInterruptedDecision::Cancel:
        {
            m_pending_restore_project_file.reset();
            if (m_settings != nullptr)
            {
                m_settings->setLastOpenProject(std::nullopt);
            }
            clearInterruptedRestoreMarker();
            updateView();
            break;
        }
    }
}

// Ignores the intent until audio activation has committed an arrangement, otherwise toggles
// playback.
void EditorController::Impl::onPlayPausePressed()
{
    runAction(EditorAction::PlayPause{});
}

// Mirrors the published transport.stop_enabled gate so the keyboard or alternate input paths
// cannot stop a transport the view considers already reset.
void EditorController::Impl::onStopPressed()
{
    runAction(EditorAction::Stop{});
}

// Clamps the normalized input and converts it through the session timeline so the seek target
// stays inside the loaded content even when the view emits out-of-range values.
void EditorController::Impl::onWaveformClicked(double normalized_x)
{
    runAction(EditorAction::SeekWaveform{normalized_x});
}

// Shows the plugin browser with whatever plugins the host already knows. Full catalog discovery is
// intentionally left behind the explicit Rescan button because plugin scans can be slow.
void EditorController::Impl::onPluginBrowserRequested()
{
    runAction(EditorAction::ShowPluginBrowser{});
}

// Hides the browser directly because closing a presentation window should not be blocked by an
// unrelated busy operation. In-flight scans still complete against the cached browser state.
void EditorController::Impl::onPluginBrowserClosed()
{
    if (!m_plugin_browser_visible)
    {
        return;
    }

    m_plugin_browser_visible = false;
    updateView();
}

// Starts a user-requested catalog refresh through the normal action gate.
void EditorController::Impl::onPluginCatalogScanRequested()
{
    runAction(EditorAction::ScanPluginCatalog{});
}

// Adds the plugin selected by the browser window.
void EditorController::Impl::onAddPluginRequested(std::string plugin_id)
{
    runAction(EditorAction::AddPlugin{std::move(plugin_id)});
}

// Removes one runtime plugin instance from the current linear chain without marking the project
// dirty while tone persistence does not exist.
void EditorController::Impl::onRemovePluginRequested(std::string instance_id)
{
    runAction(EditorAction::RemovePlugin{std::move(instance_id)});
}

// Opens the hosted plugin editor window for a row-level signal-chain request.
void EditorController::Impl::onOpenPluginRequested(std::string instance_id)
{
    runAction(EditorAction::OpenPlugin{std::move(instance_id)});
}

// Wraps the supplied audio-device open work in the editor's busy overlay paint fence so the
// blocking presentation paints once before juce::AudioDeviceManager occupies the message thread.
// The settings dialog launcher provides work already aware of dialog success/failure handling, so
// this method owns only the busy lifecycle.
void EditorController::Impl::onAudioDeviceChangeRequested(std::function<void()> change_audio_device)
{
    if (!change_audio_device || m_input_calibration_prompt_visible)
    {
        return;
    }

    const std::uint64_t token = beginBusy(BusyOperation::OpeningAudioDevice);
    updateView();
    runAfterBusyOverlayPainted(
        safeCallback([this, token, captured_change = std::move(change_audio_device)]() mutable {
            if (token != m_current_busy_token)
            {
                return;
            }
            captured_change();
            syncCommittedInputDeviceIdentity();
            applyLiveInputGate();
            finishBusyOperation();
        }));
}

// Persists the new device manager state and re-derives view state after a configuration change.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    syncCommittedInputDeviceIdentity();
    applyLiveInputGate();
    updateView();
}

// Marks the audio settings window active so route transitions can be committed as one change.
void EditorController::Impl::onAudioDeviceSettingsOpened()
{
    if (m_input_calibration_prompt_visible)
    {
        return;
    }

    m_audio_device_settings_window_open = true;
    updateView();
}

// Re-applies the route gate after settings closes or restores its previous route.
void EditorController::Impl::onAudioDeviceSettingsClosed()
{
    m_audio_device_settings_window_open = false;
    syncCommittedInputDeviceIdentity();
    applyLiveInputGate();
    updateView();
}

// Applies the central action gate and routes the accepted action.
void EditorController::Impl::runAction(EditorAction::Action action)
{
    if (!prepareAction(idOf(action)))
    {
        return;
    }

    performAction(std::move(action));
}

// Applies availability and busy policy before an action mutates state or schedules work.
bool EditorController::Impl::prepareAction(EditorAction::Id action)
{
    if (!canRunAction(action))
    {
        return false;
    }

    const ActionBusyPolicy busy_policy = actionBusyPolicy(action);
    if (isBusy() && busy_policy == ActionBusyPolicy::SupersedesBusy)
    {
        supersedeBusyOperation();
    }

    return true;
}

// Visits the variant once and dispatches to a typed overload per case. The overloads keep each
// case body short and individually testable, with payload access through the alternative's fields.
void EditorController::Impl::performAction(EditorAction::Action action)
{
    std::visit(
        [this](auto&& a) { performActionImpl(std::forward<decltype(a)>(a)); }, std::move(action));
}

void EditorController::Impl::performActionImpl(EditorAction::OpenProject action)
{
    requestProjectAction(EditorAction::OpenProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::RestoreProject action)
{
    requestProjectAction(EditorAction::RestoreProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::ImportSong action)
{
    requestProjectAction(EditorAction::ImportSong{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProject /*action*/)
{
    if (m_save_requires_destination)
    {
        return;
    }
    runProjectAction(EditorAction::SaveProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProjectAs action)
{
    runProjectAction(EditorAction::SaveProjectAs{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::PublishProject action)
{
    runProjectAction(EditorAction::PublishProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::CloseProject /*action*/)
{
    requestProjectAction(EditorAction::CloseProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::ExitApplication /*action*/)
{
    requestProjectAction(EditorAction::ExitApplication{});
}

void EditorController::Impl::performActionImpl(EditorAction::ResolveUnsavedChangesPrompt action)
{
    if (!m_deferred_action.has_value())
    {
        clearDeferredAction();
        updateView();
        return;
    }

    m_unsaved_changes_prompt_visible = false;
    switch (action.decision)
    {
        case UnsavedChangesDecision::Save:
        {
            if (m_save_requires_destination)
            {
                m_save_as_prompt_visible = true;
                updateView();
                return;
            }

            // The deferred action stays in m_deferred_action so completeProjectWriteAction can
            // resume it after the save commits, and drop it if the save fails.
            runProjectAction(EditorAction::SaveProject{});
            break;
        }
        case UnsavedChangesDecision::Discard:
        {
            const EditorAction::Id deferred_id = idOf(*m_deferred_action);
            std::filesystem::path displaced_by_import;
            if (deferred_id == EditorAction::Id::ImportSong)
            {
                displaced_by_import =
                    !m_project_file.empty() ? m_project_file : m_displaced_project_file;
            }
            m_has_unsaved_changes = false;
            m_save_requires_destination = false;
            // CloseProject and ExitApplication both close the current project as part of their
            // own action handler, and ExitApplication additionally needs m_project_file alive
            // when it captures the value to persist as last_open_project. Closing here first
            // would zero that path out, so let the replay action do its own close + capture.
            if (deferred_id == EditorAction::Id::CloseProject ||
                deferred_id == EditorAction::Id::ExitApplication)
            {
                continueDeferredAction();
                break;
            }

            EditorAction::ProjectAction replay = std::move(*m_deferred_action);
            clearDeferredAction();
            if (closeProject())
            {
                if (!displaced_by_import.empty())
                {
                    m_displaced_project_file = std::move(displaced_by_import);
                }
                runProjectAction(std::move(replay));
            }
            return;
        }
        case UnsavedChangesDecision::Cancel:
        {
            clearDeferredAction();
            updateView();
            break;
        }
    }
}

void EditorController::Impl::performActionImpl(EditorAction::CancelSaveAsPrompt /*action*/)
{
    if (!m_save_as_prompt_visible)
    {
        return;
    }

    clearDeferredAction();
    updateView();
}

void EditorController::Impl::performActionImpl(EditorAction::PlayPause /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (m_transport.state().playing)
    {
        m_transport.pause();
    }
    else
    {
        m_transport.play();
    }
}

void EditorController::Impl::performActionImpl(EditorAction::Stop /*action*/)
{
    const common::audio::TransportState transport_state = m_transport.state();
    if (!canStopTransport(transport_state))
    {
        return;
    }
    m_transport.stop();

    if (!transport_state.playing)
    {
        updateView();
    }
}

void EditorController::Impl::performActionImpl(EditorAction::SeekWaveform action)
{
    const double clamped = std::clamp(action.normalized_x, 0.0, 1.0);
    const common::core::TimeRange timeline_range = session().timeline();
    const double target_seconds =
        timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
    m_transport.seek(timeline_range.clamp(common::core::TimePosition{target_seconds}));
    updateView();
}

// Makes the browser visible and refreshes the lightweight in-memory catalog.
void EditorController::Impl::performActionImpl(EditorAction::ShowPluginBrowser /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    m_plugin_browser_visible = true;
    refreshKnownPluginCatalog();
    updateView();
}

// Offloads catalog scanning to the editor task runner because directory traversal and plugin
// inspection can execute slow third-party code.
void EditorController::Impl::performActionImpl(EditorAction::ScanPluginCatalog /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    auto state = std::make_shared<PluginCatalogTaskState>();
    runBusyOperation(
        BusyOperation::ScanningPlugins,
        state,
        [plugin_host = &m_plugin_host](const std::shared_ptr<PluginCatalogTaskState>& task_state) {
            task_state->scan_result = plugin_host->scanPluginCatalog();
        },
        [this](const std::shared_ptr<PluginCatalogTaskState>& task_state) {
            completePluginCatalogScan(task_state);
        });
}

// Begins loading the selected browser plugin. The catalog is the authority for display
// metadata, while the audio boundary remains the authority for creating the runtime plugin.
void EditorController::Impl::performActionImpl(const EditorAction::AddPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const auto found = std::ranges::find_if(
        m_plugin_catalog, [&action](const common::audio::PluginCandidate& plugin_candidate) {
            return plugin_candidate.id == action.plugin_id;
        });
    if (found == m_plugin_catalog.end())
    {
        reportError("Could not add plugin: selected plugin is no longer available");
        updateView();
        return;
    }

    beginAddKnownPlugin(*found);
}

// Inserts the selected browser plugin into the live chain after the loading state has painted.
void EditorController::Impl::completeAddPluginLoad(const std::shared_ptr<AddPluginTaskState>& state)
{
    assert(isBusy() && "completeAddPluginLoad called outside a busy operation");

    const common::audio::PluginCandidate& plugin_candidate = state->plugin_candidate;
    const auto handle = m_plugin_host.addPlugin(plugin_candidate);
    if (!handle.has_value())
    {
        const std::string message = handle.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not add plugin: "} + message);
        return;
    }

    m_plugins.push_back(makePluginViewState(plugin_candidate, *handle));
    m_plugin_browser_visible = false;
    if (hasLiveRigPersistence())
    {
        m_has_unsaved_changes = true;
    }

    finishBusyOperation();
}

// Starts the blocking plugin-instantiation phase after pushing LoadingPlugin state first.
void EditorController::Impl::beginAddKnownPlugin(
    const common::audio::PluginCandidate& plugin_candidate)
{
    auto state = std::make_shared<AddPluginTaskState>();
    state->plugin_candidate = plugin_candidate;

    const std::uint64_t token = beginBusy(BusyOperation::LoadingPlugin);
    updateView();
    runAfterBusyOverlayPainted(safeCallback([this, state, token]() {
        if (token != m_current_busy_token)
        {
            return;
        }
        completeAddPluginLoad(state);
    }));
}

// Replaces the browser catalog with the latest scan result while keeping the browser open.
void EditorController::Impl::completePluginCatalogScan(
    const std::shared_ptr<PluginCatalogTaskState>& state)
{
    assert(isBusy() && "completePluginCatalogScan called outside a busy operation");

    if (!state->scan_result.has_value())
    {
        const std::string message = state->scan_result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not scan plugins: "} + message);
        return;
    }

    // Rescan refreshes the host-owned catalog on a worker thread. The controller reads the
    // canonical known catalog here on the message thread and keeps only a sorted UI snapshot.
    refreshKnownPluginCatalog();
    finishBusyOperation();
}

// Refreshes the browser from Tracktion's already-known plugins without touching the filesystem.
void EditorController::Impl::refreshKnownPluginCatalog()
{
    m_plugin_catalog = m_plugin_host.knownPluginCatalog();
    sortPluginCatalog(m_plugin_catalog);
}

void EditorController::Impl::performActionImpl(const EditorAction::RemovePlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const auto plugin = std::ranges::find_if(m_plugins, [&action](const PluginViewState& item) {
        return item.instance_id == action.instance_id;
    });
    if (plugin == m_plugins.end())
    {
        return;
    }

    const auto result = m_plugin_host.removePlugin(action.instance_id);
    if (!result.has_value())
    {
        reportError(std::string{"Could not remove plugin: "} + result.error().message);
        updateView();
        return;
    }

    m_plugins.erase(plugin);
    for (std::size_t index = 0; index < m_plugins.size(); ++index)
    {
        m_plugins[index].chain_index = index;
    }
    if (hasLiveRigPersistence())
    {
        m_has_unsaved_changes = true;
    }
    updateView();
}

void EditorController::Impl::performActionImpl(const EditorAction::OpenPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const auto plugin = std::ranges::find_if(m_plugins, [&action](const PluginViewState& item) {
        return item.instance_id == action.instance_id;
    });
    if (plugin == m_plugins.end())
    {
        return;
    }

    const auto result = m_plugin_host.openPluginWindow(action.instance_id);
    if (!result.has_value())
    {
        reportError(std::string{"Could not open plugin: "} + result.error().message);
    }
}

// Opens the required calibration prompt on explicit user request.
void EditorController::Impl::onInputCalibrationRequested()
{
    if (!m_audio_device_settings_window_open && m_project_audio_ready && hasLoadedArrangement() &&
        currentInputDeviceIdentity().has_value())
    {
        if (m_transport.state().playing)
        {
            m_transport.pause();
        }
        m_input_calibration_prompt_visible = true;
    }
    updateView();
}

// Prepares the current input route for a raw calibration measurement.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationMeasurementStarted()
{
    if (!m_project_audio_ready || !hasLoadedArrangement())
    {
        return std::unexpected{common::audio::LiveInputError{
            common::audio::LiveInputErrorCode::InputRouteUnavailable,
            "Project audio is not ready",
        }};
    }

    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    if (!current_identity.has_value())
    {
        return std::unexpected{
            common::audio::LiveInputError{common::audio::LiveInputErrorCode::InputRouteUnavailable}
        };
    }

    std::optional<common::audio::InputCalibrationState> previous_calibration_state;
    if (m_input_calibration_state.has_value() && common::audio::inputCalibrationMatchesIdentity(
                                                     *m_input_calibration_state, *current_identity))
    {
        previous_calibration_state = m_input_calibration_state;
    }

    const InputCalibrationRouteState previous_route_state = currentInputCalibrationRouteState();
    auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
    if (!monitoring_disabled.has_value())
    {
        return std::unexpected{std::move(monitoring_disabled.error())};
    }

    auto gain_reset =
        m_live_input.setInputGain(common::audio::Gain{common::audio::defaultGainDb()});
    if (!gain_reset.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        return std::unexpected{std::move(gain_reset.error())};
    }

    auto calibration_monitoring_enabled = m_live_input.setCalibrationInputMonitoringEnabled(true);
    if (!calibration_monitoring_enabled.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        return std::unexpected{std::move(calibration_monitoring_enabled.error())};
    }

    m_active_input_calibration = ActiveInputCalibration{
        .input_device_identity = *current_identity,
        .previous_calibration_state = std::move(previous_calibration_state),
    };
    updateView();
    return {};
}

// Stops an in-progress measurement without closing the calibration prompt.
void EditorController::Impl::onInputCalibrationMeasurementCancelled()
{
    if (m_active_input_calibration.has_value())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
        m_active_input_calibration.reset();
    }
    updateView();
}

// Applies a successful calibration gain, persists it, and enables processed monitoring.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationSucceeded(double gain_db)
{
    if (!m_active_input_calibration.has_value())
    {
        return std::unexpected{common::audio::LiveInputError{
            common::audio::LiveInputErrorCode::InputRouteUnavailable,
            "Calibration measurement is not active",
        }};
    }

    return commitInputCalibration(gain_db, m_active_input_calibration->input_device_identity);
}

// Applies a user-entered calibration gain without requiring an active automatic attempt.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationManuallySet(double gain_db)
{
    return commitInputCalibration(gain_db, std::nullopt);
}

// Closes the calibration prompt without enabling uncalibrated live monitoring.
void EditorController::Impl::onInputCalibrationDismissed()
{
    if (m_active_input_calibration.has_value())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
    }
    m_active_input_calibration.reset();
    closeInputCalibrationPrompt();
    updateView();
}

// Applies a clamped output gain to the live rig and marks the tone dirty.
void EditorController::Impl::onOutputGainChanged(double gain_db)
{
    if (!m_project_audio_ready || !hasLoadedArrangement() || isBusy())
    {
        return;
    }

    const auto gain = common::audio::clampGain(common::audio::Gain{gain_db});
    if (gain.db == m_output_gain_db)
    {
        return;
    }

    const auto result = m_live_rig.setOutputGain(gain);
    if (!result.has_value())
    {
        reportError(std::string{"Could not set output gain: "} + result.error().message);
        updateView();
        return;
    }

    m_output_gain_db = gain.db;
    if (hasLiveRigPersistence())
    {
        m_has_unsaved_changes = true;
    }
    updateView();
}

// Combines natural action availability with the action's busy-state policy.
bool EditorController::Impl::canRunAction(EditorAction::Id action) const
{
    const ActionBusyPolicy busy_policy = actionBusyPolicy(action);
    if (isBusy())
    {
        switch (busy_policy)
        {
            case ActionBusyPolicy::BlockedByBusy:
            {
                return false;
            }
            case ActionBusyPolicy::SupersedesBusy:
            {
                return true;
            }
            case ActionBusyPolicy::AllowedWhileBusy:
            {
                return actionAvailableWhenIdle(action);
            }
        }
    }

    return actionAvailableWhenIdle(action);
}

// Keeps action availability in one policy table instead of relying on the view to hide actions.
bool EditorController::Impl::actionAvailableWhenIdle(EditorAction::Id action) const
{
    if (m_input_calibration_prompt_visible &&
        (action == EditorAction::Id::PlayPause || action == EditorAction::Id::ShowPluginBrowser ||
         action == EditorAction::Id::ScanPluginCatalog || action == EditorAction::Id::AddPlugin ||
         action == EditorAction::Id::RemovePlugin || action == EditorAction::Id::OpenPlugin))
    {
        return false;
    }

    switch (action)
    {
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::ExitApplication:
        {
            return true;
        }
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::CloseProject:
        {
            return m_project.has_value();
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            return m_deferred_action.has_value() && m_unsaved_changes_prompt_visible;
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            return m_save_as_prompt_visible;
        }
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::SeekWaveform:
        {
            return hasLoadedArrangement();
        }
        case EditorAction::Id::Stop:
        {
            return canStopTransport(m_transport.state());
        }
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::ScanPluginCatalog:
        {
            return hasLoadedArrangement() && liveInputAuditionAvailable();
        }
        case EditorAction::Id::AddPlugin:
        {
            return hasLoadedArrangement() && liveInputAuditionAvailable() &&
                   !m_plugin_catalog.empty();
        }
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::OpenPlugin:
        {
            return hasLoadedArrangement() && liveInputAuditionAvailable() && !m_plugins.empty();
        }
    }

    return false;
}

// Encodes the small set of actions that intentionally do not follow normal busy blocking.
EditorController::Impl::ActionBusyPolicy EditorController::Impl::actionBusyPolicy(
    EditorAction::Id action) noexcept
{
    switch (action)
    {
        case EditorAction::Id::CloseProject:
        case EditorAction::Id::ExitApplication:
        {
            return ActionBusyPolicy::SupersedesBusy;
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekWaveform:
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::ScanPluginCatalog:
        case EditorAction::Id::AddPlugin:
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::OpenPlugin:
        {
            return ActionBusyPolicy::BlockedByBusy;
        }
    }

    return ActionBusyPolicy::BlockedByBusy;
}

// Coarse-only transport callback. During an in-flight session load, defer the push so the final
// derivation runs against the updated session and transport state instead of stale data.
void EditorController::Impl::onTransportStateChanged(common::audio::TransportState /*state*/)
{
    if (m_session_load_in_progress)
    {
        return;
    }
    updateView();
}

// Starts a project-level action or asks the view to confirm unsaved changes first. Callers pass
// the original action; on dirty state the action itself is stashed for replay after the prompt
// resolves.
void EditorController::Impl::requestProjectAction(EditorAction::ProjectAction action)
{
    if (hasUnsavedChanges())
    {
        m_deferred_action = std::move(action);
        m_unsaved_changes_prompt_visible = true;
        m_save_as_prompt_visible = false;
        updateView();
        return;
    }

    runProjectAction(std::move(action));
}

// Runs a project-level action once dirty-state gates have been satisfied. Write-side cases
// re-pack the moved alternative into a fresh Action rather than capturing the outer variant by
// reference, so the visit's view into the source variant cannot dangle if the destination move
// reassigns it.
void EditorController::Impl::runProjectAction(EditorAction::ProjectAction action)
{
    std::visit(
        [this](auto&& a) {
            using A = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<A, EditorAction::CloseProject>)
            {
                // Capture the displaced path before closeProject() clears it so we can re-open
                // the project that was displaced by an import the user is now discarding.
                const std::filesystem::path displaced = m_displaced_project_file;
                if (closeProject())
                {
                    clearDeferredAction();
                    if (!displaced.empty())
                    {
                        openProject(displaced, false);
                    }
                    else
                    {
                        updateView();
                    }
                }
            }
            else if constexpr (std::is_same_v<A, EditorAction::OpenProject>)
            {
                openProject(a.file, false);
            }
            else if constexpr (std::is_same_v<A, EditorAction::RestoreProject>)
            {
                openProject(a.file, true);
            }
            else if constexpr (std::is_same_v<A, EditorAction::ImportSong>)
            {
                importSongSource(a.file);
            }
            else if constexpr (
                std::is_same_v<A, EditorAction::SaveProject> ||
                std::is_same_v<A, EditorAction::SaveProjectAs> ||
                std::is_same_v<A, EditorAction::PublishProject>
            )
            {
                runProjectWriteAction(
                    EditorAction::ProjectWriteAction{std::forward<decltype(a)>(a)});
            }
            else if constexpr (std::is_same_v<A, EditorAction::ExitApplication>)
            {
                const std::optional<std::filesystem::path> restorable_project_file =
                    restorableProjectFileForExit();
                if (closeProject())
                {
                    m_pending_restore_project_file.reset();
                    clearDeferredAction();
                    if (m_settings != nullptr)
                    {
                        m_settings->setLastOpenProject(restorable_project_file);
                    }
                    updateView();
                    m_exit_function();
                }
            }
        },
        std::move(action));
}

// Closes the current editor document across transport, backend audio, session, and workspace.
// Always supersedes any in-flight busy operation so closing or exiting during background work
// invalidates the worker's busy token; the worker's completion then sees a mismatch and
// discards itself rather than committing on top of a now-empty session.
bool EditorController::Impl::closeProject()
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    if (!m_project.has_value())
    {
        if (hasLoadedArrangement())
        {
            m_transport.stop();
        }
        clearLiveRig();
        m_song_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        m_output_gain_db = 0.0;
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        m_plugin_browser_visible = false;
        return true;
    }

    m_transport.stop();
    clearLiveRig();
    m_song_audio.clearActiveArrangement();
    m_session.reset();
    m_plugins.clear();
    m_output_gain_db = 0.0;

    auto closed = closeExistingProject(m_project);
    if (!closed.has_value())
    {
        reportError(std::string{"Could not close: "} + closed.error().message);
        m_project.reset();
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        m_plugin_browser_visible = false;
        updateView();
        return false;
    }

    m_project.reset();
    m_project_file.clear();
    m_displaced_project_file.clear();
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    m_plugin_browser_visible = false;
    return true;
}

// Snapshots message-thread state and transfers Project ownership to a write task so worker-side
// package IO cannot race with controller-owned Project mutation.
auto EditorController::Impl::takeProjectForWrite(EditorAction::ProjectWriteAction action)
    -> std::shared_ptr<ProjectWriteTaskState>
{
    if (!m_project.has_value())
    {
        return {};
    }

    Project& project = m_project.value();
    auto state = std::make_shared<ProjectWriteTaskState>(std::move(action));
    common::core::Song song = session().song();
    if (const auto rig_captured = captureLiveRigIntoSong(song, project); !rig_captured.has_value())
    {
        reportError(std::string{"Could not capture live rig: "} + rig_captured.error());
        return {};
    }

    state->project = std::move(project);
    state->song = std::move(song);
    state->editor_state = projectEditorStateForSave();
    m_project.reset();
    return state;
}

// Captures the selected arrangement's live rig state into song files before Project IO leaves the
// message thread.
std::expected<void, std::string> EditorController::Impl::captureLiveRigIntoSong(
    common::core::Song& song, const Project& project)
{
    const common::core::Arrangement* const current_arrangement = session().currentArrangement();
    if (current_arrangement == nullptr)
    {
        return {};
    }

    auto arrangement = std::ranges::find_if(
        song.arrangements, [current_arrangement](const common::core::Arrangement& item) {
            return item.id == current_arrangement->id;
        });
    if (arrangement == song.arrangements.end())
    {
        return std::unexpected{std::string{"current arrangement is missing from the song"}};
    }

    auto snapshot = m_live_rig.captureActiveRig(
        common::audio::LiveRigCaptureRequest{
            .song_directory = songDirectoryForProject(project),
            .arrangement_id = arrangement->id,
            .existing_tone_document_ref = arrangement->tone_document_ref,
        });
    if (!snapshot.has_value())
    {
        return std::unexpected{snapshot.error().message};
    }

    arrangement->tone_document_ref = snapshot->tone_document_ref;
    m_plugins = makePluginViewStates(snapshot->plugins);
    m_output_gain_db = snapshot->output_gain.db;
    return {};
}

// Restores the selected arrangement's saved tone document after the backing audio is active.
// Live rig restore runs cooperatively on the message thread inside the audio adapter, so this
// method always returns immediately and routes the audio load result through the on_loaded callback
// without mutating controller state.
void EditorController::Impl::restoreLiveRig(
    const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
    std::function<void(std::expected<common::audio::LiveRigLoadResult, std::string>)> on_loaded)
{
    if (!on_loaded)
    {
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        on_loaded(common::audio::LiveRigLoadResult{});
        return;
    }

    common::audio::LiveRigLoadRequest request{
        .song_directory = song_directory,
        .tone_document_ref = arrangement->tone_document_ref,
        .progress_callback = {},
        .yield_callback = {},
    };
    if (report_progress)
    {
        request.progress_callback =
            safeCallback([this, token](const common::audio::LiveRigLoadProgress& progress) {
                if (token != m_current_busy_token)
                {
                    return;
                }
                updateLiveRigLoadProgress(progress);
            });
        // Route the engine's per-step yield through the busy-overlay paint fence so each plugin's
        // progress update actually paints before the next step blocks the message thread.
        request.yield_callback = safeCallback([this, token](std::function<void()> next) {
            if (token != m_current_busy_token)
            {
                return;
            }
            runAfterBusyOverlayPainted(
                safeCallback([this, token, continuation = std::move(next)]() mutable {
                    if (token != m_current_busy_token)
                    {
                        return;
                    }
                    if (continuation)
                    {
                        continuation();
                    }
                }));
        });
    }

    m_live_rig.loadLiveRig(
        std::move(request),
        safeCallback(
            [completion = std::move(on_loaded)](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    loaded) {
                if (!loaded.has_value())
                {
                    completion(std::unexpected{loaded.error().message});
                    return;
                }
                completion(std::move(*loaded));
            }));
}

// Clears the audio backend's live rig chain as part of project teardown.
void EditorController::Impl::clearLiveRig()
{
    const auto cleared = m_live_rig.clearLiveRig();
    if (!cleared.has_value())
    {
        reportError(std::string{"Could not clear live rig: "} + cleared.error().message);
    }
}

// Runs project write actions through one task-runner path so busy lifetime, stale completion
// checks, and project restoration stay consistent across save, save-as, and publish.
void EditorController::Impl::runProjectWriteAction(EditorAction::ProjectWriteAction&& action)
{
    auto state = takeProjectForWrite(std::move(action));
    if (state == nullptr)
    {
        return;
    }

    runBusyOperation(
        busyOperationForProjectWrite(idOf(state->action)),
        state,
        [save_function = m_save_function,
         save_as_function = m_save_as_function,
         publish_function =
             m_publish_function](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            std::visit(
                [&task_state, &save_function, &save_as_function, &publish_function](
                    auto&& alternative) {
                    using A = std::decay_t<decltype(alternative)>;
                    if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
                    {
                        task_state->result = save_function(
                            task_state->project, task_state->song, task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
                    {
                        task_state->result = save_as_function(
                            task_state->project,
                            alternative.file,
                            task_state->song,
                            task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
                    {
                        task_state->result = publish_function(
                            task_state->project, alternative.file, task_state->song);
                    }
                },
                task_state->action);
        },
        [this](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            completeProjectWriteAction(task_state);
        });
}

// Restores the Project context, clears busy before errors, and applies the successful write's
// action-specific state transition on the message thread.
void EditorController::Impl::completeProjectWriteAction(
    const std::shared_ptr<ProjectWriteTaskState>& state)
{
    assert(isBusy() && "completeProjectWriteAction called outside a busy operation");

    const EditorAction::Id action_id = idOf(state->action);
    m_project = std::move(state->project);
    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        // A deferred action present at the moment of a failed save means this save was the one
        // synthesized by the unsaved-changes prompt's Save branch; the user wanted to protect
        // their work first, so dropping the deferred replay is the right escape.
        if (m_deferred_action.has_value())
        {
            clearDeferredAction();
        }
        finishBusyOperation();
        reportError(std::string{projectWriteErrorPrefix(action_id)} + message);
        return;
    }

    std::visit(
        [this](auto&& alternative) {
            using A = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
            {
                m_has_unsaved_changes = false;
            }
            else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
            {
                m_save_requires_destination = false;
                m_project_file = alternative.file;
                m_displaced_project_file.clear();
                m_has_unsaved_changes = false;
            }
            else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
            {
                // Publish does not change save destination or dirty state.
            }
        },
        state->action);

    finishBusyOperation();
    if ((action_id == EditorAction::Id::SaveProject ||
         action_id == EditorAction::Id::SaveProjectAs) &&
        m_deferred_action.has_value())
    {
        continueDeferredAction();
    }
}

// Resumes a deferred action after Save or Save As has successfully protected user changes.
void EditorController::Impl::continueDeferredAction()
{
    if (!m_deferred_action.has_value())
    {
        updateView();
        return;
    }

    EditorAction::ProjectAction replay = std::move(*m_deferred_action);
    clearDeferredAction();
    runProjectAction(std::move(replay));
}

// Clears all prompt-related state without changing the currently loaded project.
void EditorController::Impl::clearDeferredAction() noexcept
{
    m_deferred_action.reset();
    m_unsaved_changes_prompt_visible = false;
    m_save_as_prompt_visible = false;
}

// Clears the restore-interrupted marker without touching the normal last-project path.
void EditorController::Impl::clearInterruptedRestoreMarker()
{
    if (m_settings != nullptr)
    {
        m_settings->setInterruptedRestoreProject(std::nullopt);
    }
}

// Removes the regular last-project path only when it refers to a now-invalid restore target.
void EditorController::Impl::clearLastOpenProjectIfMatches(
    const std::filesystem::path& project_file)
{
    if (m_settings == nullptr)
    {
        return;
    }

    const std::optional<std::filesystem::path> last_project = m_settings->lastOpenProject();
    if (last_project.has_value() && *last_project == project_file)
    {
        m_settings->setLastOpenProject(std::nullopt);
    }
}

// Returns the controller-owned editor session through the read-only access boundary.
const common::core::Session& EditorController::Impl::session() const noexcept
{
    return m_session;
}

// Returns an editor project file only when the current workspace is backed by one.
std::optional<std::filesystem::path> EditorController::Impl::currentProjectFile() const
{
    if (m_project_file.empty() || !hasLoadedArrangement())
    {
        return std::nullopt;
    }

    return m_project_file;
}

// Selects the project path persisted for next launch before exit tears down controller state.
std::optional<std::filesystem::path> EditorController::Impl::restorableProjectFileForExit() const
{
    std::optional<std::filesystem::path> restorable_project_file = currentProjectFile();
    if (!restorable_project_file.has_value() && !m_displaced_project_file.empty())
    {
        restorable_project_file = m_displaced_project_file;
    }
    if (!restorable_project_file.has_value() && m_pending_restore_project_file.has_value())
    {
        restorable_project_file = m_pending_restore_project_file;
    }

    return restorable_project_file;
}

// Restores a settings-backed project, or prompts first if the previous restore was interrupted.
void EditorController::Impl::restoreLastOpenProject()
{
    if (m_settings == nullptr)
    {
        return;
    }

    m_restore_interrupted_prompt_file.reset();
    const std::optional<std::filesystem::path> interrupted_project_file =
        m_settings->interruptedRestoreProject();
    if (interrupted_project_file.has_value())
    {
        if (!projectFileExists(*interrupted_project_file))
        {
            clearLastOpenProjectIfMatches(*interrupted_project_file);
            clearInterruptedRestoreMarker();
        }
        else
        {
            m_restore_interrupted_prompt_file = *interrupted_project_file;
            updateView();
            return;
        }
    }

    const std::optional<std::filesystem::path> project_file = m_settings->lastOpenProject();
    if (!project_file.has_value())
    {
        return;
    }

    if (!projectFileExists(*project_file))
    {
        m_settings->setLastOpenProject(std::nullopt);
        clearInterruptedRestoreMarker();
        return;
    }

    runAction(EditorAction::RestoreProject{*project_file});
}

// Captures editor-only persistence state from the current transport and displayed arrangement.
ProjectEditorState EditorController::Impl::projectEditorStateForSave() const
{
    ProjectEditorState editor_state{
        .cursor_position = m_transport.position(),
        .selected_arrangement = std::nullopt,
    };

    const auto* arrangement = session().currentArrangement();
    if (arrangement != nullptr && !arrangement->id.empty())
    {
        editor_state.selected_arrangement = arrangement->id;
    }

    return editor_state;
}

// Prepares project audio, activates the selected arrangement, and commits the song to Session.
bool EditorController::Impl::loadSessionSong(
    common::core::Song song, const std::optional<std::string>& selected_arrangement)
{
    if (song.arrangements.empty())
    {
        return false;
    }

    if (!m_song_audio.prepareSong(song))
    {
        return false;
    }

    const std::size_t selected_index = getSelectedArrangementIndex(song, selected_arrangement);
    m_session_load_in_progress = true;
    const bool active_arrangement_set =
        m_song_audio.setActiveArrangement(song.arrangements[selected_index]);
    bool committed = false;
    if (active_arrangement_set)
    {
        committed = m_session.loadSong(std::move(song), selected_index);
        assert(committed && "Session rejected backend-accepted project song");
        m_plugins.clear();
        m_output_gain_db = 0.0;
        m_plugin_browser_visible = false;
    }
    m_session_load_in_progress = false;

    return committed;
}

// Builds the message-thread view state from the session and transport state. Current cursor
// position is only sampled to derive stop enabledness; the view receives discrete mapping state
// rather than a continuously pushed playhead position.
EditorViewState EditorController::Impl::deriveViewState() const
{
    const common::audio::TransportState transport_state = m_transport.state();
    const common::core::TimeRange timeline_range = session().timeline();

    EditorViewState state;
    state.open_enabled = canRunAction(EditorAction::Id::OpenProject);
    state.import_enabled = canRunAction(EditorAction::Id::ImportSong);
    state.save_enabled = canRunAction(EditorAction::Id::SaveProject);
    state.save_as_enabled = canRunAction(EditorAction::Id::SaveProjectAs);
    state.publish_enabled = canRunAction(EditorAction::Id::PublishProject);
    if (!m_project_file.empty())
    {
        state.suggested_publish_file = m_project_file;
        state.suggested_publish_file.replace_extension(".rock");
    }
    state.close_enabled = canRunAction(EditorAction::Id::CloseProject);
    state.project_loaded = session().currentArrangement() != nullptr;
    state.save_requires_destination = m_save_requires_destination;
    state.transport.play_pause_enabled = canRunAction(EditorAction::Id::PlayPause);
    state.transport.stop_enabled = canRunAction(EditorAction::Id::Stop);
    state.transport.play_pause_shows_pause_icon = transport_state.playing;
    state.audio_devices_available = true;
    state.audio_device_settings_enabled = !m_input_calibration_prompt_visible;
    state.audio_device_status_text = audioDeviceStatusText(m_audio_devices.currentDeviceStatus());
    state.visible_timeline = timeline_range;
    state.signal_chain = SignalChainViewState{
        .add_plugin_enabled = canRunAction(EditorAction::Id::ShowPluginBrowser),
        .remove_plugins_enabled = canRunAction(EditorAction::Id::RemovePlugin),
        .plugins = m_plugins,
        .input_calibration_status = inputCalibrationStatus(),
        .input_calibrate_enabled =
            m_project_audio_ready && currentInputDeviceIdentity().has_value(),
        .disabled_message =
            liveInputAuditionAvailable() ? std::string{} : inputCalibrationDisabledMessage(),
        .output_gain_controls_enabled = m_project_audio_ready && hasLoadedArrangement(),
        .output_gain_db = m_output_gain_db,
    };
    state.plugin_browser = PluginBrowserViewState{
        .visible = m_plugin_browser_visible,
        .scan_enabled = canRunAction(EditorAction::Id::ScanPluginCatalog),
        .add_enabled = canRunAction(EditorAction::Id::AddPlugin),
        .plugins = makePluginCandidateViewStates(m_plugin_catalog),
    };

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
        };
    }
    if (m_deferred_action.has_value() && m_unsaved_changes_prompt_visible)
    {
        state.unsaved_changes_prompt = UnsavedChangesPrompt{idOf(*m_deferred_action)};
    }

    if (m_deferred_action.has_value() && m_save_as_prompt_visible)
    {
        state.save_as_prompt = SaveAsPrompt{idOf(*m_deferred_action)};
    }

    if (m_restore_interrupted_prompt_file.has_value())
    {
        state.restore_interrupted_prompt =
            RestoreInterruptedPrompt{*m_restore_interrupted_prompt_file};
    }

    if (m_input_calibration_prompt_visible)
    {
        state.input_calibration_prompt = InputCalibrationPrompt{
            .message = inputCalibrationDisabledMessage(),
            .input_gain_db = inputCalibrationPromptGainDb(),
        };
    }

    if (m_busy_operation.has_value())
    {
        BusyViewState busy{
            .operation = *m_busy_operation,
            .message = busyMessage(*m_busy_operation),
            .presentation = busyPresentation(*m_busy_operation),
            .cancel_enabled = false,
        };
        if (*m_busy_operation == BusyOperation::LoadingLiveRig && m_live_rig_progress.has_value())
        {
            busy.message = m_live_rig_progress->message;
            busy.progress = m_live_rig_progress->fraction;
        }
        state.busy = busy;
    }

    return state;
}

// Applies the serialized audio-device state stored by a previous editor session, if any.
void EditorController::Impl::restoreAudioDeviceState()
{
    if (m_settings == nullptr)
    {
        return;
    }

    const std::optional<std::string> xml_state = m_settings->audioDeviceState();
    if (!xml_state.has_value() || xml_state->empty())
    {
        return;
    }

    const std::unique_ptr<juce::XmlElement> xml = juce::parseXML(juce::String{xml_state->c_str()});
    if (xml == nullptr)
    {
        m_settings->setAudioDeviceState(std::nullopt);
        return;
    }

    m_audio_devices.deviceManager().initialise(1, 2, xml.get(), true);
}

// Stores the current device manager state so the next launch can restore the user's selection.
void EditorController::Impl::persistAudioDeviceState()
{
    if (m_settings == nullptr)
    {
        return;
    }

    const std::unique_ptr<juce::XmlElement> xml = m_audio_devices.deviceManager().createStateXml();
    if (xml == nullptr)
    {
        m_settings->setAudioDeviceState(std::nullopt);
        return;
    }

    m_settings->setAudioDeviceState(xml->toString().toStdString());
}

// Loads stored calibration only when it matches the current committed input route.
void EditorController::Impl::loadInputCalibrationFromSettings()
{
    if (m_settings == nullptr)
    {
        m_input_calibration_state.reset();
        return;
    }

    m_input_calibration_state = m_settings->inputCalibrationState();
    if (m_input_calibration_state.has_value() &&
        !common::audio::isValidInputDeviceIdentity(
            m_input_calibration_state->input_device_identity))
    {
        m_input_calibration_state.reset();
    }
}

// Persists the one app-local calibration record, or clears the stored value.
void EditorController::Impl::persistInputCalibration()
{
    if (m_settings != nullptr)
    {
        m_settings->setInputCalibrationState(m_input_calibration_state);
    }
}

// Clears calibration state and storage without changing the committed input identity.
void EditorController::Impl::clearInputCalibration()
{
    m_input_calibration_state.reset();
    persistInputCalibration();
}

// Returns the active exact input route identity, if the audio backend can provide one.
std::optional<common::audio::InputDeviceIdentity> EditorController::Impl::
    currentInputDeviceIdentity() const
{
    return m_audio_devices.currentInputDeviceIdentity();
}

// Updates committed input identity and clears calibration only when the route actually changed.
void EditorController::Impl::syncCommittedInputDeviceIdentity()
{
    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    if (!current_identity.has_value() && m_audio_device_settings_window_open)
    {
        return;
    }

    if (current_identity == m_committed_input_device_identity)
    {
        return;
    }

    m_committed_input_device_identity = current_identity;
    m_active_input_calibration.reset();
    clearInputCalibration();
    std::ignore = m_live_input.setCalibrationInputMonitoringEnabled(false);
    std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
}

// Reports whether stored calibration belongs to the currently active input route.
bool EditorController::Impl::inputCalibrationMatchesCurrentInput() const
{
    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    return current_identity.has_value() && m_input_calibration_state.has_value() &&
           common::audio::inputCalibrationMatchesIdentity(
               *m_input_calibration_state, *current_identity);
}

// Returns the gain shown in the calibration prompt without creating a stored calibration value.
double EditorController::Impl::inputCalibrationPromptGainDb() const
{
    return inputCalibrationMatchesCurrentInput() ? m_input_calibration_state->calibration_gain.db
                                                 : common::audio::defaultGainDb();
}

// Reports whether the live chain can be auditioned for the current route.
bool EditorController::Impl::liveInputAuditionAvailable() const
{
    if (!m_project_audio_ready || !hasLoadedArrangement())
    {
        return false;
    }

    return inputCalibrationMatchesCurrentInput() && !m_input_calibration_prompt_visible;
}

// Derives the calibration status shown by the signal-chain panel.
InputCalibrationStatus EditorController::Impl::inputCalibrationStatus() const
{
    if (!currentInputDeviceIdentity().has_value())
    {
        return InputCalibrationStatus::NoActiveInputDevice;
    }

    return inputCalibrationMatchesCurrentInput() ? InputCalibrationStatus::Calibrated
                                                 : InputCalibrationStatus::MissingCalibration;
}

// Builds the signal-chain disabled message for the current live input gate.
std::string EditorController::Impl::inputCalibrationDisabledMessage() const
{
    switch (inputCalibrationStatus())
    {
        case InputCalibrationStatus::NoActiveInputDevice:
        {
            return "Live input disabled: no audio input device selected.";
        }
        case InputCalibrationStatus::MissingCalibration:
        {
            return "Live input disabled: input calibration required.";
        }
        case InputCalibrationStatus::Calibrated:
        {
            return {};
        }
        case InputCalibrationStatus::Unavailable:
        {
            return "Live input disabled: live input backend unavailable.";
        }
    }

    return "Live input disabled.";
}

// Applies the backend live-input gate from the current route and calibration state.
void EditorController::Impl::applyLiveInputGate()
{
    std::ignore = m_live_input.setCalibrationInputMonitoringEnabled(false);

    if (!m_project_audio_ready || !hasLoadedArrangement())
    {
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return;
    }

    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    if (!current_identity.has_value())
    {
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return;
    }

    if (!m_input_calibration_state.has_value())
    {
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return;
    }

    if (!common::audio::inputCalibrationMatchesIdentity(
            *m_input_calibration_state, *current_identity))
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return;
    }

    auto gain_applied = m_live_input.setInputGain(m_input_calibration_state->calibration_gain);
    if (!gain_applied.has_value())
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return;
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
    }
}

// Reads the current live-input route values before a multi-step calibration setup mutation.
EditorController::Impl::InputCalibrationRouteState EditorController::Impl::
    currentInputCalibrationRouteState() const
{
    return InputCalibrationRouteState{
        .input_gain = m_live_input.inputGain(),
        .live_input_monitoring_enabled = m_live_input.liveInputMonitoringEnabled(),
        .calibration_input_monitoring_enabled = m_live_input.calibrationInputMonitoringEnabled(),
    };
}

// Best-effort rollback for setup failures before an automatic attempt exists.
void EditorController::Impl::restoreInputCalibrationRouteStateBestEffort(
    const InputCalibrationRouteState& route_state)
{
    std::ignore = m_live_input.setCalibrationInputMonitoringEnabled(
        route_state.calibration_input_monitoring_enabled);
    auto gain_restored = m_live_input.setInputGain(route_state.input_gain);
    if (!route_state.live_input_monitoring_enabled || gain_restored.has_value())
    {
        std::ignore =
            m_live_input.setLiveInputMonitoringEnabled(route_state.live_input_monitoring_enabled);
    }
}

// Closes the calibration prompt state without changing calibration validity.
void EditorController::Impl::closeInputCalibrationPrompt()
{
    m_input_calibration_prompt_visible = false;
}

// Applies a completed calibration value to the current live route and persists it.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::commitInputCalibration(
    double gain_db, std::optional<common::audio::InputDeviceIdentity> expected_identity)
{
    if (!m_project_audio_ready || !hasLoadedArrangement())
    {
        return std::unexpected{common::audio::LiveInputError{
            common::audio::LiveInputErrorCode::InputRouteUnavailable,
            "Project audio is not ready",
        }};
    }

    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    if (!current_identity.has_value())
    {
        return std::unexpected{
            common::audio::LiveInputError{common::audio::LiveInputErrorCode::InputRouteUnavailable}
        };
    }

    if (expected_identity.has_value() && *expected_identity != *current_identity)
    {
        return std::unexpected{common::audio::LiveInputError{
            common::audio::LiveInputErrorCode::InputRouteUnavailable,
            "Input route changed during calibration",
        }};
    }

    const common::audio::Gain gain = common::audio::clampGain(common::audio::Gain{gain_db});
    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    auto gain_applied = m_live_input.setInputGain(gain);
    if (!gain_applied.has_value())
    {
        return std::unexpected{std::move(gain_applied.error())};
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        m_input_calibration_state.reset();
        persistInputCalibration();
        return std::unexpected{std::move(monitoring_enabled.error())};
    }

    m_input_calibration_state = common::audio::InputCalibrationState{
        .calibration_gain = gain,
        .input_device_identity = *current_identity,
    };
    m_committed_input_device_identity = current_identity;
    persistInputCalibration();
    m_active_input_calibration.reset();
    updateView();
    return {};
}

// Restores the previous matching calibration if a manual recalibration measurement is cancelled.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    restoreCalibrationMeasurementState()
{
    if (!m_active_input_calibration.has_value())
    {
        return {};
    }

    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    if (!m_project_audio_ready || !hasLoadedArrangement())
    {
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return {};
    }

    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    if (!current_identity.has_value() ||
        *current_identity != m_active_input_calibration->input_device_identity)
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return {};
    }

    const std::optional<common::audio::InputCalibrationState>& previous_state =
        m_active_input_calibration->previous_calibration_state;
    if (!previous_state.has_value() ||
        !common::audio::inputCalibrationMatchesIdentity(*previous_state, *current_identity))
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return {};
    }

    auto gain_restored = m_live_input.setInputGain(previous_state->calibration_gain);
    if (!gain_restored.has_value())
    {
        clearInputCalibration();
        std::ignore = m_live_input.setLiveInputMonitoringEnabled(false);
        return std::unexpected{std::move(gain_restored.error())};
    }

    auto monitoring_restored = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_restored.has_value())
    {
        clearInputCalibration();
        return std::unexpected{std::move(monitoring_restored.error())};
    }

    m_input_calibration_state = previous_state;
    persistInputCalibration();
    return {};
}

// Caches the derived state as the seed for future attachView() pushes and forwards it to the
// currently attached view if any. The null branch covers the construction window during which
// restoreAudioDeviceState() may fire onAudioDeviceConfigurationChanged() before attachView().
void EditorController::Impl::updateView()
{
    m_last_state = deriveViewState();
    if (m_view != nullptr)
    {
        m_view->setState(m_last_state);
    }
}

// Sends transient workflow failures through the view effect channel rather than render state.
void EditorController::Impl::reportError(const std::string& message)
{
    if (m_view != nullptr)
    {
        m_view->showError(message);
    }
}

// Answers the "has loading committed a usable arrangement" question used by intent gates.
bool EditorController::Impl::hasLoadedArrangement() const
{
    return session().currentArrangement() != nullptr;
}

// Reports whether the active arrangement has persisted plugin state worth showing as progress.
bool EditorController::Impl::shouldShowLiveRigLoadProgress() const
{
    if (!hasLiveRigPersistence())
    {
        return false;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    return arrangement != nullptr && !arrangement->tone_document_ref.empty();
}

// Reports whether plugin mutations can be captured into project saves.
bool EditorController::Impl::hasLiveRigPersistence() const noexcept
{
    return true;
}

// Reports whether a busy operation is currently active.
bool EditorController::Impl::isBusy() const noexcept
{
    return m_busy_operation.has_value();
}

// Begins a busy operation and advances the current busy token. The BusyViewState shown to the
// view is derived from m_busy_operation in deriveViewState(); this function only sets the source
// state.
std::uint64_t EditorController::Impl::beginBusy(BusyOperation operation)
{
    m_current_busy_token += 1;
    m_busy_operation = operation;
    return m_current_busy_token;
}

// Moves an in-flight busy operation into a new visible phase without changing its token. Project
// open/import workers use this when validation enters audio analysis but still has one completion.
void EditorController::Impl::transitionBusyOperation(BusyOperation operation, std::uint64_t token)
{
    if (token != m_current_busy_token || !m_busy_operation.has_value())
    {
        return;
    }

    m_busy_operation = operation;
    m_live_rig_progress.reset();
    updateView();
}

// Moves to a visible phase and releases the worker after that phase has had a chance to paint.
// This is only a presentation fence; the expensive audio work still runs on the worker thread.
void EditorController::Impl::transitionBusyOperationAfterPaint(
    BusyOperation operation, std::uint64_t token, std::shared_ptr<AnalysisPaintGate> gate)
{
    if (gate == nullptr)
    {
        return;
    }

    if (token != m_current_busy_token || !m_busy_operation.has_value())
    {
        gate->release();
        return;
    }

    transitionBusyOperation(operation, token);
    if (m_view == nullptr)
    {
        gate->release();
        return;
    }

    runAfterBusyOverlayPainted([gate = std::move(gate)] { gate->release(); });
}

// Normal operation completion: clears busy state and pushes the resulting view state so the
// overlay clears in the same frame. Completion paths call this only after their captured busy
// token has already matched the current busy token. Failure sites call this BEFORE
// reportError() so the cleared state is pushed before any modal dialog the error path may
// raise. Otherwise the dialog overlays a stale "busy" view.
void EditorController::Impl::finishBusyOperation()
{
    endBusy();
    updateView();
}

// Close/Exit-style commands that intentionally take over the busy lifecycle. Does not push view
// state itself; the accepted action's close or exit path owns the next visible state push.
void EditorController::Impl::supersedeBusyOperation()
{
    endBusy();
}

// Clears busy state and advances the current busy token. This makes any in-flight worker completion
// stale whether this is a normal finish or a superseding action takeover.
void EditorController::Impl::endBusy()
{
    m_busy_operation.reset();
    m_live_rig_progress.reset();
    m_current_busy_token += 1;
}

// Writes the current live rig load progress (message override and 0..1 fraction). Does not
// promote m_busy_operation; beginLiveRigLoadProgress() is responsible for that transition.
// deriveViewState() composes m_busy_operation with m_live_rig_progress to produce the
// BusyViewState shown to the view.
void EditorController::Impl::setLiveRigLoadProgress(std::string message, double fraction)
{
    if (!m_busy_operation.has_value())
    {
        return;
    }

    m_live_rig_progress = LiveRigProgress{
        .message = std::move(message),
        .fraction = fraction,
    };
}

// Switches a project-load busy operation into determinate live rig restore progress and pushes
// the new state so the overlay paints at 0% before any plugin construction starts. Promoting
// m_busy_operation is part of this transition because LoadingLiveRig is a distinct phase from
// the surrounding project-open or import operation.
void EditorController::Impl::beginLiveRigLoadProgress()
{
    if (!m_busy_operation.has_value())
    {
        return;
    }
    m_busy_operation = BusyOperation::LoadingLiveRig;
    setLiveRigLoadProgress(busyMessage(BusyOperation::LoadingLiveRig), 0.0);
    updateView();
}

// Applies plugin-level progress reported by the live rig boundary to the busy overlay state.
void EditorController::Impl::updateLiveRigLoadProgress(
    const common::audio::LiveRigLoadProgress& progress)
{
    setLiveRigLoadProgress(liveRigProgressMessage(progress), liveRigProgressFraction(progress));
    updateView();
}

// Routes message-thread-only load work through the attached view's busy-overlay paint fence.
// Every caller is downstream of a user UI intent, so a view must already be attached; the
// assert pins that precondition.
void EditorController::Impl::runAfterBusyOverlayPainted(std::function<void()>&& callback)
{
    if (!callback)
    {
        return;
    }

    assert(m_view != nullptr && "runAfterBusyOverlayPainted requires an attached view");
    m_view->runAfterBusyOverlayPainted(std::move(callback));
}

// Treat imported unsaved projects and future session edits as requiring confirmation.
bool EditorController::Impl::hasUnsavedChanges() const noexcept
{
    return m_project.has_value() && (m_has_unsaved_changes || m_save_requires_destination);
}

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::Impl::canStopTransport(
    const common::audio::TransportState& transport_state) const
{
    return hasLoadedArrangement() &&
           (transport_state.playing || m_transport.position() != session().timeline().start);
}

} // namespace rock_hero::editor::core
