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
// So a verb that STATES something of a derived position — a fret entry, the move — authors it there
// first, and a verb that needs something stored — Delete, Alt+Delete — refuses one.

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

// Why a verb that needs a STORED position — Delete, Alt+Delete — refuses a derived one, which
// stores nothing to remove.
[[nodiscard]] std::string derivedFretHandPositionRefusal(const FretHandPositionSelection& selection)
{
    return common::core::formatHandPositionToken(selection.position) +
           " is derived, so nothing is stored there";
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
    selectMarkerAt(MarkerRow::FretHandPosition, action.index);
}

// THE fret-hand position chord's precedence, stated once and performed at the press, as every
// marker chord's is. It authors at the cursor and never reads the selection; the cursor is read at
// the placement quantum — the slot an arrow press would arm on, as the tone chord reads it. A
// placement arriving exactly there is restated, which only selects it (a digit then retypes its
// fret) and keeps a second press from inserting a second placement on the first one's start;
// anywhere else a marker may start, a placement is inserted, defaulted from the chart's hand
// events, and left SELECTED, so Delete and Alt+arrows act on it next. On the closing barline, where
// nothing may start, the press is refused; without a chart or a cursor (playing, no song) it
// addresses nothing and says nothing.
void EditorController::Impl::performActionImpl(
    EditorAction::AuthorFretHandPositionAtCursor /*action*/)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::optional<common::core::GridPosition> cursor = cursorPosition(placementQuantum());
    if (arrangement == nullptr || !arrangement->chart.has_value() || !cursor.has_value())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::GridPosition position = *cursor;
    if (std::ranges::contains(
            chart.fret_hand_positions, position, &common::core::FretHandPosition::position))
    {
        selectMarker(FretHandPositionSelection{.position = position});
        updateView();
        return;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    if (!common::core::objectCanStartAt(position, tempo_map))
    {
        refuseAtSongEnd(MarkerVerb::AddHandPosition);
        return;
    }

    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    after.placements.push_back(
        defaultedPlacement(fretHandResolutions().events, chart.tuning.capo, tempo_map, position));
    // The subject is the place the insert addressed, which lights the derived chip standing there.
    if (commitMarkerModel(
            std::move(before),
            std::move(after),
            markerVerbLabel(MarkerVerb::AddHandPosition),
            lightOf(FretHandPositionSelection{.position = position})) == MarkerCommit::Refused)
    {
        return;
    }
    // The insert selects what it made, and a selection made after the commit publishes with this
    // refresh: the commit's own publish ran before it existed.
    selectMarker(FretHandPositionSelection{.position = position});
    updateView();
}

