#include "editor_controller.h"

#include "editor_action.h"
#include "project_io.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/scoped_listener.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <rock_hero/editor/core/inline_editor_task_runner.h>
#include <rock_hero/editor/core/psarc_song_importer.h>
#include <rock_hero/editor/core/rock_song_importer.h>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Production open path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultOpen(
    Project& project, const std::filesystem::path& file)
{
    return project.load(file);
}

// Production import path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultImport(
    Project& project, const std::filesystem::path& file)
{
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension == ".rock")
    {
        RockSongImporter importer;
        return project.import(file, importer);
    }

    if (extension == ".psarc")
    {
        PsarcSongImporter importer;
        return project.import(file, importer);
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
    const common::audio::PluginCandidate& candidate, const common::audio::PluginHandle& handle)
{
    return PluginViewState{
        .instance_id = handle.instance_id,
        .plugin_id = handle.plugin_id,
        .name = candidate.name,
        .manufacturer = candidate.manufacturer,
        .format_name = candidate.format_name,
        .chain_index = handle.chain_index,
    };
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
    struct ProjectWriteTaskState;
    struct ProjectLoadLiveRigStage;

    Impl(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IAudioDeviceConfiguration* audio_devices,
        common::audio::IPluginHost* plugin_host, common::audio::ILiveRig* live_rig,
        EditorController::Services services);
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
    void onPlayPausePressed();
    void onStopPressed();
    void onWaveformClicked(double normalized_x);
    void onAddPluginRequested(std::filesystem::path file);
    void onRemovePluginRequested(std::string instance_id);
    void onOpenPluginRequested(std::string instance_id);
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
    void performActionImpl(const EditorAction::AddPlugin& action);
    void performActionImpl(const EditorAction::RemovePlugin& action);
    void performActionImpl(const EditorAction::OpenPlugin& action);
    [[nodiscard]] bool canRunAction(EditorAction::Id action) const;
    [[nodiscard]] bool actionAvailableWhenIdle(EditorAction::Id action) const;
    [[nodiscard]] static ActionBusyPolicy actionBusyPolicy(EditorAction::Id action) noexcept;

    void requestProjectAction(EditorAction::ProjectAction action);
    void runProjectAction(EditorAction::ProjectAction action);
    void openProject(const std::filesystem::path& file, bool clear_last_open_project_on_failure);
    void completeOpenProject(std::uint64_t token, const std::shared_ptr<OpenTaskState>& state);
    void finishOpenProjectAfterLiveRigLoad(
        const std::shared_ptr<OpenTaskState>& state, const ProjectEditorState& editor_state,
        std::expected<void, std::string> rig_result);
    void importSongSource(const std::filesystem::path& file);
    void completeImportSongSource(
        std::uint64_t token, const std::shared_ptr<ImportTaskState>& state);
    void finishImportSongSourceAfterLiveRigLoad(
        const std::shared_ptr<ImportTaskState>& state, std::expected<void, std::string> rig_result);
    void completeAddPluginScan(
        std::uint64_t token, const std::shared_ptr<AddPluginTaskState>& state);
    void completeAddPluginLoad(
        std::uint64_t token, const std::shared_ptr<AddPluginTaskState>& state);
    [[nodiscard]] bool closeProject();
    [[nodiscard]] std::shared_ptr<ProjectWriteTaskState> takeProjectForWrite(
        EditorAction::ProjectWriteAction action);
    [[nodiscard]] std::expected<void, std::string> captureLiveRigIntoSong(
        common::core::Song& song, const Project& project);
    void runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state);
    void startLiveRigLoadStage(ProjectLoadLiveRigStage stage_state, bool report_progress);
    void restoreLiveRig(
        const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
        std::function<void(std::expected<std::vector<PluginViewState>, std::string>)> on_loaded);
    void clearLiveRig();
    void runProjectWriteAction(EditorAction::ProjectWriteAction&& action);
    void completeProjectWriteAction(
        std::uint64_t token, const std::shared_ptr<ProjectWriteTaskState>& state);
    void continueDeferredAction();
    void clearDeferredAction() noexcept;
    [[nodiscard]] ProjectEditorState projectEditorStateForSave() const;
    [[nodiscard]] bool loadSessionSong(
        common::core::Song song, const std::optional<std::string>& selected_arrangement);
    [[nodiscard]] EditorViewState deriveViewState() const;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] std::uint64_t beginBusy(BusyOperation operation);
    void finishBusyOperation();
    void supersedeBusyOperation();
    void endBusy();
    void setLiveRigLoadBusyState(std::string&& message, double progress);
    void beginLiveRigLoadProgress();
    void updateLiveRigLoadProgress(const common::audio::LiveRigLoadProgress& progress);
    void runAfterBusyOverlayPaintedOrNow(std::function<void()>&& callback);
    void restoreAudioDeviceState();
    void persistAudioDeviceState();
    void updateView();
    void reportError(const std::string& message);
    [[nodiscard]] bool hasLoadedArrangement() const;
    [[nodiscard]] bool shouldShowLiveRigLoadProgress() const;
    [[nodiscard]] bool hasLiveRigPersistence() const noexcept;
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;
    [[nodiscard]] bool canStopTransport(const common::audio::TransportState& transport_state) const;

    // Transport port used for control intents and coarse listener delivery.
    common::audio::ITransport& m_transport;

    // Audio port used for project audio validation and selected-arrangement loading.
    common::audio::IAudio& m_audio;

    // Optional audio-device port used for ASIO input/output routing.
    common::audio::IAudioDeviceConfiguration* m_audio_devices{};

    // Optional plugin-host port used to mutate the processing chain.
    common::audio::IPluginHost* m_plugin_host{};

    // Optional live rig port used to persist and restore arrangement-owned plugin state.
    common::audio::ILiveRig* m_live_rig{};

    // Song aggregate and selected arrangement state currently loaded in the editor.
    common::core::Session m_session;

    // Project IO and host-exit seams supplied by production composition or tests.
    EditorController::OpenFunction m_open_function;
    EditorController::ImportFunction m_import_function;
    EditorController::SaveFunction m_save_function;
    EditorController::SaveAsFunction m_save_as_function;
    EditorController::PublishFunction m_publish_function;
    EditorController::ExitFunction m_exit_function;

    // Optional app-local settings used to restore startup state and persist exit state.
    EditorSettings* m_settings;

    // Non-owning view binding installed by attachView(); null before the first attachment.
    IEditorView* m_view{nullptr};

    // Most recently derived view state used as the seed push at view attachment.
    EditorViewState m_last_state{};

    // Runtime plugin chain shown by the view and refreshed from the live rig boundary.
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

    // Project-lifecycle action waiting for either unsaved-change confirmation or a Save As path.
    std::optional<EditorAction::ProjectAction> m_deferred_action{};

    // True while the view should present an unsaved-changes prompt.
    bool m_unsaved_changes_prompt_visible{false};

    // True while the view should present a Save As chooser for a deferred command.
    bool m_save_as_prompt_visible{false};

    // Active busy state pushed to the view; empty while no slow operation is in flight.
    std::optional<BusyViewState> m_busy{};

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

