#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "timeline/keep_notes_in_time.h"
#include "timeline/song_timeline_snapshot.h"
#include "timeline/tempo_map_edits.h"
#include "timeline/tempo_map_snapshot.h"
#include "timeline/tempo_scale.h"
#include "timeline/timeline_readdress.h"
#include "tone/tone_automation_edits.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/decimal_text.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The tempo text of the span the anchor standing exactly at a position opens, as its chip prints
// it, or nothing where no anchor opening a span stands there (the terminal opens none). The one
// lookup the chord, Enter's verb and the typed tempo's undo label all print through.
[[nodiscard]] std::optional<std::string> spanBpmTextAt(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    const std::optional<std::size_t> span = tempo_map.spanStartingAt(position);
    if (!span.has_value())
    {
        return std::nullopt;
    }
    return tempoSpanBpmText(tempo_map, *span);
}

// The time a rippled map must still reach: the latest audio end over every arrangement, since the
// one map places them all and a faster tempo must not leave any of them playing past the terminal.
[[nodiscard]] double latestAudioEndSeconds(const common::core::Song& song)
{
    double latest = 0.0;
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        latest = std::max(latest, arrangement.audioTimelineRange().end.seconds);
    }
    return latest;
}

// A whole-map verb's subject: every anchor, lit as its chip.
[[nodiscard]] RefusalLight tempoMapLight(const common::core::TempoMap& tempo_map)
{
    RefusalLight light;
    for (const common::core::BeatAnchor& anchor : tempo_map.anchors())
    {
        light.add(TempoAnchorSelection{.position = anchor.position});
    }
    return light;
}

} // namespace

// Rationale lives on the declaration in editor_controller_impl.h.
template <typename Edit>
std::optional<common::core::TempoMap> EditorController::Impl::judgeTempoEdit(
    const std::string_view verb, const Edit& edit, const RefusalLight& subject)
{
    TempoMapEditResult edited = edit(session().song().tempo_map);
    if (!edited.has_value())
    {
        RefusalLight lit = subject;
        lit.add(edited.error().lit);
        refuseMarkerVerb(verb, edited.error().message, lit);
        return std::nullopt;
    }
    return std::move(*edited);
}

// Rationale lives on the declaration in editor_controller_impl.h. Both branches land through one
// `land`, so each verb states its commit once, whichever the toggle chose. The new map is built
// from the session's map in both: a held nudge run needs no origin of its own, since a move states
// its anchor's time absolutely, and under the lock the content's origin is the lock's. A refusal,
// the judge's or the funnel's, lights the verb's subject with whatever stood in its way.
template <typename Edit>
void EditorController::Impl::commitTempoEdit(
    const std::string_view verb, const Edit& edit, const TempoCommit& landing, RefusalLight subject)
{
    const auto* const moved = std::get_if<TempoAnchorSelection>(&landing);
    const auto land = [this, verb, moved, &landing, &subject](
                          auto before, auto after, const common::core::TempoMap& edited) {
        if (moved != nullptr)
        {
            if (commitMarkerMove(
                    std::move(before), std::move(after), std::string{verb}, *moved, subject))
            {
                followMovedMarker(moved->position);
            }
            return;
        }
        const auto& grid = std::get<TempoGridCommit>(landing);
        commitGridModel(
            std::move(before),
            std::move(after),
            grid.label(edited),
            MarkerRow::Tempo,
            grid.anchor,
            grid.focus,
            subject);
    };

    std::optional<common::core::TempoMap> edited = judgeTempoEdit(verb, edit, subject);
    if (!edited.has_value())
    {
        return;
    }
    const common::core::TempoMap tempo_map = std::move(*edited);
    if (!m_grid_session.note_lock.has_value())
    {
        land(
            TempoMapSnapshot::capture(session()),
            TempoMapSnapshot{.tempo_map = tempo_map},
            tempo_map);
        return;
    }
    land(SongTimelineSnapshot::capture(session()), timeKeptSnapshot(tempo_map), tempo_map);
}

// Holds the song as it stands, and marks the history there so holdsBase can say whether a later
// position still descends from it.
const common::core::SongTimeline& EditorController::Impl::engageNoteLock()
{
    m_undo_history.markBase();
    return m_grid_session.note_lock.emplace(common::core::makeSongTimeline(session().song()));
}

// Rationale lives on the declaration in editor_controller_impl.h.
void EditorController::Impl::relockOnConvertedContent(
    const std::span<const common::core::ChartConversion> conversions)
{
    if (!conversions.empty() && m_grid_session.note_lock.has_value())
    {
        engageNoteLock();
    }
}

