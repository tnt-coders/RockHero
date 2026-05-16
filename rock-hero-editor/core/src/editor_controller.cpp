#include "editor_controller.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <expected>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/editor/core/psarc_song_importer.h>
#include <rock_hero/editor/core/rock_song_importer.h>
#include <string>
#include <system_error>
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

// Production exit fallback used when a composition host does not provide an exit callback.
void defaultExit()
{}

// Uses the real filesystem to avoid adding another startup-restore callback seam.
[[nodiscard]] bool projectFileExists(const std::filesystem::path& project_file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(project_file, error);
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

} // namespace

// Per-operation worker state for openProject(). The worker thread writes the result; the
// message-thread completion reads it. Held through a shared_ptr captured by both lambdas so the
// Project's workspace files stay alive until either side is done with them.
struct EditorController::OpenTaskState
{
    std::filesystem::path file{};
    bool clear_last_open_project_on_failure{false};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Per-operation worker state for importSongSource(). Same shared_ptr ownership pattern as
// OpenTaskState.
struct EditorController::ImportTaskState
{
    std::filesystem::path file{};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Builds an open-project action carrying the chosen package path.
EditorController::EditorAction EditorController::EditorAction::openProject(
    std::filesystem::path file)
{
    return EditorAction{Id::OpenProject, std::move(file)};
}

// Builds a startup-restore action carrying the persisted package path.
EditorController::EditorAction EditorController::EditorAction::restoreProject(
    std::filesystem::path file)
{
    return EditorAction{Id::RestoreProject, std::move(file)};
}

// Builds an import action carrying the chosen source path.
EditorController::EditorAction EditorController::EditorAction::importProject(
    std::filesystem::path file)
{
    return EditorAction{Id::ImportProject, std::move(file)};
}

// Builds a direct save action.
EditorController::EditorAction EditorController::EditorAction::saveProject() noexcept
{
    return EditorAction{Id::SaveProject};
}

// Builds a Save As action carrying the chosen package path.
EditorController::EditorAction EditorController::EditorAction::saveProjectAs(
    std::filesystem::path file)
{
    return EditorAction{Id::SaveProjectAs, std::move(file)};
}

// Builds a publish action carrying the chosen native song package path.
EditorController::EditorAction EditorController::EditorAction::publishProject(
    std::filesystem::path file)
{
    return EditorAction{Id::PublishProject, std::move(file)};
}

// Builds a close-project action.
EditorController::EditorAction EditorController::EditorAction::closeProject() noexcept
{
    return EditorAction{Id::CloseProject};
}

// Builds an exit-application action.
EditorController::EditorAction EditorController::EditorAction::exitApplication() noexcept
{
    return EditorAction{Id::ExitApplication};
}

// Builds an unsaved-changes prompt resolution action.
EditorController::EditorAction EditorController::EditorAction::resolveUnsavedChangesPrompt(
    UnsavedChangesDecision decision) noexcept
{
    return EditorAction{Id::ResolveUnsavedChangesPrompt, decision};
}

// Builds a Save As cancellation action for a controller-requested chooser.
EditorController::EditorAction EditorController::EditorAction::cancelSaveAsPrompt() noexcept
{
    return EditorAction{Id::CancelSaveAsPrompt};
}

// Builds a transport play/pause action.
EditorController::EditorAction EditorController::EditorAction::playPause() noexcept
{
    return EditorAction{Id::PlayPause};
}

// Builds a transport stop action.
EditorController::EditorAction EditorController::EditorAction::stop() noexcept
{
    return EditorAction{Id::Stop};
}

// Builds a waveform seek action carrying the normalized click coordinate.
EditorController::EditorAction EditorController::EditorAction::seekWaveform(
    double normalized_x) noexcept
{
    return EditorAction{Id::SeekWaveform, normalized_x};
}

// Builds an add-plugin action carrying the chosen plugin path.
EditorController::EditorAction EditorController::EditorAction::addPlugin(std::filesystem::path file)
{
    return EditorAction{Id::AddPlugin, std::move(file)};
}

// Stores the action id for payload-free actions.
EditorController::EditorAction::EditorAction(Id id) noexcept
    : m_id(id)
{}

// Stores the action id and path payload.
EditorController::EditorAction::EditorAction(Id id, std::filesystem::path file)
    : m_id(id)
    , m_file(std::move(file))
{}

// Stores the action id and unsaved-changes decision payload.
EditorController::EditorAction::EditorAction(Id id, UnsavedChangesDecision decision) noexcept
    : m_id(id)
    , m_decision(decision)
{}

// Stores the action id and normalized waveform coordinate payload.
EditorController::EditorAction::EditorAction(Id id, double normalized_x) noexcept
    : m_id(id)
    , m_normalized_x(normalized_x)
{}

// Returns the action identity used by controller policy.
EditorController::EditorAction::Id EditorController::EditorAction::id() const noexcept
{
    return m_id;
}

// Returns the prompt decision payload for ResolveUnsavedChangesPrompt.
UnsavedChangesDecision EditorController::EditorAction::decision() const noexcept
{
    return m_decision;
}

// Returns the normalized waveform coordinate payload for SeekWaveform.
double EditorController::EditorAction::normalizedX() const noexcept
{
    return m_normalized_x;
}

// Moves the path payload out of file-backed actions.
std::filesystem::path EditorController::EditorAction::takeFile() noexcept
{
    return std::move(m_file);
}

// Builds an open-project command carrying the package path.
EditorController::ProjectCommand EditorController::ProjectCommand::open(std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::Open, std::move(file)};
}

// Builds an import command carrying the source path.
EditorController::ProjectCommand EditorController::ProjectCommand::importSong(
    std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::Import, std::move(file)};
}

