#include "chart/fret_hand_positions_snapshot.h"
#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

// The position row's markers: every hand event, derived and authored alike, through the marker
// grammar every other marker kind shares. Only an authored position stores anything — where the
// hand arrives and its index finger's fret, and its window's end fret where the charter states one.
// So a verb that STATES something of a derived position authors it there first, and a verb that
// needs something stored — Delete and the move — refuses one.

namespace
{

// The placement with `fret` typed at one edge: its index finger's start, or its window's end.
[[nodiscard]] common::core::FretHandPosition retyped(
    common::core::FretHandPosition placement, const FretHandEdge edge, const int fret)
{
    if (edge == FretHandEdge::End)
    {
        placement.end_fret = fret;
    }
    else
    {
        placement.fret = fret;
    }
    return placement;
}

// Finds the placement arriving exactly at a position; a placement's position is its identity.
[[nodiscard]] std::vector<common::core::FretHandPosition>::iterator findPlacement(
    std::vector<common::core::FretHandPosition>& placements,
    const common::core::GridPosition& position)
{
    return std::ranges::find(placements, position, &common::core::FretHandPosition::position);
}

// The placement an insert at a position makes: it states the fret of the window already standing
// there — the last hand event fronting at or before it, derived or authored, read from the hand
// layer the projection draws — so inserting moves no hand. Where nothing stands yet it states the
// nut window, the first fret the capo leaves playable. Either fret is already one the normalizer
// leaves alone: every event's window is fitted onto the board, and so is the nut window.
[[nodiscard]] common::core::FretHandPosition defaultedPlacement(
    const std::vector<common::core::FretHandEvent>& events, const int capo,
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    const auto past =
        common::core::firstFretHandEventAfter(events, tempo_map.wholeNotePositionAt(position));
    return common::core::FretHandPosition{
        .position = position,
        .fret = past == events.begin() ? common::core::firstPlayableFret(capo)
                                       : std::prev(past)->window.fret,
        .end_fret = std::nullopt,
    };
}

// The placement a STATING edit at `position` writes into: the authored one standing there, else —
// a derived position stores nothing — the one Ctrl+P makes at that place (defaultedPlacement over
// the chart's hand events), appended for the funnel's normalize to order. The edit then applies to
// it exactly as to an authored one, the authoring and the edit one commit. Nothing without a chart.
[[nodiscard]] std::vector<common::core::FretHandPosition>::iterator statedPlacement(
    std::vector<common::core::FretHandPosition>& placements, const common::core::Session& session,
    const std::vector<common::core::FretHandEvent>& events,
    const common::core::GridPosition& position)
{
    if (const auto match = findPlacement(placements, position); match != placements.end())
    {
        return match;
    }
    const common::core::Arrangement* const arrangement = session.currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return placements.end();
    }
    placements.push_back(defaultedPlacement(
        events, arrangement->chart->tuning.capo, session.song().tempo_map, position));
    return std::prev(placements.end());
}

// The stored positions less every one the derivation does not need: walked in order, a position is
// dropped where the events derived without it equal the events derived with it, and the walk goes
// on against the reduced list, so each verdict is judged against what is actually kept. Dropping
// one leaves the events unchanged, so the events every later verdict compares against are those
// the walk began with. Every derivation reads the chart's notes layer, which no position changes.
[[nodiscard]] std::vector<common::core::FretHandPosition> withoutRedundantPositions(
    const std::vector<common::core::FretHandPosition>& stored,
    const common::core::ChartResolutions& resolved, const common::core::TempoMap& tempo_map)
{
    const auto derive = [&](const std::vector<common::core::FretHandPosition>& authored) {
        return common::core::deriveFretHandEvents(
            resolved.connections, resolved.hand_demands, tempo_map, authored);
    };
    const std::vector<common::core::FretHandEvent> events = derive(stored);
    std::vector<common::core::FretHandPosition> kept = stored;
    std::size_t index = 0;
    while (index < kept.size())
    {
        std::vector<common::core::FretHandPosition> without = kept;
        without.erase(std::next(without.begin(), static_cast<std::ptrdiff_t>(index)));
        if (derive(without) == events)
        {
            kept = std::move(without);
        }
        else
        {
            ++index;
        }
    }
    return kept;
}