// Per-operation worker state for AddPlugin discovery. The slow Tracktion plugin scan runs on a
// worker so the busy overlay stays responsive. Actual chain mutation happens on the message thread
// in the completion callback because Tracktion requires it.
struct EditorController::Impl::AddPluginTaskState
{
    std::filesystem::path file{};
    std::expected<std::vector<common::audio::PluginCandidate>, common::audio::PluginHostError>
        candidates{};
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

// Subscribes for coarse transport transitions and captures an initial derived state with
// audio-device routing available.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices, EditorController::Services services)
    : EditorController(transport, audio, &audio_devices, nullptr, nullptr, std::move(services))
{}

// Subscribes for coarse transport transitions and captures an initial derived state with plugin
// hosting available.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, EditorController::Services services)
    : EditorController(transport, audio, &audio_devices, &plugin_host, nullptr, std::move(services))
{}

// Subscribes with plugin hosting and persistent tone storage available.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    EditorController::Services services)
    : EditorController(
          transport, audio, &audio_devices, &plugin_host, &live_rig, std::move(services))
{}

// Builds a controller for tests and temporary hosts that do not provide audio-device routing.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    EditorController::Services services)
    : EditorController(transport, audio, nullptr, nullptr, nullptr, std::move(services))
{}

// Builds a controller for hosts that expose plugin hosting but not audio-device settings UI.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IPluginHost& plugin_host, EditorController::Services services)
    : EditorController(transport, audio, nullptr, &plugin_host, nullptr, std::move(services))
{}

// Builds a controller for hosts that expose plugin hosting and tone storage without device UI.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    EditorController::Services services)
    : EditorController(transport, audio, nullptr, &plugin_host, &live_rig, std::move(services))
{}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where a service seam is omitted.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration* audio_devices,
    common::audio::IPluginHost* plugin_host, common::audio::ILiveRig* live_rig,
    EditorController::Services services)
    : m_impl(
          std::make_unique<Impl>(
              transport, audio, audio_devices, plugin_host, live_rig, std::move(services)))
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