// Builds a close-project command.
EditorController::ProjectCommand EditorController::ProjectCommand::close() noexcept
{
    return ProjectCommand{ProjectCommandId::Close};
}

// Builds an exit-application command.
EditorController::ProjectCommand EditorController::ProjectCommand::exit() noexcept
{
    return ProjectCommand{ProjectCommandId::Exit};
}

// Stores the project command id for payload-free commands.
EditorController::ProjectCommand::ProjectCommand(ProjectCommandId id) noexcept
    : m_id(id)
{}

// Stores the project command id and path payload.
EditorController::ProjectCommand::ProjectCommand(ProjectCommandId id, std::filesystem::path file)
    : m_id(id)
    , m_file(std::move(file))
{}

// Returns the command identity used by prompt state and controller routing.
ProjectCommandId EditorController::ProjectCommand::id() const noexcept
{
    return m_id;
}

// Moves the path payload out of file-backed commands.
std::filesystem::path EditorController::ProjectCommand::takeFile() noexcept
{
    return std::move(m_file);
}

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
    : EditorController(transport, audio, &audio_devices, nullptr, std::move(services))
{}

// Subscribes for coarse transport transitions and captures an initial derived state with plugin
// hosting available.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, EditorController::Services services)
    : EditorController(transport, audio, &audio_devices, &plugin_host, std::move(services))
{}

// Builds a controller for tests and temporary hosts that do not provide audio-device routing.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    EditorController::Services services)
    : EditorController(transport, audio, nullptr, nullptr, std::move(services))
{}