// The fret-hand position chord's restate: the placement arriving at the cursor, which it selects.
struct RestateAt
{
    common::core::GridPosition position;
};

// The fret-hand position chord's insert: the placement it will add, fret already defaulted.
struct InsertAt
{
    common::core::FretHandPosition placement;
};

// What the fret-hand position chord does, or nothing.
using FretHandPositionChordTarget = std::variant<std::monostate, RestateAt, InsertAt>;

// THE fret-hand position chord's precedence, stated once: it authors at the cursor and never reads
// the selection. A placement arriving exactly there is restated, which is what keeps a second
// press from inserting a second placement on the first one's start; anywhere else a marker may
// start, the verb is an insert, defaulted from the chart's hand events. Nothing without a chart,
// without a cursor (playing, no song), or on the closing barline, so the chord is inert exactly
// where the commit would refuse it.
[[nodiscard]] FretHandPositionChordTarget fretHandPositionChordTarget(
    const common::core::Arrangement* const arrangement,
    const std::optional<common::core::GridPosition>& cursor,
    const std::vector<common::core::FretHandEvent>& events, const common::core::TempoMap& tempo_map)
{
    if (arrangement == nullptr || !arrangement->chart.has_value() || !cursor.has_value())
    {
        return {};
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::GridPosition position = *cursor;
    if (std::ranges::contains(
            chart.fret_hand_positions, position, &common::core::FretHandPosition::position))
    {
        return RestateAt{.position = position};
    }
    if (!common::core::markerCanStartAt(position, tempo_map))
    {
        return {};
    }
    return InsertAt{
        .placement = defaultedPlacement(events, chart.tuning.capo, tempo_map, position)
    };
}

} // namespace

// Chips ascend by their fronts, so the one fronting at a position is one search.
std::optional<std::size_t> EditorController::Impl::fretHandChipIndex(
    const common::core::ChartViewState& scene, const common::core::GridPosition& position)
{
    const std::vector<common::core::FhpViewState>& chips = scene.fret_hand_positions;
    const auto found = std::ranges::lower_bound(
        chips, position, std::ranges::less{}, &common::core::FhpViewState::position);
    if (found == chips.end() || !(found->position == position))
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(chips.begin(), found));
}

// A hand chip click selects the position it draws, derived or authored, and seeks nothing, exactly
// as a ruler chip does, through the select every marker shares, so the caret demotes. The row's
// markers are the chips, so the chip's index is its marker's.
void EditorController::Impl::performActionImpl(const EditorAction::SelectFretHandPosition& action)
{
    if (action.index < markerStarts(MarkerRow::FretHandPosition).size())
    {
        selectMarker(markerSelectionAt(MarkerRow::FretHandPosition, action.index));
    }
    updateView();
}

// The fret-hand position chord runs its own target: the verb has nothing to prompt for, so the
// precedence is decided and performed here, at the press, rather than published for a surface to
// hand back. The cursor is read at the placement quantum — the slot an arrow press would arm on,
// as the tone chord reads it. A restate only selects it, and a digit then retypes its fret; an
// insert that lands is left SELECTED, so Delete and Alt+arrows act on it next.
void EditorController::Impl::performActionImpl(
    EditorAction::AuthorFretHandPositionAtCursor /*action*/)
{
    const FretHandPositionChordTarget target = fretHandPositionChordTarget(
        session().currentArrangement(),
        cursorPosition(placementQuantum()),
        fretHandResolutions().events,
        session().song().tempo_map);
    if (const auto* const restate = std::get_if<RestateAt>(&target))
    {
        selectMarker(FretHandPositionSelection{.position = restate->position});
        updateView();
        return;
    }
    const auto* const insert = std::get_if<InsertAt>(&target);
    if (insert == nullptr)
    {
        return;
    }

    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    after.placements.push_back(insert->placement);
    if (commitMarkerModel(
            std::move(before), std::move(after), markerVerbLabel(MarkerVerb::AddHandPosition)) ==
        MarkerCommit::Refused)
    {
        flashFretHandChip(insert->placement.position);
        return;
    }
    // The insert selects what it made, and a selection made after the commit publishes with this
    // refresh: the commit's own publish ran before it existed.
    selectMarker(FretHandPositionSelection{.position = insert->placement.position});
    updateView();
}