void EditorController::onAddPluginRequested(std::filesystem::path file)
{
    m_impl->onAddPluginRequested(std::move(file));
}

void EditorController::onRemovePluginRequested(std::string instance_id)
{
    m_impl->onRemovePluginRequested(std::move(instance_id));
}

void EditorController::onOpenPluginRequested(std::string instance_id)
{
    m_impl->onOpenPluginRequested(std::move(instance_id));
}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where a service seam is omitted.
EditorController::Impl::Impl(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration* audio_devices,
    common::audio::IPluginHost* plugin_host, common::audio::ILiveRig* live_rig,
    EditorController::Services services)
    : m_transport(transport)
    , m_audio(audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_live_rig(live_rig)
    , m_open_function(
          services.open_function ? std::move(services.open_function)
                                 : EditorController::OpenFunction{defaultOpen})
    , m_import_function(
          services.import_function ? std::move(services.import_function)
                                   : EditorController::ImportFunction{defaultImport})
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
    if (m_audio_devices != nullptr)
    {
        common::audio::IAudioDeviceConfiguration::Listener& self_as_listener = *this;
        m_audio_device_listener = std::make_unique<common::audio::ScopedListener<
            common::audio::IAudioDeviceConfiguration,
            common::audio::IAudioDeviceConfiguration::Listener>>(
            *m_audio_devices, self_as_listener);
        restoreAudioDeviceState();
    }
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
    auto state = std::make_shared<OpenTaskState>();
    state->file = file;
    state->clear_last_open_project_on_failure = clear_last_open_project_on_failure;
    const std::uint64_t token = beginBusy(BusyOperation::OpeningProject);
    updateView();

    std::weak_ptr<bool> completion_alive_source = m_alive;
    const EditorController::OpenFunction open_function = m_open_function;
    m_task_runner->submit(
        [state, worker_open_function = open_function] {
            state->result = worker_open_function(state->project, state->file);
        },
        [this, state, token, completion_alive = std::move(completion_alive_source)]() {
            if (completion_alive.expired())
            {
                return;
            }
            completeOpenProject(token, state);
        });
}

// Applies the worker's open result on the message thread. Discards stale completions whose busy
// token no longer matches the controller; otherwise commits the loaded song and clears busy.
void EditorController::Impl::completeOpenProject(
    std::uint64_t token, const std::shared_ptr<OpenTaskState>& state)
{
    if (token != m_current_busy_token)
    {
        return;
    }

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        updateView();
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);
    ProjectEditorState editor_state = state->project.editorState();

    if (!loadSessionSong(std::move(song), editor_state.selected_arrangement))
    {
        finishBusyOperation();
        updateView();
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        reportError(std::string{"Could not load audio from: "} + state->file.string());
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
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
    if (!rig_result.has_value())
    {
        m_transport.stop();
        m_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        finishBusyOperation();
        updateView();
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        reportError(
            std::string{"Could not load live rig from: "} + state->file.string() + ": " +
            rig_result.error());
        return;
    }

    m_project = std::move(state->project);
    m_project_file = state->file;
    m_transport.seek(session().timeline().clamp(editor_state.cursor_position));
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    clearDeferredAction();
    finishBusyOperation();

    // The single view update below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    updateView();
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
    auto state = std::make_shared<ImportTaskState>();
    state->file = file;
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    updateView();

    std::weak_ptr<bool> completion_alive_source = m_alive;
    const EditorController::ImportFunction import_function = m_import_function;
    m_task_runner->submit(
        [state, worker_import_function = import_function] {
            state->result = worker_import_function(state->project, state->file);
        },
        [this, state, token, completion_alive = std::move(completion_alive_source)]() {
            if (completion_alive.expired())
            {
                return;
            }
            completeImportSongSource(token, state);
        });
}