// Steps the selected placement one placement-quantum line, the tone change's step. A derived
// position is authored where it stands first (statedPlacement), so the one commit states it and
// moves it. Refused rather than clamped, with the chip glowing red: onto a start another placement
// holds, past the song's end, or onto a gripped note the window does not cover, by the stream's
// rules at the commit; and here where the grid step has nowhere to go, off either end of the chart
// (steppedNudgePosition). A landed move brings the paused cursor to the new start, as every
// selection move of a marker does.
void EditorController::Impl::moveSelectedFretHandPosition(
    const FretHandPositionSelection& selection, const ChartStepDirection direction)
{
    const std::string label = markerVerbLabel(MarkerVerb::MoveHandPosition);
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = statedPlacement(
        after.placements, session(), fretHandResolutions().events, selection.position);
    if (match == after.placements.end())
    {
        return;
    }
    const std::optional<common::core::GridPosition> step =
        steppedNudgePosition(selection.position, direction == ChartStepDirection::Right);
    if (!step.has_value())
    {
        refuseMarkerVerb(label, "fret-hand position would leave the chart", lightOf(selection));
        return;
    }
    const common::core::GridPosition target = *step;

    match->position = target;
    if (!commitMarkerMove(
            std::move(before),
            std::move(after),
            label,
            FretHandPositionSelection{.position = target},
            lightOf(selection)))
    {
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
std::expected<EditorController::Impl::ChartFretHandPlan, PlanRefusal> EditorController::Impl::
    planFretHandFret(
        const common::core::GridPosition& position, const FretHandEdge edge, const int fret) const
{
    const FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match =
        statedPlacement(after.placements, session(), fretHandResolutions().events, position);
    if (match == after.placements.end())
    {
        return std::unexpected{PlanNoChange{}};
    }
    *match = retyped(*match, edge, fret);
    std::expected<JudgedMarkerModel<FretHandPositionsSnapshot>, PlanRefusal> judged =
        judgeMarkerModel(before, std::move(after));
    if (!judged.has_value())
    {
        // The funnel's refusal is the entry's as it stands: what the rule lit — the notes a
        // narrowed window leaves uncovered — beside which the entry's report lights the chip.
        return std::unexpected{std::move(judged.error())};
    }
    return ChartFretHandPlan{.placements = std::move(judged->model), .edge = edge};
}

// The settle of a placement's fret entry. Its selection names the placement by position, which the
// fret does not move, so it stays selected with nothing to re-select. The plan was judged by the
// funnel's own front half against the model it lands on — the settle prologue replans first — and
// only a plan the judge passed settles, so the commit lands what was already judged and lights
// nothing of its own.
void EditorController::Impl::commitFretHandFret(ChartFretHandPlan plan)
{
    static_cast<void>(commitMarkerModel(
        FretHandPositionsSnapshot::capture(session()),
        std::move(plan.placements),
        markerVerbLabel(
            plan.edge == FretHandEdge::End ? MarkerVerb::SetHandPositionEndFret
                                           : MarkerVerb::SetHandPositionStartFret),
        RefusalLight{}));
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
// window's reach again; the Note Lock refuses it at dispatch, before anything here answers. A
// derived position stores nothing to clear, so it refuses as Delete and the move refuse it. An
// authored one whose end is already derived is already so: no commit, no undo entry, and never the
// placement's own deletion, which is Delete's. The placement stays selected: its position, which
// names it, is untouched.
void EditorController::Impl::performActionImpl(EditorAction::ClearFretHandEnd /*action*/)
{
    const auto* const selection = std::get_if<FretHandPositionSelection>(&m_selection);
    if (selection == nullptr)
    {
        return;
    }
    const std::string label = markerVerbLabel(MarkerVerb::ClearHandPositionEndFret);
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection->position);
    if (match == after.placements.end())
    {
        refuseMarkerVerb(label, derivedFretHandPositionRefusal(*selection), lightOf(*selection));
        return;
    }
    if (!match->end_fret.has_value())
    {
        return;
    }
    match->end_fret.reset();
    static_cast<void>(
        commitMarkerModel(std::move(before), std::move(after), label, lightOf(*selection)));
}

// Deletes the selected placement. Nothing refuses an authored one — a chart with no placement left
// derives every window from its notes, a legal stream. Where the derivation puts a position at the
// same place, the chip stands on, derived, and stays selected; otherwise the row's previous marker
// is selected (deleteSelectedMarker). A derived position stores nothing to delete, so it refuses.
void EditorController::Impl::deleteSelectedFretHandPosition(
    const FretHandPositionSelection& selection)
{
    const std::string label = markerVerbLabel(MarkerVerb::DeleteHandPosition);
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection.position);
    if (match == after.placements.end())
    {
        refuseMarkerVerb(label, derivedFretHandPositionRefusal(selection), lightOf(selection));
        return;
    }

    after.placements.erase(match);
    commitMarkerModel(std::move(before), std::move(after), label, lightOf(selection));
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
        refuseMarkerVerb(label, "no stored hand position is redundant", RefusalLight{});
        return;
    }
    // Every stored position is its subject, which no one chip draws: only what a rule names lights.
    static_cast<void>(
        commitMarkerModel(std::move(before), std::move(after), label, RefusalLight{}));
}

} // namespace rock_hero::editor::core
