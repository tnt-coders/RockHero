#include "editor_controller.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::ui
{

namespace
{

// Default editor row created for a fresh session before the user has imported audio.
constexpr const char* g_default_track_name = "Full Mix";

} // namespace

// Subscribes for coarse transport transitions and captures an initial derived state; no view push
// happens here because the view binding does not exist until attachView().
EditorController::EditorController(audio::ITransport& transport, EditCoordinator& edit_coordinator)
    : m_transport(transport)
    , m_edit_coordinator(edit_coordinator)
    , m_transport_listener(transport, *this)
{
    if (session().tracks().empty())
    {
        [[maybe_unused]] const core::TrackId track_id =
            m_edit_coordinator.createTrack(g_default_track_name);
        assert(track_id.isValid() && "EditorController failed to create the default track");
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

// Validates the track id, asks the editor edit coordinator to create and commit the clip, and
// coalesces reentrant transport callbacks so the view never sees stale intermediate state.
void EditorController::onLoadAudioAssetRequested(
    core::TrackId track_id, core::AudioAsset audio_asset)
{
    if (session().findTrack(track_id) == nullptr)
    {
        return;
    }

    m_session_edit_in_progress = true;
    const auto audio_clip_id =
        m_edit_coordinator.createAudioClip(track_id, audio_asset, core::TimePosition{});
    m_session_edit_in_progress = false;

    if (!audio_clip_id.has_value())
    {
        m_last_load_error = std::string{"Could not load file: "} + audio_asset.path.string();
        m_pending_refresh = false;
        deriveAndPush();
        return;
    }

    m_last_load_error.reset();

    // The single derive-and-push below also satisfies any deferred transport refresh that may
    // have arrived during the edit window.
    m_pending_refresh = false;
    deriveAndPush();
}

// Ignores the intent when no track has an asset, otherwise toggles between play and pause based
// on the current transport state.
void EditorController::onPlayPausePressed()
{
    if (!anyTrackHasClip())
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
        m_pending_refresh = true;
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
    state.load_button_enabled = !session().tracks().empty();
    state.play_pause_enabled = anyTrackHasClip();
    state.stop_enabled = canStopTransport(transport_state);
    state.play_pause_shows_pause_icon = transport_state.playing;
    state.visible_timeline = timeline_range;

    state.tracks.reserve(session().tracks().size());
    for (const core::Track& track : session().tracks())
    {
        std::vector<AudioClipViewState> audio_clips;
        if (track.audio_clip.has_value())
        {
            const core::AudioClip& audio_clip = *track.audio_clip;
            audio_clips.push_back(
                AudioClipViewState{
                    .audio_clip_id = audio_clip.id,
                    .asset = audio_clip.asset,
                    .source_range = audio_clip.source_range,
                    .timeline_range = audio_clip.timelineRange(),
                });
        }

        state.tracks.push_back(
            TrackViewState{
                .track_id = track.id,
                .display_name = track.name,
                .audio_clips = std::move(audio_clips),
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
bool EditorController::anyTrackHasClip() const
{
    return std::ranges::any_of(
        session().tracks(), [](const core::Track& track) { return track.audio_clip.has_value(); });
}

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::canStopTransport(const audio::TransportState& transport_state) const
{
    return transport_state.playing || m_transport.position() != session().timeline().start;
}

} // namespace rock_hero::ui