// Applies the worker's import result on the message thread. Discards stale completions; on
// success commits the imported song into the session and marks the workspace unsaved.
void EditorController::Impl::completeImportSongSource(
    std::uint64_t token, const std::shared_ptr<ImportTaskState>& state)
{
    if (token != m_current_busy_token)
    {
        return;
    }

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        updateView();
        reportError(std::string{"Could not import: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    if (!loadSessionSong(std::move(song), std::nullopt))
    {
        finishBusyOperation();
        updateView();
        reportError(std::string{"Could not load imported audio from: "} + state->file.string());
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
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
    if (!rig_result.has_value())
    {
        m_transport.stop();
        m_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        finishBusyOperation();
        updateView();
        reportError(
            std::string{"Could not load imported live rig from: "} + state->file.string() + ": " +
            rig_result.error());
        return;
    }

    m_project = std::move(state->project);
    m_project_file.clear();
    m_save_requires_destination = true;
    m_has_unsaved_changes = true;
    clearDeferredAction();
    finishBusyOperation();

    // The single view update below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    updateView();
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
    std::weak_ptr<bool> stage_alive_source = m_alive;
    runAfterBusyOverlayPaintedOrNow([this,
                                     captured_stage = std::move(stage_state),
                                     stage_alive = std::move(stage_alive_source)]() mutable {
        if (stage_alive.expired())
        {
            return;
        }
        startLiveRigLoadStage(std::move(captured_stage), true);
    });
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
    std::weak_ptr<bool> load_alive_source = m_alive;
    restoreLiveRig(
        song_directory,
        report_progress,
        token,
        [this,
         token,
         report_progress,
         captured_stage = std::move(stage_state),
         load_alive = std::move(load_alive_source)](
            std::expected<std::vector<PluginViewState>, std::string> rig_result) mutable {
            if (load_alive.expired() || token != m_current_busy_token)
            {
                return;
            }
            if (!rig_result.has_value())
            {
                captured_stage.finish(std::unexpected{std::move(rig_result.error())});
                return;
            }

            m_plugins = std::move(*rig_result);
            if (!report_progress)
            {
                captured_stage.finish({});
                return;
            }

            setLiveRigLoadBusyState("Live rig loaded.", 1.0);
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
                [this,
                 token,
                 timer_stage = std::move(captured_stage),
                 timer_alive = std::move(load_alive)]() mutable {
                    if (timer_alive.expired() || token != m_current_busy_token)
                    {
                        return;
                    }

                    timer_stage.finish({});
                });
        });
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

// Ignores the intent until audio activation has committed an arrangement, otherwise toggles
// playback.
void EditorController::Impl::onPlayPausePressed()
{
    runAction(EditorAction::PlayPause{});
}

// Mirrors the published stop_enabled gate so the keyboard or alternate input paths cannot stop a
// transport the view considers already reset.
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

// Handles the first plugin UI flow: scan one selected VST3 file, append the first discovered
// candidate, and publish enough state for the panel to show the linear chain.
void EditorController::Impl::onAddPluginRequested(std::filesystem::path file)
{
    runAction(EditorAction::AddPlugin{std::move(file)});
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

// Persists the new device manager state and re-derives view state after a configuration change.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
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
            m_has_unsaved_changes = false;
            m_save_requires_destination = false;
            if (deferred_id != EditorAction::Id::CloseProject)
            {
                EditorAction::ProjectAction replay = std::move(*m_deferred_action);
                clearDeferredAction();
                if (closeProject())
                {
                    runProjectAction(std::move(replay));
                }
                return;
            }

            continueDeferredAction();
            break;
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

// Offloads the slow Tracktion plugin scan to the editor task runner so the busy overlay stays
// responsive while the file is inspected. Actual chain mutation resumes on the message thread
// in the completion callback because Tracktion plugin insertion requires it.
void EditorController::Impl::performActionImpl(const EditorAction::AddPlugin& action)
{
    if (m_plugin_host == nullptr || !hasLoadedArrangement())
    {
        return;
    }

    auto state = std::make_shared<AddPluginTaskState>();
    state->file = action.file;
    const std::uint64_t token = beginBusy(BusyOperation::LoadingPlugin);
    updateView();

    std::weak_ptr<bool> completion_alive_source = m_alive;
    common::audio::IPluginHost* const plugin_host = m_plugin_host;
    m_task_runner->submit(
        [state, plugin_host] { state->candidates = plugin_host->scanPluginFile(state->file); },
        [this, state, token, completion_alive = std::move(completion_alive_source)]() {
            if (completion_alive.expired())
            {
                return;
            }
            completeAddPluginScan(token, state);
        });
}

// Applies the scan result on the message thread before final chain mutation.
void EditorController::Impl::completeAddPluginScan(
    std::uint64_t token, const std::shared_ptr<AddPluginTaskState>& state)
{
    if (token != m_current_busy_token)
    {
        return;
    }

    if (!state->candidates.has_value())
    {
        const std::string message = state->candidates.error().message;
        finishBusyOperation();
        updateView();
        reportError(std::string{"Could not scan plugin: "} + message);
        return;
    }

    if (state->candidates->empty())
    {
        finishBusyOperation();
        updateView();
        reportError("Could not scan plugin: no compatible plugin was found");
        return;
    }

    std::weak_ptr<bool> completion_alive_source = m_alive;
    runAfterBusyOverlayPaintedOrNow(
        [this, state, token, completion_alive = std::move(completion_alive_source)]() {
            if (completion_alive.expired())
            {
                return;
            }
            completeAddPluginLoad(token, state);
        });
}

// Inserts the already-scanned plugin candidate into the live chain.
void EditorController::Impl::completeAddPluginLoad(
    std::uint64_t token, const std::shared_ptr<AddPluginTaskState>& state)
{
    if (token != m_current_busy_token || !state->candidates.has_value() ||
        state->candidates->empty())
    {
        return;
    }

    const common::audio::PluginCandidate& candidate = state->candidates->front();
    const auto handle = m_plugin_host->addPlugin(candidate.id);
    if (!handle.has_value())
    {
        const std::string message = handle.error().message;
        finishBusyOperation();
        updateView();
        reportError(std::string{"Could not add plugin: "} + message);
        return;
    }

    m_plugins.push_back(makePluginViewState(candidate, *handle));
    if (hasLiveRigPersistence())
    {
        m_has_unsaved_changes = true;
    }

    finishBusyOperation();
    updateView();
}

void EditorController::Impl::performActionImpl(const EditorAction::RemovePlugin& action)
{
    if (m_plugin_host == nullptr || !hasLoadedArrangement())
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

    const auto result = m_plugin_host->removePlugin(action.instance_id);
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
    if (m_plugin_host == nullptr || !hasLoadedArrangement())
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

    const auto result = m_plugin_host->openPluginWindow(action.instance_id);
    if (!result.has_value())
    {
        reportError(std::string{"Could not open plugin: "} + result.error().message);
    }
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
        case EditorAction::Id::AddPlugin:
        {
            return m_plugin_host != nullptr && hasLoadedArrangement();
        }
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::OpenPlugin:
        {
            return m_plugin_host != nullptr && hasLoadedArrangement() && !m_plugins.empty();
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
                if (closeProject())
                {
                    clearDeferredAction();
                    updateView();
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
                    currentProjectFile();
                if (closeProject())
                {
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
    if (!m_project.has_value())
    {
        if (hasLoadedArrangement())
        {
            m_transport.stop();
        }
        clearLiveRig();
        m_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        m_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        return true;
    }

    m_transport.stop();
    clearLiveRig();
    m_audio.clearActiveArrangement();
    m_session.reset();
    m_plugins.clear();

    auto closed = closeExistingProject(m_project);
    if (!closed.has_value())
    {
        reportError(std::string{"Could not close: "} + closed.error().message);
        m_project.reset();
        m_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        updateView();
        return false;
    }

    m_project.reset();
    m_project_file.clear();
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
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
    if (m_live_rig == nullptr)
    {
        return {};
    }

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

    auto snapshot = m_live_rig->captureActiveRig(
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
    return {};
}

// Restores the selected arrangement's saved tone document after the backing audio is active.
// Live rig restore runs cooperatively on the message thread inside the audio adapter, so this
// method always returns immediately and routes the plugin view-state result through the on_loaded
// callback without mutating controller state.
void EditorController::Impl::restoreLiveRig(
    const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
    std::function<void(std::expected<std::vector<PluginViewState>, std::string>)> on_loaded)
{
    if (!on_loaded)
    {
        return;
    }

    if (m_live_rig == nullptr)
    {
        on_loaded(std::vector<PluginViewState>{});
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        on_loaded(std::vector<PluginViewState>{});
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
        std::weak_ptr<bool> progress_alive_source = m_alive;
        request.progress_callback =
            [this, token, progress_alive = std::move(progress_alive_source)](
                const common::audio::LiveRigLoadProgress& progress) {
                if (progress_alive.expired() || token != m_current_busy_token)
                {
                    return;
                }
                updateLiveRigLoadProgress(progress);
            };
        // Route the engine's per-step yield through the busy-overlay paint fence so each plugin's
        // progress update actually paints before the next step blocks the message thread.
        std::weak_ptr<bool> yield_alive_source = m_alive;
        request.yield_callback =
            [this, token, yield_alive = std::move(yield_alive_source)](std::function<void()> next) {
                if (yield_alive.expired() || token != m_current_busy_token)
                {
                    return;
                }
                runAfterBusyOverlayPaintedOrNow(
                    [this, token, continuation = std::move(next), yield_alive]() mutable {
                        if (yield_alive.expired() || token != m_current_busy_token)
                        {
                            return;
                        }
                        if (continuation)
                        {
                            continuation();
                        }
                    });
            };
    }

    std::weak_ptr<bool> load_alive_source = m_alive;
    m_live_rig->loadRig(
        std::move(request),
        [load_alive = std::move(load_alive_source), completion = std::move(on_loaded)](
            std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> loaded) {
            if (load_alive.expired())
            {
                return;
            }

            if (!loaded.has_value())
            {
                completion(std::unexpected{loaded.error().message});
                return;
            }

            completion(makePluginViewStates(loaded->plugins));
        });
}

// Clears the audio backend's live rig chain as part of project teardown.
void EditorController::Impl::clearLiveRig()
{
    if (m_live_rig == nullptr)
    {
        return;
    }

    const auto cleared = m_live_rig->clearRig();
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

    const std::uint64_t token = beginBusy(busyOperationForProjectWrite(idOf(state->action)));
    updateView();

    std::weak_ptr<bool> completion_alive_source = m_alive;
    const EditorController::SaveFunction save_function = m_save_function;
    const EditorController::SaveAsFunction save_as_function = m_save_as_function;
    const EditorController::PublishFunction publish_function = m_publish_function;
    m_task_runner->submit(
        [state,
         worker_save_function = save_function,
         worker_save_as_function = save_as_function,
         worker_publish_function = publish_function] {
            std::visit(
                [&state, &worker_save_function, &worker_save_as_function, &worker_publish_function](
                    auto&& alternative) {
                    using A = std::decay_t<decltype(alternative)>;
                    if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
                    {
                        state->result =
                            worker_save_function(state->project, state->song, state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
                    {
                        state->result = worker_save_as_function(
                            state->project, alternative.file, state->song, state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
                    {
                        state->result =
                            worker_publish_function(state->project, alternative.file, state->song);
                    }
                },
                state->action);
        },
        [this, state, token, completion_alive = std::move(completion_alive_source)]() {
            if (completion_alive.expired())
            {
                return;
            }
            completeProjectWriteAction(token, state);
        });
}

// Restores the Project context, clears busy before errors, and applies the successful write's
// action-specific state transition on the message thread.
void EditorController::Impl::completeProjectWriteAction(
    std::uint64_t token, const std::shared_ptr<ProjectWriteTaskState>& state)
{
    if (token != m_current_busy_token)
    {
        return;
    }

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
        updateView();
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
        return;
    }

    updateView();
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

// Restores a settings-backed project and clears stale restore state when the path cannot load.
void EditorController::Impl::restoreLastOpenProject()
{
    if (m_settings == nullptr)
    {
        return;
    }

    const std::optional<std::filesystem::path> project_file = m_settings->lastOpenProject();
    if (!project_file.has_value())
    {
        return;
    }

    if (!projectFileExists(*project_file))
    {
        m_settings->setLastOpenProject(std::nullopt);
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

    if (!m_audio.prepareSong(song))
    {
        return false;
    }

    const std::size_t selected_index = getSelectedArrangementIndex(song, selected_arrangement);
    m_session_load_in_progress = true;
    const bool active_arrangement_set =
        m_audio.setActiveArrangement(song.arrangements[selected_index]);
    bool committed = false;
    if (active_arrangement_set)
    {
        committed = m_session.loadSong(std::move(song), selected_index);
        assert(committed && "Session rejected backend-accepted project song");
        m_plugins.clear();
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
    state.play_pause_enabled = canRunAction(EditorAction::Id::PlayPause);
    state.stop_enabled = canRunAction(EditorAction::Id::Stop);
    state.play_pause_shows_pause_icon = transport_state.playing;
    state.audio_devices_available = m_audio_devices != nullptr;
    state.current_audio_device_name =
        m_audio_devices != nullptr ? m_audio_devices->currentDeviceName() : std::nullopt;
    state.visible_timeline = timeline_range;
    state.signal_chain = SignalChainViewState{
        .add_plugin_enabled = canRunAction(EditorAction::Id::AddPlugin),
        .remove_plugins_enabled = canRunAction(EditorAction::Id::RemovePlugin),
        .plugins = m_plugins,
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

    state.busy = m_busy;

    return state;
}

// Applies the serialized audio-device state stored by a previous editor session, if any.
void EditorController::Impl::restoreAudioDeviceState()
{
    if (m_audio_devices == nullptr || m_settings == nullptr)
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

    m_audio_devices->deviceManager().initialise(1, 2, xml.get(), true);
}

// Stores the current device manager state so the next launch can restore the user's selection.
void EditorController::Impl::persistAudioDeviceState()
{
    if (m_audio_devices == nullptr || m_settings == nullptr)
    {
        return;
    }

    const std::unique_ptr<juce::XmlElement> xml = m_audio_devices->deviceManager().createStateXml();
    if (xml == nullptr)
    {
        m_settings->setAudioDeviceState(std::nullopt);
        return;
    }

    m_settings->setAudioDeviceState(xml->toString().toStdString());
}

// Caches the derived state as the seed for future attachView() pushes and forwards it to the
// currently attached view if any.
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
    return m_live_rig != nullptr;
}

// Reports whether a busy operation is currently active.
bool EditorController::Impl::isBusy() const noexcept
{
    return m_busy.has_value();
}

// Begins a busy operation, advances the current busy token, and fills the default message from the
// central busyMessage() helper so every entry point produces consistent overlay copy.
std::uint64_t EditorController::Impl::beginBusy(BusyOperation operation)
{
    m_current_busy_token += 1;
    m_busy = BusyViewState{
        .operation = operation,
        .message = busyMessage(operation),
        .presentation = busyPresentation(operation),
        .cancel_enabled = false,
    };
    return m_current_busy_token;
}

// Semantic wrapper for normal operation completion. Completion paths call this only after their
// captured busy token has already matched the current busy token.
void EditorController::Impl::finishBusyOperation()
{
    endBusy();
}

// Semantic wrapper for Close/Exit-style commands that intentionally take over the busy lifecycle.
void EditorController::Impl::supersedeBusyOperation()
{
    endBusy();
}

// Clears busy state and advances the current busy token. This makes any in-flight worker completion
// stale whether this is a normal finish or a superseding action takeover.
void EditorController::Impl::endBusy()
{
    m_busy.reset();
    m_current_busy_token += 1;
}

// Writes the current live rig load message and fraction into the active busy overlay state.
void EditorController::Impl::setLiveRigLoadBusyState(std::string&& message, double progress)
{
    if (!m_busy.has_value())
    {
        return;
    }

    m_busy->operation = BusyOperation::LoadingLiveRig;
    m_busy->message = std::move(message);
    m_busy->presentation = busyPresentation(BusyOperation::LoadingLiveRig);
    m_busy->progress = progress;
}

// Switches a project-load busy operation into determinate live rig restore progress and pushes
// the new state so the overlay paints at 0% before any plugin construction starts.
void EditorController::Impl::beginLiveRigLoadProgress()
{
    setLiveRigLoadBusyState(busyMessage(BusyOperation::LoadingLiveRig), 0.0);
    updateView();
}

// Applies plugin-level progress reported by the live rig boundary to the busy overlay state.
void EditorController::Impl::updateLiveRigLoadProgress(
    const common::audio::LiveRigLoadProgress& progress)
{
    setLiveRigLoadBusyState(liveRigProgressMessage(progress), liveRigProgressFraction(progress));
    updateView();
}

// Runs message-thread-only load work after the busy overlay has had a chance to repaint.
void EditorController::Impl::runAfterBusyOverlayPaintedOrNow(std::function<void()>&& callback)
{
    if (!callback)
    {
        return;
    }

    if (m_view == nullptr)
    {
        callback();
        return;
    }

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