// Refuses a verb that needs a STORED position on a derived one, which stores nothing to delete or
// move: logged, and the refused chip glows red, the selection staying where it was.
void EditorController::Impl::refuseDerivedFretHandPosition(
    const FretHandPositionSelection& selection, const MarkerVerb verb)
{
    m_refusal_log.markerEdit(
        markerVerbLabel(verb),
        common::core::formatHandPositionToken(selection.position) +
            " is derived, so nothing is stored there");
    flashFretHandChip(selection.position);
}

void EditorController::Impl::flashFretHandChip(const common::core::GridPosition& position)
{
    if (const std::optional<std::size_t> chip = markerIndex(MarkerRow::FretHandPosition, position);
        chip.has_value() && m_view != nullptr)
    {
        m_view->flashChartRefusal(
            ChartRefusalFlash{.notes = {}, .keyframes = {}, .fret_hand_position = chip});
    }
}

// Steps the selected placement one placement-quantum line, the tone change's step. Refused rather
// than clamped, with the chip glowing red: onto a start another placement holds, past the song's
// end, or onto a gripped note the window does not cover, by the stream's rules at the commit; and
// here where the grid step has nowhere to go, off either end of the chart (steppedNudgePosition).
// A landed move brings the paused cursor to the new start, as every selection move of a marker
// does.
void EditorController::Impl::moveSelectedFretHandPosition(
    const FretHandPositionSelection& selection, const ChartStepDirection direction)
{
    const std::string label = markerVerbLabel(MarkerVerb::MoveHandPosition);
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection.position);
    if (match == after.placements.end())
    {
        refuseDerivedFretHandPosition(selection, MarkerVerb::MoveHandPosition);
        return;
    }
    const std::optional<common::core::GridPosition> step =
        steppedNudgePosition(selection.position, direction == ChartStepDirection::Right);
    if (!step.has_value())
    {
        m_refusal_log.markerEdit(label, "fret-hand position would leave the chart");
        flashFretHandChip(selection.position);
        return;
    }
    const common::core::GridPosition target = *step;

    match->position = target;
    if (!commitMarkerMove(
            std::move(before),
            std::move(after),
            label,
            FretHandPositionSelection{.position = target}))
    {
        flashFretHandChip(selection.position);
        return;
    }
    // The position IS the placement's identity, so the commit selects the moved placement under its
    // new one; followMovedMarker publishes it together with the cursor it brings along.
    followMovedMarker(target);
}

// A placement's fret entry, planned the way the commit will judge it: the funnel's own front half
// over the stream with the one fret replaced, so the red box refuses exactly what the settle would
// — a start retyped above an authored end included. On a derived position the plan authors it first
// (statedPlacement), so the one commit states both.
std::expected<EditorController::Impl::ChartFretHandPlan, ChartPlanRefusal> EditorController::Impl::
    planFretHandFret(
        const common::core::GridPosition& position, const FretHandEdge edge, const int fret) const
{
    const FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match =
        statedPlacement(after.placements, session(), fretHandResolutions().events, position);
    if (match == after.placements.end())
    {
        return std::unexpected{ChartPlanNoChange{}};
    }
    *match = retyped(*match, edge, fret);
    std::expected<JudgedMarkerModel<FretHandPositionsSnapshot>, ChartPlanRefusal> judged =
        judgeMarkerModel(before, std::move(after));
    if (!judged.has_value())
    {
        return std::unexpected{std::move(judged.error())};
    }
    return ChartFretHandPlan{.placements = std::move(judged->model), .edge = edge};
}