// Builds a controller for hosts that expose plugin hosting but not audio-device settings UI.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IPluginHost& plugin_host, EditorController::Services services)
    : EditorController(transport, audio, nullptr, &plugin_host, std::move(services))
{}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where a service seam is omitted.
EditorController::EditorController(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration* audio_devices,
    common::audio::IPluginHost* plugin_host, EditorController::Services services)
    : m_transport(transport)
    , m_audio(audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_open_function(
          services.open_function ? std::move(services.open_function) : OpenFunction{defaultOpen})
    , m_import_function(
          services.import_function ? std::move(services.import_function)
                                   : ImportFunction{defaultImport})
    , m_save_function(
          services.save_function ? std::move(services.save_function) : SaveFunction{defaultSave})
    , m_save_as_function(
          services.save_as_function ? std::move(services.save_as_function)
                                    : SaveAsFunction{defaultSaveAs})
    , m_publish_function(
          services.publish_function ? std::move(services.publish_function)
                                    : PublishFunction{defaultPublish})
    , m_exit_function(
          services.exit_function ? std::move(services.exit_function) : ExitFunction{defaultExit})
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
EditorController::~EditorController()
{
    m_alive.reset();
}

// Stores the new view binding and immediately satisfies the "first push at attachment" contract
// using whatever state the controller has cached up to this point.
void EditorController::attachView(IEditorView& view)
{
    m_view = &view;
    view.setState(m_last_state);
}

// Opens an editor project package and stores it after audio and Session both accept the song.
void EditorController::onOpenRequested(std::filesystem::path file)
{
    runAction(EditorAction::openProject(std::move(file)));
}

// Opens an editor project package after any project-replacement prompt has been satisfied.
// Pushes busy state, dispatches package IO to the task runner, and returns immediately. The
// final commit runs in completeOpenProject() on the message thread once the worker reports the
// result.
void EditorController::openProject(
    const std::filesystem::path& file, bool clear_last_open_project_on_failure)
{
    auto state = std::make_shared<OpenTaskState>();
    state->file = file;
    state->clear_last_open_project_on_failure = clear_last_open_project_on_failure;
    const std::uint64_t token = beginBusy(BusyOperation::OpeningProject);
    deriveAndPush();

    std::weak_ptr<bool> alive_weak = m_alive;
    OpenFunction open_function = m_open_function;
    m_task_runner->submit(
        [state, open_function = std::move(open_function)]() mutable {
            state->result = open_function(state->project, state->file);
        },
        [this, state, token, alive_weak = std::move(alive_weak)]() {
            if (alive_weak.expired())
            {
                return;
            }
            completeOpenProject(token, state);
        });
}

// Applies the worker's open result on the message thread. Discards stale completions whose
// generation no longer matches the controller; otherwise commits the loaded song into the
// session and clears busy.
void EditorController::completeOpenProject(
    std::uint64_t token, const std::shared_ptr<OpenTaskState>& state)
{
    if (token != m_busy_generation)
    {
        return;
    }

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        deriveAndPush();
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);
    const ProjectEditorState editor_state = state->project.editorState();

    if (!loadSessionSong(std::move(song), editor_state.selected_arrangement))
    {
        finishBusyOperation();
        deriveAndPush();
        if (state->clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        reportError(std::string{"Could not load audio from: "} + state->file.string());
        return;
    }

    m_project = std::move(state->project);
    m_project_file = state->file;
    m_transport.seek(session().timeline().clamp(editor_state.cursor_position));
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    clearDeferredProjectCommand();
    finishBusyOperation();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    deriveAndPush();
}

// Imports a song source and stores the workspace only after audio and Session accept the song.
void EditorController::onImportRequested(std::filesystem::path file)
{
    runAction(EditorAction::importProject(std::move(file)));
}

// Imports a song source after any current project-replacement prompt has been satisfied. Same
// shape as openProject(): busy + worker dispatch here, commit in completeImportSongSource().
void EditorController::importSongSource(const std::filesystem::path& file)
{
    auto state = std::make_shared<ImportTaskState>();
    state->file = file;
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    deriveAndPush();

    std::weak_ptr<bool> alive_weak = m_alive;
    ImportFunction import_function = m_import_function;
    m_task_runner->submit(
        [state, import_function = std::move(import_function)]() mutable {
            state->result = import_function(state->project, state->file);
        },
        [this, state, token, alive_weak = std::move(alive_weak)]() {
            if (alive_weak.expired())
            {
                return;
            }
            completeImportSongSource(token, state);
        });
}

// Applies the worker's import result on the message thread. Discards stale completions; on
// success commits the imported song into the session and marks the workspace unsaved.
void EditorController::completeImportSongSource(
    std::uint64_t token, const std::shared_ptr<ImportTaskState>& state)
{
    if (token != m_busy_generation)
    {
        return;
    }

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        deriveAndPush();
        reportError(std::string{"Could not import: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    if (!loadSessionSong(std::move(song), std::nullopt))
    {
        finishBusyOperation();
        deriveAndPush();
        reportError(std::string{"Could not load imported audio from: "} + state->file.string());
        return;
    }

    m_project = std::move(state->project);
    m_project_file.clear();
    m_save_requires_destination = true;
    m_has_unsaved_changes = true;
    clearDeferredProjectCommand();
    finishBusyOperation();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    deriveAndPush();
}

// Saves to the current destination when one exists; Save As is responsible for destination choice.
void EditorController::onSaveRequested()
{
    runAction(EditorAction::saveProject());
}

// Saves to a chosen destination and promotes future Save commands to direct saves.
void EditorController::onSaveAsRequested(std::filesystem::path file)
{
    runAction(EditorAction::saveProjectAs(std::move(file)));
}

// Publishes the current project as a native song package without changing save destination or
// dirty state.
void EditorController::onPublishRequested(std::filesystem::path file)
{
    runAction(EditorAction::publishProject(std::move(file)));
}

// Cancels only a Save As chooser that was opened to continue a deferred project command.
void EditorController::onSaveAsCancelled()
{
    runAction(EditorAction::cancelSaveAsPrompt());
}

// Closes the current project after prompting for unsaved changes when needed.
void EditorController::onCloseRequested()
{
    runAction(EditorAction::closeProject());
}

// Exits through the composition host after prompting for unsaved changes when needed.
void EditorController::onExitRequested()
{
    runAction(EditorAction::exitApplication());
}

// Applies the user's unsaved-changes choice to the stored deferred project command.
void EditorController::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    runAction(EditorAction::resolveUnsavedChangesPrompt(decision));
}

// Ignores the intent until audio activation has committed an arrangement, otherwise toggles
// playback.
void EditorController::onPlayPausePressed()
{
    runAction(EditorAction::playPause());
}

// Mirrors the published stop_enabled gate so the keyboard or alternate input paths cannot stop a
// transport the view considers already reset.
void EditorController::onStopPressed()
{
    runAction(EditorAction::stop());
}

// Clamps the normalized input and converts it through the session timeline so the seek target
// stays inside the loaded content even when the view emits out-of-range values.
void EditorController::onWaveformClicked(double normalized_x)
{
    runAction(EditorAction::seekWaveform(normalized_x));
}

// Handles the first plugin UI flow: scan one selected VST3 file, append the first discovered
// candidate, and publish enough state for the panel to show the linear chain.
void EditorController::onAddPluginRequested(std::filesystem::path file)
{
    runAction(EditorAction::addPlugin(std::move(file)));
}

// Persists the new device manager state and re-derives view state after a configuration change.
void EditorController::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    deriveAndPush();
}

// Applies the central action gate and routes the accepted action.
void EditorController::runAction(EditorAction action)
{
    if (!prepareAction(action.id()))
    {
        return;
    }

    performAction(std::move(action));
}

// Applies availability and busy policy before an action mutates state or schedules work.
bool EditorController::prepareAction(EditorAction::Id action)
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

// Executes an accepted action. Payload access stays centralized here so controller entry points
// stay readable and do not hand-roll action bodies.
void EditorController::performAction(EditorAction action)
{
    switch (action.id())
    {
        case EditorAction::Id::OpenProject:
        {
            requestProjectCommand(ProjectCommand::open(action.takeFile()));
            break;
        }
        case EditorAction::Id::RestoreProject:
        {
            const std::filesystem::path file = action.takeFile();
            openProject(file, true);
            break;
        }
        case EditorAction::Id::ImportProject:
        {
            requestProjectCommand(ProjectCommand::importSong(action.takeFile()));
            break;
        }
        case EditorAction::Id::SaveProject:
        {
            if (m_save_requires_destination)
            {
                return;
            }

            if (!saveProject())
            {
                return;
            }

            if (m_pending_project_command.has_value())
            {
                continueDeferredProjectCommand();
                return;
            }

            deriveAndPush();
            break;
        }
        case EditorAction::Id::SaveProjectAs:
        {
            if (!m_project.has_value())
            {
                return;
            }

            std::filesystem::path file = action.takeFile();
            const auto saved =
                m_save_as_function(*m_project, file, session().song(), projectEditorStateForSave());
            if (!saved.has_value())
            {
                reportError(std::string{"Could not save as: "} + saved.error().message);
                deriveAndPush();
                return;
            }

            m_save_requires_destination = false;
            m_project_file = std::move(file);
            m_has_unsaved_changes = false;
            if (m_pending_project_command.has_value())
            {
                continueDeferredProjectCommand();
                return;
            }

            deriveAndPush();
            break;
        }
        case EditorAction::Id::PublishProject:
        {
            if (!m_project.has_value())
            {
                return;
            }

            const std::filesystem::path file = action.takeFile();
            const auto published = m_publish_function(*m_project, file, session().song());
            if (!published.has_value())
            {
                reportError(std::string{"Could not publish: "} + published.error().message);
                deriveAndPush();
                return;
            }

            deriveAndPush();
            break;
        }
        case EditorAction::Id::CloseProject:
        {
            requestProjectCommand(ProjectCommand::close());
            break;
        }
        case EditorAction::Id::ExitApplication:
        {
            requestProjectCommand(ProjectCommand::exit());
            break;
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            if (!m_pending_project_command.has_value())
            {
                clearDeferredProjectCommand();
                deriveAndPush();
                return;
            }

            m_unsaved_changes_prompt_visible = false;
            switch (action.decision())
            {
                case UnsavedChangesDecision::Save:
                {
                    if (m_save_requires_destination)
                    {
                        m_save_as_prompt_visible = true;
                        deriveAndPush();
                        return;
                    }

                    if (!saveProject())
                    {
                        clearDeferredProjectCommand();
                        return;
                    }

                    continueDeferredProjectCommand();
                    break;
                }
                case UnsavedChangesDecision::Discard:
                {
                    const ProjectCommandId command_id = m_pending_project_command->id();
                    m_has_unsaved_changes = false;
                    m_save_requires_destination = false;
                    if (command_id != ProjectCommandId::Close)
                    {
                        ProjectCommand command = std::move(*m_pending_project_command);
                        clearDeferredProjectCommand();
                        if (closeProject())
                        {
                            runProjectCommand(std::move(command));
                        }
                        return;
                    }

                    continueDeferredProjectCommand();
                    break;
                }
                case UnsavedChangesDecision::Cancel:
                {
                    clearDeferredProjectCommand();
                    deriveAndPush();
                    break;
                }
            }
            break;
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            if (!m_save_as_prompt_visible)
            {
                return;
            }

            clearDeferredProjectCommand();
            deriveAndPush();
            break;
        }
        case EditorAction::Id::PlayPause:
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
            break;
        }
        case EditorAction::Id::Stop:
        {
            const common::audio::TransportState transport_state = m_transport.state();
            if (!canStopTransport(transport_state))
            {
                return;
            }
            m_transport.stop();

            if (!transport_state.playing)
            {
                deriveAndPush();
            }
            break;
        }
        case EditorAction::Id::SeekWaveform:
        {
            const double normalized_x = action.normalizedX();
            const double clamped = std::clamp(normalized_x, 0.0, 1.0);
            const common::core::TimeRange timeline_range = session().timeline();
            const double target_seconds =
                timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
            m_transport.seek(timeline_range.clamp(common::core::TimePosition{target_seconds}));
            deriveAndPush();
            break;
        }
        case EditorAction::Id::AddPlugin:
        {
            if (m_plugin_host == nullptr || !hasLoadedArrangement())
            {
                return;
            }

            const std::filesystem::path file = action.takeFile();
            std::expected<
                std::vector<common::audio::PluginCandidate>,
                common::audio::PluginHostError>
                candidates = m_plugin_host->scanPluginFile(file);
            if (!candidates.has_value())
            {
                reportError(std::string{"Could not scan plugin: "} + candidates.error().message);
                deriveAndPush();
                return;
            }

            if (candidates->empty())
            {
                reportError("Could not scan plugin: no compatible plugin was found");
                deriveAndPush();
                return;
            }

            const common::audio::PluginCandidate& candidate = candidates->front();
            std::expected<common::audio::PluginHandle, common::audio::PluginHostError> handle =
                m_plugin_host->addPlugin(candidate.id);
            if (!handle.has_value())
            {
                reportError(std::string{"Could not add plugin: "} + handle.error().message);
                deriveAndPush();
                return;
            }

            // Tone persistence is not implemented yet, so the panel tracks the runtime chain
            // without marking the project dirty for data Save cannot currently restore.
            m_plugins.push_back(makePluginViewState(candidate, *handle));
            deriveAndPush();
            break;
        }
    }
}

