#include "editor_controller.h"

#include <algorithm>
#include <expected>
#include <optional>
#include <rock_hero/core/psarc_project_importer.h>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Production open path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<core::Song, std::string> defaultOpen(
    core::Project& project, const std::filesystem::path& file)
{
    return project.load(file);
}

// Production import path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<core::Song, std::string> defaultImport(
    core::Project& project, const std::filesystem::path& file)
{
    core::PsarcProjectImporter importer;
    return project.import(file, importer);
}

// Production save path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, std::string> defaultSave(
    core::Project& project, const core::Song& song)
{
    return project.save(song);
}

// Production save-as path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, std::string> defaultSaveAs(
    core::Project& project, const std::filesystem::path& file, const core::Song& song)
{
    return project.saveAs(file, song);
}

// Production exit fallback used when a composition host does not provide an exit callback.
void defaultExit()
{}

} // namespace

// Subscribes for coarse transport transitions and captures an initial derived state; no view push
// happens here because the view binding does not exist until attachView().
EditorController::EditorController(
    audio::ITransport& transport, EditCoordinator& edit_coordinator, OpenFunction open_function,
    ImportFunction import_function, SaveFunction save_function, SaveAsFunction save_as_function,
    ExitFunction exit_function)
    : m_transport(transport)
    , m_edit_coordinator(edit_coordinator)
    , m_open_function(open_function ? std::move(open_function) : OpenFunction{defaultOpen})
    , m_import_function(
          import_function ? std::move(import_function) : ImportFunction{defaultImport})
    , m_save_function(save_function ? std::move(save_function) : SaveFunction{defaultSave})
    , m_save_as_function(
          save_as_function ? std::move(save_as_function) : SaveAsFunction{defaultSaveAs})
    , m_exit_function(exit_function ? std::move(exit_function) : ExitFunction{defaultExit})
    , m_transport_listener(transport, *this)
{
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

// Opens a package, asks the edit coordinator to load the initial arrangement, and stores the
// context only after the backend and Session both accept the song.
void EditorController::onOpenRequested(std::filesystem::path file)
{
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Open,
            .file = std::move(file),
        });
}

// Opens a package after any current project-replacement prompt has already been satisfied.
void EditorController::openProject(const std::filesystem::path& file)
{
    core::Project project;
    std::expected<core::Song, std::string> loaded_song = m_open_function(project, file);
    if (!loaded_song.has_value())
    {
        m_last_error = std::string{"Could not open: "} + loaded_song.error();
        deriveAndPush();
        return;
    }

    core::Song song = std::move(*loaded_song);

    m_session_edit_in_progress = true;
    const bool project_loaded = m_edit_coordinator.loadSong(std::move(song), 0);
    m_session_edit_in_progress = false;

    if (!project_loaded)
    {
        m_last_error = std::string{"Could not load audio from: "} + file.string();
        deriveAndPush();
        return;
    }

    m_project = std::move(project);
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    m_last_error.reset();
    clearPendingProjectAction();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
    deriveAndPush();
}

// Imports a foreign package, asks the edit coordinator to load the initial arrangement, and stores
// the unsaved workspace only after the backend and Session both accept the song.
void EditorController::onImportRequested(std::filesystem::path file)
{
    requestProjectAction(
        PendingProjectRequest{
            .action = PendingProjectAction::Import,
            .file = std::move(file),
        });
}

