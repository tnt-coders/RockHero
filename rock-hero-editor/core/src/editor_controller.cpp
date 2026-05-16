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

// Uses ScopedListener member teardown to detach before controller state is destroyed.
EditorController::~EditorController() = default;

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
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Open,
            .file = std::move(file),
        });
}

// Opens an editor project package after any project-replacement prompt has been satisfied.
void EditorController::openProject(const std::filesystem::path& file)
{
    Project project;
    std::expected<common::core::Song, ProjectError> loaded_song = m_open_function(project, file);
    if (!loaded_song.has_value())
    {
        reportError(std::string{"Could not open: "} + loaded_song.error().message);
        deriveAndPush();
        return;
    }

    common::core::Song song = std::move(*loaded_song);
    const ProjectEditorState editor_state = project.editorState();

    if (!loadSessionSong(std::move(song), editor_state.selected_arrangement))
    {
        reportError(std::string{"Could not load audio from: "} + file.string());
        deriveAndPush();
        return;
    }

    m_project = std::move(project);
    m_project_file = file;
    m_transport.seek(session().timeline().clamp(editor_state.cursor_position));
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    clearPendingProjectAction();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    deriveAndPush();
}

// Imports a song source and stores the workspace only after audio and Session accept the song.
void EditorController::onImportRequested(std::filesystem::path file)
{
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Import,
            .file = std::move(file),
        });
}

// Imports a song source after any current project-replacement prompt has been satisfied.
void EditorController::importSongSource(const std::filesystem::path& file)
{
    Project project;
    std::expected<common::core::Song, ProjectError> loaded_song = m_import_function(project, file);
    if (!loaded_song.has_value())
    {
        reportError(std::string{"Could not import: "} + loaded_song.error().message);
        deriveAndPush();
        return;
    }

    common::core::Song song = std::move(*loaded_song);

    if (!loadSessionSong(std::move(song), std::nullopt))
    {
        reportError(std::string{"Could not load imported audio from: "} + file.string());
        deriveAndPush();
        return;
    }

    m_project = std::move(project);
    m_project_file.clear();
    m_save_requires_destination = true;
    m_has_unsaved_changes = true;
    clearPendingProjectAction();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the load window.
    deriveAndPush();
}

// Saves to the current destination when one exists; Save As is responsible for destination choice.
void EditorController::onSaveRequested()
{
    if (m_save_requires_destination)
    {
        return;
    }

    if (!saveProject())
    {
        return;
    }

    if (m_pending_project_action.has_value())
    {
        continuePendingProjectAction();
        return;
    }

    deriveAndPush();
}

// Saves to a chosen destination and promotes future Save commands to direct saves.
void EditorController::onSaveAsRequested(std::filesystem::path file)
{
    if (!m_project.has_value())
    {
        return;
    }

    const auto saved =
        m_save_as_function(*m_project, file, session().song(), projectEditorStateForSave());
    if (!saved.has_value())
    {
        reportError(std::string{"Could not save as: "} + saved.error().message);
        deriveAndPush();
        return;
    }

    m_save_requires_destination = false;
    m_project_file = file;
    m_has_unsaved_changes = false;
    if (m_pending_project_action.has_value())
    {
        continuePendingProjectAction();
        return;
    }

    deriveAndPush();
}

// Publishes the current project as a native song package without changing save destination or
// dirty state.
void EditorController::onPublishRequested(std::filesystem::path file)
{
    if (!m_project.has_value())
    {
        return;
    }

    const auto published = m_publish_function(*m_project, file, session().song());
    if (!published.has_value())
    {
        reportError(std::string{"Could not publish: "} + published.error().message);
        deriveAndPush();
        return;
    }

    deriveAndPush();
}

// Cancels only a Save As chooser that was opened to continue a pending project action.
void EditorController::onSaveAsCancelled()
{
    if (!m_save_as_prompt_visible)
    {
        return;
    }

    clearPendingProjectAction();
    deriveAndPush();
}

// Closes the current project after prompting for unsaved changes when needed.
void EditorController::onCloseRequested()
{
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Close,
            .file = {},
        });
}

// Exits through the composition host after prompting for unsaved changes when needed.
void EditorController::onExitRequested()
{
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Exit,
            .file = {},
        });
}

// Applies the user's unsaved-changes choice to the stored pending project action.
void EditorController::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    if (!m_pending_project_action.has_value())
    {
        clearPendingProjectAction();
        deriveAndPush();
        return;
    }

    m_unsaved_changes_prompt_visible = false;
    switch (decision)
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
                clearPendingProjectAction();
                return;
            }

            continuePendingProjectAction();
            break;
        }
        case UnsavedChangesDecision::Discard:
        {
            const PendingProjectRequest request = *m_pending_project_action;
            m_has_unsaved_changes = false;
            m_save_requires_destination = false;
            if (request.action != PendingProjectAction::Close)
            {
                clearPendingProjectAction();
                if (closeProject())
                {
                    performProjectAction(request);
                }
                return;
            }

            continuePendingProjectAction();
            break;
        }
        case UnsavedChangesDecision::Cancel:
        {
            clearPendingProjectAction();
            deriveAndPush();
            break;
        }
    }
}

// Ignores the intent until audio activation has committed an arrangement, otherwise toggles
// playback.
void EditorController::onPlayPausePressed()
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

// Mirrors the published stop_enabled gate so the keyboard or alternate input paths cannot stop a
// transport the view considers already reset.
void EditorController::onStopPressed()
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
}

