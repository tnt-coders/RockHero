#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "timeline/tempo_map_edits.h"
#include "timeline/tempo_map_snapshot.h"
#include "tone/tone_automation_edits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/decimal_text.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The label a tempo-anchor move records its undo entry under and logs its refusals under,
// whichever gesture drives it.
constexpr std::string_view g_move_tempo_anchor_label{"Move Tempo Anchor"};

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

} // namespace

// The tempo chord authors at the cursor and never reads the selection: the cursor's placement slot,
// the tone chord's read — the grid line with snap on, the tick with snap off. An anchor already
// standing there is restated — the BPM prompt for the span it opens — which is what makes the
// double press work: the first inserts and selects, and since the commit keeps the cursor on its
// musical place, the second finds that anchor on the same slot. Where none stands and a marker may
// start, the verb is an insert; at or past the terminal it is nothing, since the terminal opens no
// span and nothing may start after it.
TempoChordTarget EditorController::Impl::tempoChordTarget() const
{
    const std::optional<common::core::GridPosition> position = cursorPosition(placementQuantum());
    if (!position.has_value())
    {
        return {};
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    if (std::optional<std::string> bpm_text = spanBpmTextAt(tempo_map, *position);
        bpm_text.has_value())
    {
        return SetTempoTarget{.position = *position, .bpm_text = std::move(*bpm_text)};
    }
    if (!common::core::markerCanStartAt(*position, tempo_map))
    {
        return {};
    }
    return InsertTempoAnchorTarget{.position = *position};
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
        logMarkerRefusal("Set Tempo", "not a number: " + text);
        return;
    }
    runAction(
        EditorAction::SetTempoSpanBpm{
            .position = position,
            .quarter_bpm = *quarter_bpm,
        });
}

// Adds an anchor at a grid position, pinning the time the map already gives it, so the insert moves
// nothing audible. Like every insert, it leaves what it made SELECTED, so Enter, Delete and
// Alt+arrows act on the new anchor next. An occupied position, or one at or past the terminal, is
// the edit's refusal.
void EditorController::Impl::performActionImpl(const EditorAction::InsertTempoAnchor& action)
{
    constexpr std::string_view label{"Add Tempo Anchor"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult inserted = insertTempoAnchor(before.tempo_map, action.position);
    if (!inserted.has_value())
    {
        logMarkerRefusal(label, inserted.error().message);
        return;
    }
    commitGridModel(
        std::move(before),
        TempoMapSnapshot{.tempo_map = std::move(*inserted)},
        std::string{label},
        MarkerRow::Tempo,
        action.position);
}

// Types the tempo of the span an anchor opens: the anchor stays put and every later anchor ripples
// by the same whole-millisecond amount, so every later span keeps its tempo, and the terminal grows
// to keep covering the song's audio. Like the section rename, a restate SELECTS its target whether
// the tempo changed or not, and only a refusal leaves the selection where it was.
void EditorController::Impl::performActionImpl(const EditorAction::SetTempoSpanBpm& action)
{
    constexpr std::string_view verb{"Set Tempo"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult rippled = setTempoSpanBpm(
        before.tempo_map,
        action.position,
        action.quarter_bpm,
        latestAudioEndSeconds(session().song()));
    if (!rippled.has_value())
    {
        logMarkerRefusal(verb, rippled.error().message);
        return;
    }

    // The edit refuses a position no span-opening anchor holds, so the result always prints one.
    std::string label =
        std::string{verb} + " to " + spanBpmTextAt(*rippled, action.position).value_or("");
    commitGridModel(
        std::move(before),
        TempoMapSnapshot{.tempo_map = std::move(*rippled)},
        std::move(label),
        MarkerRow::Tempo,
        action.position);
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
    if (refuseLockedTempoAnchorMove(selection, "nudging"))
    {
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

// One event of a tempo-chip drag, the pointer form of Alt+arrows: the press selected the anchor, so
// each event is one more press of a held run on it and the whole drag is one undo entry. The move
// is asked only of the SELECTED anchor, which is what proves the run is this drag's: selecting here
// instead would let an event after Esc released the anchor quietly grab it again and open a second
// entry.
void EditorController::Impl::performActionImpl(const EditorAction::MoveTempoAnchor& action)
{
    const TempoAnchorSelection dragged{.position = action.position};
    if (const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
        anchor == nullptr || *anchor != dragged)
    {
        return;
    }
    // Refused at every event, so nothing lands and no entry opens however far the pointer goes; the
    // flash joins its running pulse train rather than restarting per event.
    if (refuseLockedTempoAnchorMove(dragged, "dragging"))
    {
        return;
    }
    moveSelectedTempoAnchorTo(dragged, action.milliseconds);
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

// Rationale lives on the declaration in editor_controller_impl.h. The lock guards anchor TIMES
// only, which is why the insert, delete and typed-tempo verbs never ask here.
bool EditorController::Impl::refuseLockedTempoAnchorMove(
    const TempoAnchorSelection& selection, const std::string_view gesture)
{
    if (!m_grid_session.tempo_lock)
    {
        return false;
    }
    logMarkerRefusal(
        g_move_tempo_anchor_label,
        "Tempo Lock is on: " + std::string{gesture} + " the tempo anchor at " +
            common::core::formatGridPositionToken(selection.position) + " is blocked");
    if (m_view != nullptr)
    {
        m_view->flashTempoAnchorRefusal(selection.position);
    }
    return true;
}

// The one tail every tempo-anchor move ends in, whatever drives it. A move never changes the
// anchor's position, so the selection it lands is the one it started from, and a held run of moves
// is one undo entry.
void EditorController::Impl::moveSelectedTempoAnchorTo(
    const TempoAnchorSelection& selection, const std::int64_t milliseconds)
{
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult moved = moveTempoAnchor(before.tempo_map, selection.position, milliseconds);
    if (!moved.has_value())
    {
        logMarkerRefusal(g_move_tempo_anchor_label, moved.error().message);
        return;
    }

    if (commitMarkerMove(
            std::move(before),
            TempoMapSnapshot{.tempo_map = std::move(*moved)},
            std::string{g_move_tempo_anchor_label},
            selection))
    {
        followMovedMarker(selection.position);
    }
}

// Deletes the selected anchor (the Delete-key dispatch for the tempo alternative): the two spans
// beside it merge, their outer anchors staying pinned, so it is the exact inverse of an insert. The
// first anchor and the terminal are refused; every map keeps both.
void EditorController::Impl::deleteSelectedTempoAnchor(const TempoAnchorSelection& selection)
{
    constexpr std::string_view label{"Delete Tempo Anchor"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult merged = deleteTempoAnchor(before.tempo_map, selection.position);
    if (!merged.has_value())
    {
        logMarkerRefusal(label, merged.error().message);
        return;
    }
    commitGridModel(
        std::move(before),
        TempoMapSnapshot{.tempo_map = std::move(*merged)},
        std::string{label},
        MarkerRow::Tempo,
        std::nullopt);
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
