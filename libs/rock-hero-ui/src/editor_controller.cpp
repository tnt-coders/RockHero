#include "editor_controller.h"

#include <algorithm>
#include <optional>

namespace rock_hero::ui
{

// Subscribes for coarse transport transitions and captures an initial derived state; no view push
// happens here because the view binding does not exist until attachView().
EditorController::EditorController(
    audio::ITransport& transport, EditCoordinator& edit_coordinator, IProjectLoader& project_loader)
    : m_transport(transport)
    , m_edit_coordinator(edit_coordinator)
    , m_project_loader(project_loader)
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

// Opens a project package, asks the edit coordinator to load its selected arrangement, and
// promotes the extracted cache only after the backend and Session both accept the song.
void EditorController::onOpenProjectRequested(std::filesystem::path project_file)
{
    ProjectLoadResult result = m_project_loader.loadProject(project_file);
    if (!result.succeeded())
    {
        m_last_load_error = std::string{"Could not open project: "} + result.error_message;
        deriveAndPush();
        return;
    }

    m_session_edit_in_progress = true;
    const bool project_loaded = m_edit_coordinator.loadSong(
        std::move(result.project->song), result.project->selected_arrangement_index);
    m_session_edit_in_progress = false;

    if (!project_loaded)
    {
        m_last_load_error =
            std::string{"Could not load project audio from: "} + project_file.string();
        deriveAndPush();
        return;
    }

    m_project_cache = std::move(result.project->cache);
    m_last_load_error.reset();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
    deriveAndPush();
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
    state.open_project_button_enabled = true;
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
    state.last_load_error = m_last_load_error;
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

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::canStopTransport(const audio::TransportState& transport_state) const
{
    return transport_state.playing || m_transport.position() != session().timeline().start;
}

} // namespace rock_hero::ui