// Combines natural action availability with the action's busy-state policy.
bool EditorController::canRunAction(EditorAction::Id action) const
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
bool EditorController::actionAvailableWhenIdle(EditorAction::Id action) const
{
    switch (action)
    {
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportProject:
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
            return m_pending_project_command.has_value() && m_unsaved_changes_prompt_visible;
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
    }

    return false;
}

// Encodes the small set of actions that intentionally do not follow normal busy blocking.
EditorController::ActionBusyPolicy EditorController::actionBusyPolicy(
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
        case EditorAction::Id::ImportProject:
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekWaveform:
        case EditorAction::Id::AddPlugin:
        {
            return ActionBusyPolicy::BlockedByBusy;
        }
    }

    return ActionBusyPolicy::BlockedByBusy;
}

// Coarse-only transport callback. During an in-flight session load, defer the push so the final
// derivation runs against the updated session and transport state instead of stale data.
void EditorController::onTransportStateChanged(common::audio::TransportState /*state*/)
{
    if (m_session_load_in_progress)
    {
        return;
    }
    deriveAndPush();
}

// Starts a project-level command or asks the view to confirm unsaved changes first.
void EditorController::requestProjectCommand(ProjectCommand command)
{
    if (hasUnsavedChanges())
    {
        m_pending_project_command = std::move(command);
        m_unsaved_changes_prompt_visible = true;
        m_save_as_prompt_visible = false;
        deriveAndPush();
        return;
    }

    runProjectCommand(std::move(command));
}