// The settle of a placement's fret entry. Its selection names the placement by position, which the
// fret does not move, so it stays selected with nothing to re-select.
void EditorController::Impl::commitFretHandFret(ChartFretHandPlan plan)
{
    static_cast<void>(commitMarkerModel(
        FretHandPositionsSnapshot::capture(session()),
        std::move(plan.placements),
        markerVerbLabel(
            plan.edge == FretHandEdge::End ? MarkerVerb::SetHandPositionEndFret
                                           : MarkerVerb::SetHandPositionStartFret)));
}

// The chip a pending fret entry draws: the position with the typed value at its edge, at the
// narrowest window where no end is stated.
common::core::FhpViewState EditorController::Impl::fretHandEntryChip(
    const common::core::GridPosition& position, const FretHandEdge edge, const int value) const
{
    FretHandPositionsSnapshot placements = FretHandPositionsSnapshot::capture(session());
    const auto match =
        statedPlacement(placements.placements, session(), fretHandResolutions().events, position);
    const common::core::FretHandPosition typed =
        match == placements.placements.end()
            ? common::core::FretHandPosition{.position = position, .fret = value}
            : retyped(*match, edge, value);
    // Bound once so the presence test and the read are provably one object.
    const std::optional<int>& end_fret = typed.end_fret;
    return common::core::FhpViewState{
        .position = position,
        .fret = typed.fret,
        .width = end_fret.has_value()
                     ? common::core::FretWindow::through(typed.fret, *end_fret).width
                     : common::core::g_min_fret_hand_width,
        .authored = end_fret.has_value() ? common::core::FhpAuthored::FretAndEnd
                                         : common::core::FhpAuthored::Fret,
    };
}

// Alt+Delete over a selected placement: its authored end fret is cleared, and the notes derive the
// window's reach again. A position whose end is already derived — a derived position included,
// which states nothing — has nothing to clear, so the verb does nothing: no commit, no undo entry,
// and never the placement's own deletion, which is Delete's. The placement stays selected: its
// position, which names it, is untouched.
void EditorController::Impl::performActionImpl(EditorAction::ClearFretHandEnd /*action*/)
{
    const auto* const selection = std::get_if<FretHandPositionSelection>(&m_selection);
    if (selection == nullptr)
    {
        return;
    }
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection->position);
    if (match == after.placements.end() || !match->end_fret.has_value())
    {
        return;
    }
    match->end_fret.reset();
    static_cast<void>(commitMarkerModel(
        std::move(before),
        std::move(after),
        markerVerbLabel(MarkerVerb::ClearHandPositionEndFret)));
}

// Deletes the selected placement. Nothing refuses an authored one — a chart with no placement left
// derives every window from its notes, a legal stream. Where the derivation puts a position at the
// same place, the chip stands on, derived, and stays selected; otherwise the row's previous marker
// is selected (deleteSelectedMarker). A derived position stores nothing to delete, so it refuses.
void EditorController::Impl::deleteSelectedFretHandPosition(
    const FretHandPositionSelection& selection)
{
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection.position);
    if (match == after.placements.end())
    {
        refuseDerivedFretHandPosition(selection, MarkerVerb::DeleteHandPosition);
        return;
    }

    after.placements.erase(match);
    commitMarkerModel(
        std::move(before), std::move(after), markerVerbLabel(MarkerVerb::DeleteHandPosition));
}

// The prune: every stored position the derivation does not need goes, as one undo entry; a chart
// where every one is needed records nothing and says so. The selection is untouched — a pruned
// position's chip still stands, derived now, so a selection naming it names a marker still.
void EditorController::Impl::performActionImpl(EditorAction::PruneFretHandPositions /*action*/)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const std::string label = markerVerbLabel(MarkerVerb::PruneHandPositions);
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after{
        .placements = withoutRedundantPositions(
            before.placements, chartResolutions(), session().song().tempo_map),
    };
    if (after == before)
    {
        m_refusal_log.markerEdit(label, "no stored hand position is redundant");
        return;
    }
    static_cast<void>(commitMarkerModel(std::move(before), std::move(after), label));
}

} // namespace rock_hero::editor::core
