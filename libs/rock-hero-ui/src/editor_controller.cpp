#include "editor_controller.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace rock_hero::ui
{

// Captures an initial derived state and subscribes for coarse transport transitions; no view push
// happens here because the view binding does not exist until attachView().
EditorController::EditorController(
    core::Session& session, audio::ITransport& transport, audio::IEdit& edit)
    : m_session(session)
    , m_transport(transport)
    , m_edit(edit)
{
    m_last_state = deriveViewState();
    m_transport.addListener(*this);
}

// Detaches the listener registration first so a callback firing during teardown cannot reach a
// half-destroyed controller.
EditorController::~EditorController()
{
    m_transport.removeListener(*this);
}

// Stores the new view binding and immediately satisfies the "first push at attachment" contract
// using whatever state the controller has cached up to this point.
void EditorController::attachView(IEditorView& view)
{
    m_view = &view;
    view.setState(m_last_state);
}

// Validates the track id, runs the edit through the IEdit port, then commits the asset to the
// session. Reentrant transport callbacks during the edit window are coalesced into the single
// post-edit push so the view never sees a stale intermediate state derived from pre-commit
// session data.
void EditorController::onLoadAudioAssetRequested(
    core::TrackId track_id, core::AudioAsset audio_asset)
{
    if (m_session.findTrack(track_id) == nullptr)
    {
        return;
    }

    m_edit_in_progress = true;
    const bool edit_ok = m_edit.setTrackAudioSource(track_id, audio_asset);

    if (!edit_ok)
    {
        m_last_load_error = std::string{"Could not load file: "} + audio_asset.path.string();
        m_edit_in_progress = false;
        m_pending_refresh = false;
        deriveAndPush();
        return;
    }

    const bool session_ok = m_session.replaceTrackAsset(track_id, std::move(audio_asset));
    m_edit_in_progress = false;

    if (session_ok)
    {
        m_last_load_error.reset();
    }
    else
    {
        // Session and audio backend are out of sync; preserve the existing session state and
        // surface the inconsistency rather than silently pretending the load succeeded.
        assert(false && "Session::replaceTrackAsset failed after IEdit::setTrackAudioSource");
        m_last_load_error = std::string{"Internal error: session out of sync after audio load"};
    }

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
    m_pending_refresh = false;
    deriveAndPush();
}

// Ignores the intent when no track has an asset, otherwise toggles between play and pause based
// on the current transport snapshot.
void EditorController::onPlayPausePressed()
{
    if (!anyTrackHasAsset())
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
// transport the view considers stopped.
void EditorController::onStopPressed()
{
    if (!m_transport.state().playing)
    {
        return;
    }
    m_transport.stop();
}

// Clamps the normalized input and converts it through the current transport duration so the seek
// target stays inside the loaded content even when the view emits out-of-range values.
void EditorController::onWaveformClicked(double normalized_x)
{
    const double clamped = std::clamp(normalized_x, 0.0, 1.0);
    const double duration_seconds = m_transport.state().duration.seconds;
    m_transport.seek(core::TimePosition{clamped * duration_seconds});
}

// Coarse-only transport callback. During an in-flight edit, defer the push so the post-edit
// derivation runs against the committed session and transport state instead of the pre-commit
// snapshot.
void EditorController::onTransportStateChanged(const audio::TransportState& /*state*/)
{
    if (m_edit_in_progress)
    {
        m_pending_refresh = true;
        return;
    }
    deriveAndPush();
}

// Builds the message-thread view state from the session and transport snapshot. Position is
// intentionally excluded; cursor motion is rendered by the editor view through a vsync-rate pull
// from audio::ITransport.
EditorViewState EditorController::deriveViewState() const
{
    const audio::TransportState transport_state = m_transport.state();

    EditorViewState state;
    state.load_button_enabled = !m_session.tracks().empty();
    state.play_pause_enabled = anyTrackHasAsset();
    state.stop_enabled = transport_state.playing;
    state.play_pause_shows_pause_icon = transport_state.playing;

    state.tracks.reserve(m_session.tracks().size());
    for (const core::Track& track : m_session.tracks())
    {
        state.tracks.push_back(
            TrackViewState{
                .track_id = track.id,
                .display_name = track.name,
                .audio_asset = track.audio_asset,
            });
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

// Walks the session tracks once to answer the "is anything playable" question used by both
// state derivation and play/pause gating.
bool EditorController::anyTrackHasAsset() const
{
    return std::ranges::any_of(
        m_session.tracks(), [](const core::Track& track) { return track.audio_asset.has_value(); });
}

} // namespace rock_hero::ui