// Clamps the normalized input and converts it through the session timeline so the seek target
// stays inside the loaded content even when the view emits out-of-range values.
void EditorController::onWaveformClicked(double normalized_x)
{
    const double clamped = std::clamp(normalized_x, 0.0, 1.0);
    const common::core::TimeRange timeline_range = session().timeline();
    const double target_seconds =
        timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
    m_transport.seek(timeline_range.clamp(common::core::TimePosition{target_seconds}));
    deriveAndPush();
}

// Handles the first plugin UI flow: scan one selected VST3 file, append the first discovered
// candidate, and publish enough state for the panel to show the linear chain.
void EditorController::onAddPluginRequested(std::filesystem::path file)
{
    if (m_plugin_host == nullptr || !hasLoadedArrangement())
    {
        return;
    }

    std::expected<std::vector<common::audio::PluginCandidate>, common::audio::PluginHostError>
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

    // Tone persistence is not implemented yet, so the panel tracks the runtime chain without
    // marking the project dirty for data Save cannot currently restore.
    m_plugins.push_back(makePluginViewState(candidate, *handle));
    deriveAndPush();
}

// Persists the new device manager state and re-derives view state after a configuration change.
void EditorController::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    deriveAndPush();
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

// Starts a project-level action or asks the view to confirm unsaved changes first.
void EditorController::requestProjectAction(PendingProjectRequest request)
{
    if (request.action == PendingProjectAction::Close && !m_project.has_value())
    {
        return;
    }

    if (hasUnsavedChanges())
    {
        m_pending_project_action = std::move(request);
        m_unsaved_changes_prompt_visible = true;
        m_save_as_prompt_visible = false;
        deriveAndPush();
        return;
    }

    performProjectAction(request);
}

// Runs a project-level action once dirty-state gates have been satisfied.
void EditorController::performProjectAction(const PendingProjectRequest& request)
{
    switch (request.action)
    {
        case PendingProjectAction::Close:
        {
            if (closeProject())
            {
                clearPendingProjectAction();
                deriveAndPush();
            }
            break;
        }
        case PendingProjectAction::Open:
        {
            openProject(request.file);
            break;
        }
        case PendingProjectAction::Import:
        {
            importSongSource(request.file);
            break;
        }
        case PendingProjectAction::Exit:
        {
            const std::optional<std::filesystem::path> restorable_project_file =
                currentProjectFile();
            if (closeProject())
            {
                clearPendingProjectAction();
                if (m_settings != nullptr)
                {
                    m_settings->setLastOpenProject(restorable_project_file);
                }
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
    endBusy();

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

// Resumes a pending action after Save or Save As has successfully protected user changes.
void EditorController::continuePendingProjectAction()
{
    if (!m_pending_project_action.has_value())
    {
        deriveAndPush();
        return;
    }

    const PendingProjectRequest request = std::move(*m_pending_project_action);
    clearPendingProjectAction();
    performProjectAction(request);
}

// Clears all prompt-related state without changing the currently loaded project.
void EditorController::clearPendingProjectAction() noexcept
{
    m_pending_project_action.reset();
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

    onOpenRequested(*project_file);
    const std::optional<std::filesystem::path> opened_project = currentProjectFile();
    if (!opened_project.has_value() ||
        opened_project->lexically_normal() != project_file->lexically_normal())
    {
        m_settings->setLastOpenProject(std::nullopt);
    }
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
    state.open_enabled = true;
    state.import_enabled = true;
    state.save_enabled = m_project.has_value();
    state.save_as_enabled = m_project.has_value();
    state.publish_enabled = m_project.has_value();
    if (!m_project_file.empty())
    {
        state.suggested_publish_file = m_project_file;
        state.suggested_publish_file.replace_extension(".rock");
    }
    state.close_enabled = m_project.has_value();
    state.project_loaded = session().currentArrangement() != nullptr;
    state.save_requires_destination = m_save_requires_destination;
    state.play_pause_enabled = hasLoadedArrangement();
    state.stop_enabled = canStopTransport(transport_state);
    state.play_pause_shows_pause_icon = transport_state.playing;
    state.audio_devices_available = m_audio_devices != nullptr;
    state.current_audio_device_name =
        m_audio_devices != nullptr ? m_audio_devices->currentDeviceName() : std::nullopt;
    state.visible_timeline = timeline_range;
    state.signal_chain = SignalChainViewState{
        .add_plugin_enabled = m_plugin_host != nullptr && hasLoadedArrangement(),
        .plugins = m_plugins,
    };

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
        };
    }
    if (m_pending_project_action.has_value() && m_unsaved_changes_prompt_visible)
    {
        state.unsaved_changes_prompt =
            UnsavedChangesPrompt{.action = m_pending_project_action->action};
    }

    if (m_pending_project_action.has_value() && m_save_as_prompt_visible)
    {
        state.save_as_prompt = SaveAsPrompt{.action = m_pending_project_action->action};
    }

    state.busy = m_busy;

    // Busy overrides command affordances regardless of feature availability. Each *_enabled
    // boolean above retains its natural "available when not busy" meaning; this single block is
    // the only place that knows about the busy override, so command-availability logic does not
    // need to be sprinkled with isBusy() checks. close_enabled stays at its non-busy value
    // because Close supersedes an in-flight operation through endBusy() rather than being
    // blocked by it.
    if (isBusy())
    {
        state.open_enabled = false;
        state.import_enabled = false;
        state.save_enabled = false;
        state.save_as_enabled = false;
        state.publish_enabled = false;
        state.play_pause_enabled = false;
        state.stop_enabled = false;
        state.signal_chain.add_plugin_enabled = false;
    }

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

// Clears the busy state and advances the generation token so any in-flight worker's completion
// sees a mismatch and discards itself. Successful completions call this when their commit
// finishes; Close and Exit also call it to supersede an in-flight open/import. Stale completions
// must not call this themselves: the live busy state belongs to whichever operation superseded
// the stale one.
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