// Imports a foreign package after any current project-replacement prompt has been satisfied.
void EditorController::importProject(const std::filesystem::path& file)
{
    core::Project project;
    std::expected<core::Song, std::string> loaded_song = m_import_function(project, file);
    if (!loaded_song.has_value())
    {
        m_last_error = std::string{"Could not import: "} + loaded_song.error();
        deriveAndPush();
        return;
    }

    core::Song song = std::move(*loaded_song);

    m_session_edit_in_progress = true;
    const bool project_loaded = m_edit_coordinator.loadSong(std::move(song), 0);
    m_session_edit_in_progress = false;

    if (!project_loaded)
    {
        m_last_error = std::string{"Could not load imported audio from: "} + file.string();
        deriveAndPush();
        return;
    }

    m_project = std::move(project);
    m_save_requires_destination = true;
    m_has_unsaved_changes = true;
    m_last_error.reset();
    clearPendingProjectAction();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
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

    const auto saved = m_save_as_function(*m_project, file, session().song());
    if (!saved.has_value())
    {
        m_last_error = std::string{"Could not save as: "} + saved.error();
        deriveAndPush();
        return;
    }

    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    m_last_error.reset();
    if (m_pending_project_action.has_value())
    {
        continuePendingProjectAction();
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
    case UnsavedChangesDecision::Discard: {
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
        clearPendingProjectAction();
        deriveAndPush();
        break;
    }
}

// Ignores the intent when the current arrangement has no audio, otherwise toggles playback.
void EditorController::onPlayPausePressed()
{
    if (!currentArrangementHasAudio())
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
    const audio::TransportState transport_state = m_transport.state();
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
    const core::TimeRange timeline_range = session().timeline();
    const double target_seconds =
        timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
    m_transport.seek(timeline_range.clamp(core::TimePosition{target_seconds}));
    deriveAndPush();
}

// Coarse-only transport callback. During an in-flight edit, defer the push so the post-edit
// derivation runs against the updated session and transport state instead of stale data.
void EditorController::onTransportStateChanged(audio::TransportState /*state*/)
{
    if (m_session_edit_in_progress)
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
        if (closeProject())
        {
            clearPendingProjectAction();
            deriveAndPush();
        }
        break;
    case PendingProjectAction::Open:
        openProject(request.file);
        break;
    case PendingProjectAction::Import:
        importProject(request.file);
        break;
    case PendingProjectAction::Exit:
        if (closeProject())
        {
            clearPendingProjectAction();
            m_exit_function();
        }
        break;
    }
}

// Closes the current editor document across transport, backend audio, session, and workspace.
bool EditorController::closeProject()
{
    if (!m_project.has_value())
    {
        m_edit_coordinator.closeSong();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        return true;
    }

    m_transport.stop();
    m_edit_coordinator.closeSong();

    auto closed = m_project->close();
    if (!closed.has_value())
    {
        m_last_error = std::string{"Could not close: "} + closed.error();
        m_project.reset();
        m_save_requires_destination = false;
        m_has_unsaved_changes = false;
        deriveAndPush();
        return false;
    }

    m_project.reset();
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    m_last_error.reset();
    return true;
}

// Saves through the current destination and clears dirty state only after persistence succeeds.
bool EditorController::saveProject()
{
    if (!m_project.has_value())
    {
        return false;
    }

    const auto saved = m_save_function(*m_project, session().song());
    if (!saved.has_value())
    {
        m_last_error = std::string{"Could not save: "} + saved.error();
        deriveAndPush();
        return false;
    }

    m_has_unsaved_changes = false;
    m_last_error.reset();
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

// Returns the coordinator-owned editor session through the read-only access boundary.
const core::Session& EditorController::session() const noexcept
{
    return m_edit_coordinator.session();
}

// Builds the message-thread view state from the session and transport state. Current cursor
// position is only sampled to derive stop enabledness; the view receives discrete mapping state
// rather than a continuously pushed playhead position.
EditorViewState EditorController::deriveViewState() const
{
    const audio::TransportState transport_state = m_transport.state();
    const core::TimeRange timeline_range = session().timeline();

    EditorViewState state;
    state.open_enabled = true;
    state.import_enabled = true;
    state.save_enabled = m_project.has_value();
    state.save_as_enabled = m_project.has_value();
    state.close_enabled = m_project.has_value();
    state.project_loaded = session().currentArrangement() != nullptr;
    state.save_requires_destination = m_save_requires_destination;
    state.play_pause_enabled = currentArrangementHasAudio();
    state.stop_enabled = canStopTransport(transport_state);
    state.play_pause_shows_pause_icon = transport_state.playing;
    state.visible_timeline = timeline_range;

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
        };
    }
    state.last_error = m_last_error;
    if (m_pending_project_action.has_value() && m_unsaved_changes_prompt_visible)
    {
        state.unsaved_changes_prompt =
            UnsavedChangesPrompt{.action = m_pending_project_action->action};
    }

    if (m_pending_project_action.has_value() && m_save_as_prompt_visible)
    {
        state.save_as_prompt = SaveAsPrompt{.action = m_pending_project_action->action};
    }

    return state;
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

// Answers the "is the displayed arrangement playable" question used by state and intent gates.
bool EditorController::currentArrangementHasAudio() const
{
    const auto* arrangement = session().currentArrangement();
    return arrangement != nullptr && arrangement->hasAudio();
}

// Treat imported unsaved projects and future session edits as requiring confirmation.
bool EditorController::hasUnsavedChanges() const noexcept
{
    return m_project.has_value() && (m_has_unsaved_changes || m_save_requires_destination);
}

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::canStopTransport(const audio::TransportState& transport_state) const
{
    return currentArrangementHasAudio() &&
           (transport_state.playing || m_transport.position() != session().timeline().start);
}

} // namespace rock_hero::ui