// Runs a project-level command once dirty-state gates have been satisfied.
void EditorController::runProjectCommand(ProjectCommand command)
{
    switch (command.id())
    {
        case ProjectCommandId::Close:
        {
            if (closeProject())
            {
                clearDeferredProjectCommand();
                deriveAndPush();
            }
            break;
        }
        case ProjectCommandId::Open:
        {
            const std::filesystem::path file = command.takeFile();
            openProject(file, false);
            break;
        }
        case ProjectCommandId::Import:
        {
            const std::filesystem::path file = command.takeFile();
            importSongSource(file);
            break;
        }
        case ProjectCommandId::Exit:
        {
            const std::optional<std::filesystem::path> restorable_project_file =
                currentProjectFile();
            if (closeProject())
            {
                clearDeferredProjectCommand();
                if (m_settings != nullptr)
                {
                    m_settings->setLastOpenProject(restorable_project_file);
                }
                deriveAndPush();
                m_exit_function();
            }
            break;
        }
    }
}

// Closes the current editor document across transport, backend audio, session, and workspace.
// Always supersedes any in-flight busy operation so closing or exiting during an open/import
// invalidates the worker's generation token; the worker's completion then sees a mismatch and
// discards itself rather than committing on top of a now-empty session.
bool EditorController::closeProject()
{
    if (!m_project.has_value())
    {
        m_audio.clearActiveArrangement();
        m_session.reset();
        m_plugins.clear();
        m_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        return true;
    }

    m_transport.stop();
    m_audio.clearActiveArrangement();
    m_session.reset();
    m_plugins.clear();

    auto closed = m_project->close();
    if (!closed.has_value())
    {
        reportError(std::string{"Could not close: "} + closed.error().message);
        m_project.reset();
        m_project_file.clear();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        deriveAndPush();
        return false;
    }

    m_project.reset();
    m_project_file.clear();
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    return true;
}

