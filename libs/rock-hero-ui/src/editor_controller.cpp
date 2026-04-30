#include "editor_controller.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

namespace rock_hero::ui
{

// Subscribes for coarse transport transitions and captures an initial derived state; no view push
// happens here because the view binding does not exist until attachView().
EditorController::EditorController(
    core::Session& session, audio::ITransport& transport, audio::IEdit& edit)
    : m_session(session)
    , m_transport(transport)
    , m_edit(edit)
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

// Validates the track id, runs the edit through the IEdit port, then commits the asset to the
// session. Reentrant transport callbacks during the edit window are coalesced into the single
// post-edit push so the view never sees stale intermediate view state derived from pre-commit
// session data.
void EditorController::onLoadAudioAssetRequested(
    core::TrackId track_id, core::AudioAsset audio_asset)
{
    if (m_session.findTrack(track_id) == nullptr)
    {
        return;
    }

    m_edit_in_progress = true;
    const auto timeline_range = m_edit.setTrackAudioSource(track_id, audio_asset);

    if (!timeline_range.has_value())
    {
        m_last_load_error = std::string{"Could not load file: "} + audio_asset.path.string();
        m_edit_in_progress = false;
        m_pending_refresh = false;
        deriveAndPush();
        return;
    }

    const bool session_ok =
        m_session.commitTrackAudioAsset(track_id, std::move(audio_asset), *timeline_range);
    m_edit_in_progress = false;

    if (session_ok)
    {
        m_last_load_error.reset();
    }
    else
    {
        // Session and audio backend are out of sync; preserve the existing session state and
        // surface the inconsistency rather than silently pretending the load succeeded.
        // TODO: Replace std::clog with the project logging framework once one exists.
        std::clog << "RockHero editor internal error: Session::commitTrackAudioAsset failed after "
                     "IEdit::setTrackAudioSource\n";
        assert(false && "Session::commitTrackAudioAsset failed after IEdit::setTrackAudioSource");
        m_last_load_error = std::string{"Internal error: session out of sync after audio load"};
    }

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
    m_pending_refresh = false;
    deriveAndPush();
}

// Ignores the intent when no track has an asset, otherwise toggles between play and pause based
// on the current transport state.
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
    const core::TimeRange timeline_range = m_session.timeline();
    const double target_seconds =
        timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
    m_transport.seek(timeline_range.clamp(core::TimePosition{target_seconds}));
    deriveAndPush();
}

// Coarse-only transport callback. During an in-flight edit, defer the push so the post-edit
// derivation runs against the committed session and transport state instead of pre-commit data.
void EditorController::onTransportStateChanged(audio::TransportState /*state*/)
{
    if (m_edit_in_progress)
    {
        m_pending_refresh = true;
        return;
    }
    deriveAndPush();
}

// Builds the message-thread view state from the session and transport state. Current cursor
// position is only sampled to derive stop enabledness; the view receives discrete mapping state
// rather than a continuously pushed playhead position.
EditorViewState EditorController::deriveViewState() const
{
    const audio::TransportState transport_state = m_transport.state();
    const core::TimeRange timeline_range = m_session.timeline();

    EditorViewState state;
    state.load_button_enabled = !m_session.tracks().empty();
    state.play_pause_enabled = anyTrackHasAsset();
    state.stop_enabled = canStopTransport(transport_state);
    state.play_pause_shows_pause_icon = transport_state.playing;
    state.visible_timeline_start = timeline_range.start;
    state.visible_timeline_duration = timeline_range.duration();

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

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::canStopTransport(const audio::TransportState& transport_state) const
{
    return transport_state.playing || m_transport.position() != m_session.timeline().start;
}

} // namespace rock_hero::ui