// Rationale lives on the declaration in editor_controller_impl.h.
SongTimelineSnapshot EditorController::Impl::timeKeptSnapshot(
    const common::core::TempoMap& tempo_map)
{
    const std::optional<common::core::SongTimeline>& held = m_grid_session.note_lock;
    assert(held.has_value() && "asked only while the Note Lock is on");
    const common::core::SongTimeline& origin =
        held.has_value() && m_undo_history.holdsBase() ? *held : engageNoteLock();
    return SongTimelineSnapshot{.timeline = timeKeptTimeline(origin, tempo_map)};
}

// The tempo chord authors at the cursor and never reads the selection: the cursor's placement slot,
// the tone chord's read — the grid line with snap on, the tick with snap off. An anchor already
// standing there is restated — the BPM prompt for the span it opens — which is what makes the
// double press work: the first inserts and selects, and since the commit keeps the cursor on its
// musical place, the second finds that anchor on the same slot. Where none stands and a marker may
// start, the verb is an insert, which has nothing to prompt for and lands at once; at or past the
// terminal, which opens no span and after which nothing may start, the press is refused. The
// dispatcher answered first: the Tempo Lock, and the paused-only marker plane, which the insert
// shares, so it lands without a second dispatch.
void EditorController::Impl::performActionImpl(EditorAction::TempoChord /*action*/)
{
    const std::optional<common::core::GridPosition> position = cursorPosition(placementQuantum());
    if (!position.has_value())
    {
        return;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    if (std::optional<std::string> bpm_text = spanBpmTextAt(tempo_map, *position);
        bpm_text.has_value())
    {
        showMarkerPrompt(SetTempoTarget{.position = *position, .bpm_text = std::move(*bpm_text)});
        return;
    }
    if (!common::core::objectCanStartAt(*position, tempo_map))
    {
        refuseAtSongEnd(MarkerVerb::AddTempoAnchor);
        return;
    }
    performActionImpl(EditorAction::InsertTempoAnchor{.position = *position});
}

// A selection naming an anchor that is gone answers nothing.
std::optional<SetTempoTarget> EditorController::Impl::selectedTempoTarget() const
{
    const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
    if (anchor == nullptr)
    {
        return std::nullopt;
    }
    std::optional<std::string> bpm_text =
        spanBpmTextAt(session().song().tempo_map, anchor->position);
    if (!bpm_text.has_value())
    {
        return std::nullopt;
    }
    return SetTempoTarget{.position = anchor->position, .bpm_text = std::move(*bpm_text)};
}

void EditorController::Impl::onTempoAnchorInsertRequested(const common::core::GridPosition position)
{
    runAction(EditorAction::InsertTempoAnchor{.position = position});
}

// The prompt hands back raw text, so the parse is the core's: text that is not one number changes
// nothing and records nothing, exactly as a refused commit does. Whether the number is a usable
// tempo is the tempo edit's rule.
void EditorController::Impl::onTempoSpanBpmRequested(
    const common::core::GridPosition position, const std::string& text)
{
    const std::optional<double> quarter_bpm = common::core::parseFiniteDecimal(text);
    if (!quarter_bpm.has_value())
    {
        refuseMarkerVerb(
            markerVerbLabel(MarkerVerb::SetTempo),
            "not a number: " + text,
            lightOf(TempoAnchorSelection{.position = position}));
        return;
    }
    runAction(
        EditorAction::SetTempoSpanBpm{
            .position = position,
            .quarter_bpm = *quarter_bpm,
        });
}

// Adds an anchor at a grid position, pinning the time the map already gives it to the nearest whole
// millisecond, so the insert moves nothing audible — by less than half a millisecond with the grid
// as truth, and by nothing but the lattice rounding under the Note Lock, where the content keeps
// its time like under every other tempo verb. Like every insert, it leaves what it made SELECTED,
// so Enter, Delete and Alt+arrows act on the new anchor next. An occupied position, or one at or
// past the terminal, is the edit's refusal.
void EditorController::Impl::performActionImpl(const EditorAction::InsertTempoAnchor& action)
{
    commitTempoEdit(
        markerVerbLabel(MarkerVerb::AddTempoAnchor),
        [&action](const common::core::TempoMap& tempo_map) {
            return insertTempoAnchor(tempo_map, action.position);
        },
        TempoGridCommit{
            .label =
                [](const common::core::TempoMap& /*tempo_map*/) {
                    return markerVerbLabel(MarkerVerb::AddTempoAnchor);
                },
            .anchor = action.position,
            .focus = EntryFocus::FirstChange,
        },
        lightOf(TempoAnchorSelection{.position = action.position}));
}

// Types the tempo of the span an anchor opens: the anchor stays put and every later anchor ripples
// by the same whole-millisecond amount, so every later span keeps its tempo, and the terminal grows
// to keep covering the song's audio. Like the section rename, a restate SELECTS its target whether
// the tempo changed or not, and only a refusal leaves the selection where it was.
void EditorController::Impl::performActionImpl(const EditorAction::SetTempoSpanBpm& action)
{
    const double cover_until_seconds = latestAudioEndSeconds(session().song());
    commitTempoEdit(
        markerVerbLabel(MarkerVerb::SetTempo),
        [&action, cover_until_seconds](const common::core::TempoMap& tempo_map) {
            return setTempoSpanBpm(
                tempo_map, action.position, action.quarter_bpm, cover_until_seconds);
        },
        TempoGridCommit{
            // The edit refuses a position no span-opening anchor holds, so the result always
            // prints one.
            .label =
                [&action](const common::core::TempoMap& tempo_map) {
                    return markerVerbLabel(MarkerVerb::SetTempo) + " to " +
                           spanBpmTextAt(tempo_map, action.position).value_or("");
                },
            .anchor = action.position,
            .focus = EntryFocus::FirstChange,
        },
        lightOf(TempoAnchorSelection{.position = action.position}));
}

// The prompt is titled as the entry it will record, which names the form the Note Lock chooses;
// the Tempo Lock answered at dispatch, before it opens.
void EditorController::Impl::performActionImpl(EditorAction::ScaleTempoPrompt /*action*/)
{
    showMarkerPrompt(
        ScaleTempoTarget{.label = scaleTempoLabel(m_grid_session.note_lock.has_value())});
}

// The prompt hands back raw text, so the parse is the core's: text that is not a factor, or one
// past the bound, changes nothing and records nothing, exactly as a refused commit does.
void EditorController::Impl::onScaleTempoRequested(const std::string& text)
{
    const std::expected<common::core::Fraction, std::string> factor = parseScaleFactor(text);
    if (!factor.has_value())
    {
        refuseMarkerVerb(
            scaleTempoLabel(m_grid_session.note_lock.has_value()), factor.error(), RefusalLight{});
        return;
    }
    runAction(EditorAction::ScaleTempo{.factor = *factor});
}

// Scale Tempo takes the form the Note Lock chooses, and its label says which. With the lock OFF it
// STRETCHES: a tempo-map edit like a typed tempo's (scaleTempoMap), landed through commitTempoEdit,
// which finds the lock off and commits the map alone, so every object keeps its address and moves
// in time with its beat. With the lock ON it RELABELS: every grid address scaled while every time
// holds, one whole-song re-address, one entry; then the lock re-engages on what it wrote, since a
// free factor rounds the content it scaled and the lock's origin would otherwise no longer be what
// the song shows. The relabel is not the stretch laid under the held content — it re-places the
// meters and the anchors, where the lock's own path keeps a verb's map — so it is the one tempo
// verb that does not reach commitTempoEdit's lock branch. The Tempo Lock refuses both forms at
// dispatch. The whole map is the subject of both forms, lit as every anchor's chip. Nothing is
// selected after either: the relabel sends every
// address the old selection held to other music.
void EditorController::Impl::performActionImpl(const EditorAction::ScaleTempo& action)
{
    std::string label = scaleTempoLabel(m_grid_session.note_lock.has_value());
    const RefusalLight subject = tempoMapLight(session().song().tempo_map);
    if (!m_grid_session.note_lock.has_value())
    {
        const double cover_until_seconds = latestAudioEndSeconds(session().song());
        commitTempoEdit(
            label,
            [&action, cover_until_seconds](const common::core::TempoMap& tempo_map) {
                return scaleTempoMap(tempo_map, action.factor, cover_until_seconds);
            },
            TempoGridCommit{
                .label = [&label](const common::core::TempoMap& /*tempo_map*/) { return label; },
                .anchor = std::nullopt,
                .focus = EntryFocus::None,
            },
            subject);
        return;
    }

    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    std::expected<common::core::SongTimeline, std::string> scaled =
        scaledTimeline(before.timeline, action.factor);
    if (!scaled.has_value())
    {
        refuseMarkerVerb(label, scaled.error(), subject);
        return;
    }
    const MarkerCommit committed = commitGridModel(
        std::move(before),
        SongTimelineSnapshot{.timeline = std::move(*scaled)},
        std::move(label),
        MarkerRow::Tempo,
        std::nullopt,
        EntryFocus::None,
        subject);
    if (committed == MarkerCommit::Landed)
    {
        engageNoteLock();
    }
}

// Moves the selected anchor one on-screen pixel at the reported zoom (at least a millisecond), in
// free time: the anchor defines the grid, so no grid line can be its step. Every other anchor stays
// pinned and the two spans beside it change tempo. A step onto a neighbour, or the first anchor
// below zero, is refused rather than clamped.
void EditorController::Impl::moveSelectedTempoAnchor(
    const TempoAnchorSelection& selection, const ChartStepDirection direction)
{
    // The tempo row's markers are the anchors that open a span, in order, so a marker's index is
    // its anchor's.
    const std::optional<std::size_t> index = markerIndex(MarkerRow::Tempo, selection.position);
    if (!index.has_value())
    {
        // The selection went stale; moving nothing is the answer.
        return;
    }
    const std::int64_t step = tempoAnchorNudgeMilliseconds(m_timeline_zoom_pixels_per_second);
    moveSelectedTempoAnchorTo(
        selection,
        session().song().tempo_map.anchors().at(*index).milliseconds +
            (direction == ChartStepDirection::Left ? -step : step));
}

void EditorController::Impl::onTempoAnchorMoveRequested(
    const common::core::GridPosition position, const std::int64_t milliseconds)
{
    runAction(EditorAction::MoveTempoAnchor{.position = position, .milliseconds = milliseconds});
}

// One event of a tempo-chip drag. An event writes nothing (plan 66 decision 7): the drag is one
// gesture, so its move is HELD for the release to commit once, and the event only publishes the
// map the release would land for the grid and content to draw (tempoDragPreview). It is judged
// now, by the judge the commit uses, so a refused event keeps the last legal time held; under the
// Tempo Lock the dispatcher refuses every event first, so nothing is held and nothing previews
// however far the pointer goes, and the flash joins its running pulse train. The move
// is asked only of the SELECTED anchor, the one the press selected: selecting here instead would
// let an event after Esc released the anchor quietly grab it again.
void EditorController::Impl::performActionImpl(const EditorAction::MoveTempoAnchor& action)
{
    const TempoAnchorSelection dragged{.position = action.position};
    if (const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
        anchor == nullptr || *anchor != dragged)
    {
        return;
    }
    const std::optional<common::core::TempoMap> moved = judgeTempoEdit(
        markerVerbLabel(MarkerVerb::MoveTempoAnchor),
        [&action](const common::core::TempoMap& tempo_map) {
            return moveTempoAnchor(tempo_map, action.position, action.milliseconds);
        },
        lightOf(dragged));
    if (!moved.has_value())
    {
        return;
    }
    m_held_tempo_anchor_milliseconds = action.milliseconds;
    updateView();
}

// Every chip-press release reports here, the core and not the ruler deciding whether the drag
// moved: only a held time has anything to commit, so the rest end before the action pipeline
// logs or gates them. The drag ENDS here, whatever answers its commit: the held time is taken and
// the preview's end published first, so a release the Tempo Lock refuses — a bound key can lock
// mid-drag — leaves no preview of a move that never landed. The projections are keyed on the chart
// revision, which only a landed commit moves, so that publish re-projects nothing.
void EditorController::Impl::onTempoAnchorDragReleased()
{
    const std::optional<std::int64_t> held =
        std::exchange(m_held_tempo_anchor_milliseconds, std::nullopt);
    const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
    if (!held.has_value() || anchor == nullptr)
    {
        return;
    }
    const common::core::GridPosition position = anchor->position;
    updateView();
    runAction(EditorAction::ReleaseTempoAnchorDrag{.position = position, .milliseconds = *held});
}

// Commits the time a drag held, as one move from the map the drag started on, which it never wrote:
// the very commit one Alt+arrow press makes, so the drag is one entry, one engine resync and one
// re-projection, and with the Note Lock on its one whole-song re-address.
void EditorController::Impl::performActionImpl(const EditorAction::ReleaseTempoAnchorDrag& action)
{
    moveSelectedTempoAnchorTo(
        TempoAnchorSelection{.position = action.position}, action.milliseconds);
}

// Derived on every publish rather than stored, so it can never outlive the map it moves: a held
// time the session's map no longer admits (an undo moved a neighbour past it) previews nothing.
std::optional<common::core::TempoMap> EditorController::Impl::tempoDragPreview() const
{
    const std::optional<std::int64_t>& held = m_held_tempo_anchor_milliseconds;
    const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
    if (!held.has_value() || anchor == nullptr)
    {
        return std::nullopt;
    }
    TempoMapEditResult moved = moveTempoAnchor(session().song().tempo_map, anchor->position, *held);
    if (!moved.has_value())
    {
        return std::nullopt;
    }
    return std::move(*moved);
}

// Flips the tempo lock and republishes, so the toolbar button and the menu tick move together off
// the one fact. Nothing is written anywhere: like grid snap, the lock is session-only, and
// resetGridSession puts it back on at every project boundary.
void EditorController::Impl::onTempoLockToggleRequested()
{
    runAction(EditorAction::ToggleTempoLock{});
}

void EditorController::Impl::performActionImpl(EditorAction::ToggleTempoLock /*action*/)
{
    m_grid_session.tempo_lock = !m_grid_session.tempo_lock;
    updateView();
}

// Flips the Note Lock and republishes, the lock's shape: session-only, and resetGridSession
// puts it back off at every project boundary.
void EditorController::Impl::onNoteLockToggleRequested()
{
    runAction(EditorAction::ToggleNoteLock{});
}

// Engaging is a context switch like an undo: the chart settles first — a silent point says
// nothing, and a claim the last burst broke is flattened — so the lock holds a chart nothing is
// still waiting to write, and the chart verbs' coalescing windows close, since their next press
// would write the chart directly, past the refusal every planned edit meets.
void EditorController::Impl::performActionImpl(EditorAction::ToggleNoteLock /*action*/)
{
    if (m_grid_session.note_lock.has_value())
    {
        m_grid_session.note_lock.reset();
        updateView();
        return;
    }
    static_cast<void>(settle());
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    engageNoteLock();
    updateView();
}

// The one tail every tempo-anchor move ends in, whatever drives it. A move never changes the
// anchor's position, so the selection it lands is the one it started from, and a held run of moves
// is one undo entry.
void EditorController::Impl::moveSelectedTempoAnchorTo(
    const TempoAnchorSelection& selection, const std::int64_t milliseconds)
{
    commitTempoEdit(
        markerVerbLabel(MarkerVerb::MoveTempoAnchor),
        [&selection, milliseconds](const common::core::TempoMap& tempo_map) {
            return moveTempoAnchor(tempo_map, selection.position, milliseconds);
        },
        selection,
        lightOf(selection));
}

// Deletes the selected anchor (the Delete-key dispatch for the tempo alternative): the two spans
// beside it merge, their outer anchors staying pinned, so it is the exact inverse of an insert. The
// first anchor and the terminal are refused; every map keeps both. Nothing is left selected, so a
// refusal lights the anchor the delete addressed, which still stands.
void EditorController::Impl::deleteSelectedTempoAnchor(const TempoAnchorSelection& selection)
{
    commitTempoEdit(
        markerVerbLabel(MarkerVerb::DeleteTempoAnchor),
        [&selection](const common::core::TempoMap& tempo_map) {
            return deleteTempoAnchor(tempo_map, selection.position);
        },
        TempoGridCommit{
            .label =
                [](const common::core::TempoMap& /*tempo_map*/) {
                    return markerVerbLabel(MarkerVerb::DeleteTempoAnchor);
                },
            .anchor = std::nullopt,
            .focus = EntryFocus::FirstChange,
        },
        lightOf(selection));
}

// THE one authority for keeping the engine's tempo-derived caches current: the seconds-domain tone
// automation curves, which are the arrangement's musical automation baked through the map, and the
// plugins' one-way host-tempo mirror. Every path that can change the map — the marker funnel, an
// undo or a redo — asks here, and it compares the session's map with the one it last pushed, so
// asking costs one comparison when the map did not change. Nothing to do without a ready rig: the
// rig load pushes both from the map it finds.
void EditorController::Impl::syncTempoDerivedAudio()
{
    if (!m_project_audio_ready)
    {
        return;
    }
    if (m_engine_tempo_map == session().song().tempo_map)
    {
        return;
    }
    pushTempoDerivedAudio();
}

void EditorController::Impl::pushTempoDerivedAudio()
{
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    rebuildDerivedToneCurves(editContext());
    m_song_audio.mirrorTempoMap(tempo_map);
    m_engine_tempo_map = tempo_map;
}

} // namespace rock_hero::editor::core