// Saves through the current destination and clears dirty state only after persistence succeeds.
bool EditorController::saveProject()
{
    if (!m_project.has_value())
    {
        return false;
    }

    const auto saved = m_save_function(*m_project, session().song(), projectEditorStateForSave());
    if (!saved.has_value())
    {
        reportError(std::string{"Could not save: "} + saved.error().message);
        deriveAndPush();
        return false;
    }

    m_has_unsaved_changes = false;
    return true;
}

// Resumes a deferred command after Save or Save As has successfully protected user changes.
void EditorController::continueDeferredProjectCommand()
{
    if (!m_pending_project_command.has_value())
    {
        deriveAndPush();
        return;
    }

    ProjectCommand command = std::move(*m_pending_project_command);
    clearDeferredProjectCommand();
    runProjectCommand(std::move(command));
}

// Clears all prompt-related state without changing the currently loaded project.
void EditorController::clearDeferredProjectCommand() noexcept
{
    m_pending_project_command.reset();
    m_unsaved_changes_prompt_visible = false;
    m_save_as_prompt_visible = false;
}

// Returns the controller-owned editor session through the read-only access boundary.
const common::core::Session& EditorController::session() const noexcept
{
    return m_session;
}

// Returns an editor project file only when the current workspace is backed by one.
std::optional<std::filesystem::path> EditorController::currentProjectFile() const
{
    if (!m_project.has_value() || m_project_file.empty())
    {
        return std::nullopt;
    }

    return m_project_file;
}

// Restores a settings-backed project and clears stale restore state when the path cannot load.
void EditorController::restoreLastOpenProject()
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

    runAction(EditorAction::restoreProject(*project_file));
}

// Captures editor-only persistence state from the current transport and displayed arrangement.
ProjectEditorState EditorController::projectEditorStateForSave() const
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
bool EditorController::loadSessionSong(
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
EditorViewState EditorController::deriveViewState() const
{
    const common::audio::TransportState transport_state = m_transport.state();
    const common::core::TimeRange timeline_range = session().timeline();

    EditorViewState state;
    state.open_enabled = canRunAction(EditorAction::Id::OpenProject);
    state.import_enabled = canRunAction(EditorAction::Id::ImportProject);
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
        .plugins = m_plugins,
    };

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
        };
    }
    if (m_pending_project_command.has_value() && m_unsaved_changes_prompt_visible)
    {
        state.unsaved_changes_prompt =
            UnsavedChangesPrompt{.command = m_pending_project_command->id()};
    }

    if (m_pending_project_command.has_value() && m_save_as_prompt_visible)
    {
        state.save_as_prompt = SaveAsPrompt{.command = m_pending_project_command->id()};
    }

    state.busy = m_busy;

    return state;
}

// Applies the serialized audio-device state stored by a previous editor session, if any.
void EditorController::restoreAudioDeviceState()
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
void EditorController::persistAudioDeviceState()
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
void EditorController::deriveAndPush()
{
    m_last_state = deriveViewState();
    if (m_view != nullptr)
    {
        m_view->setState(m_last_state);
    }
}

// Sends transient workflow failures through the view effect channel rather than render state.
void EditorController::reportError(const std::string& message)
{
    if (m_view != nullptr)
    {
        m_view->showError(message);
    }
}

// Answers the "has loading committed a usable arrangement" question used by intent gates.
bool EditorController::hasLoadedArrangement() const
{
    return session().currentArrangement() != nullptr;
}

// Reports whether a busy operation is currently active.
bool EditorController::isBusy() const noexcept
{
    return m_busy.has_value();
}

// Begins a busy operation, advances the generation token, and fills the default message from the
// central busyMessage() helper so every entry point produces consistent overlay copy.
std::uint64_t EditorController::beginBusy(BusyOperation operation)
{
    m_busy_generation += 1;
    m_busy = BusyViewState{
        .operation = operation,
        .message = busyMessage(operation),
        .cancel_enabled = false,
    };
    return m_busy_generation;
}

// Semantic wrapper for normal operation completion. Completion paths call this only after their
// captured busy generation has already matched the current controller generation.
void EditorController::finishBusyOperation()
{
    endBusy();
}

// Semantic wrapper for Close/Exit-style commands that intentionally take over the busy lifecycle.
void EditorController::supersedeBusyOperation()
{
    endBusy();
}

// Clears busy state and advances the generation token. The generation advance makes any in-flight
// worker completion stale whether this is a normal finish or a superseding action takeover.
void EditorController::endBusy()
{
    m_busy.reset();
    m_busy_generation += 1;
}

// Treat imported unsaved projects and future session edits as requiring confirmation.
bool EditorController::hasUnsavedChanges() const noexcept
{
    return m_project.has_value() && (m_has_unsaved_changes || m_save_requires_destination);
}

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::canStopTransport(const common::audio::TransportState& transport_state) const
{
    return hasLoadedArrangement() &&
           (transport_state.playing || m_transport.position() != session().timeline().start);
}

} // namespace rock_hero::editor::core
