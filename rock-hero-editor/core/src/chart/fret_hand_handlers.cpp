#include "chart/fret_hand_positions_snapshot.h"
#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

// The hand row's markers (docs/plans/in-progress/hand-marker-stopgap.md): the chart's stored
// fret-hand positions, authored through the marker grammar every other marker kind shares. Only
// where the hand arrives and its index finger's fret are stored; the window's reach is derived.

namespace
{

// Finds the placement arriving exactly at a position; a placement's position is its identity.
[[nodiscard]] std::vector<common::core::FretHandPosition>::iterator findPlacement(
    std::vector<common::core::FretHandPosition>& placements,
    const common::core::GridPosition& position)
{
    return std::ranges::find(placements, position, &common::core::FretHandPosition::position);
}

// The placement an insert at a position makes: the fret defaults to the lowest fretting-hand stop
// its stretch holds, asked of the one fold that also derives every window's width
// (deriveHeldFretRanges) over the stream with the new placement in it, so "its stretch" is exactly
// the stretch it will govern. A stretch holding no stop keeps the hand where the placement before
// it put it, and the nut window (fret 1) where there is none. The fret is fitted onto the board
// afterwards, by the one normalizer every stored placement passes.
[[nodiscard]] common::core::FretHandPosition defaultedPlacement(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const common::core::GridPosition& position)
{
    std::vector<common::core::FretHandPosition> placements = chart.fret_hand_positions;
    const auto at = std::ranges::upper_bound(
        placements, position, std::ranges::less{}, &common::core::FretHandPosition::position);
    const auto index = static_cast<std::size_t>(std::distance(placements.begin(), at));
    placements.insert(at, common::core::FretHandPosition{.position = position, .fret = 1});

    // Resolved exactly as the projection resolves it, so the claims read here are the claims the
    // drawn window is derived from.
    const std::vector<std::optional<common::core::HeldFretRange>> held =
        common::core::deriveHeldFretRanges(
            common::core::chartResolutions(chart.notes, tempo_map), placements, tempo_map);

    common::core::FretHandPosition placement{.position = position, .fret = 1};
    // Bound once so the presence test and the read are provably one object.
    if (const std::optional<common::core::HeldFretRange>& stretch = held[index];
        stretch.has_value())
    {
        placement.fret = stretch->lowest;
    }
    else if (index > 0)
    {
        placement.fret = placements[index - 1].fret;
    }
    // The repairs are not a refusal: fitting the defaulted window onto the board IS the default.
    static_cast<void>(common::core::normalizeFretHandPosition(placement, chart.tuning));
    return placement;
}

// The hand chord's restate: the placement arriving at the cursor, which it selects.
struct RestateAt
{
    common::core::GridPosition position;
};

// The hand chord's insert: the placement it will add, fret already defaulted.
struct InsertAt
{
    common::core::FretHandPosition placement;
};

// What the hand chord does, or nothing.
using HandChordTarget = std::variant<std::monostate, RestateAt, InsertAt>;

// THE hand chord's precedence, stated once: it authors at the cursor and never reads the selection.
// A placement arriving exactly there is restated, which is what keeps a second press from inserting
// a second placement on the first one's start; anywhere else a marker may start, the verb is an
// insert. Nothing without a chart, without a cursor (playing, no song), or on the closing barline,
// so the chord is inert exactly where the commit would refuse it.
[[nodiscard]] HandChordTarget handChordTarget(
    const common::core::Arrangement* const arrangement,
    const std::optional<common::core::GridPosition>& cursor,
    const common::core::TempoMap& tempo_map)
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
    return InsertAt{.placement = defaultedPlacement(chart, tempo_map, position)};
}

} // namespace

// A hand chip click selects the placement it names and seeks nothing, exactly as a ruler chip does,
// through the select every path onto a marker shares, so the caret demotes.
void EditorController::Impl::performActionImpl(const EditorAction::SelectFretHandPosition& action)
{
    if (action.index < markerStarts(MarkerRow::Hand).size())
    {
        selectMarker(markerSelectionAt(MarkerRow::Hand, action.index));
    }
    updateView();
}

// The hand chord runs its own target: the verb has nothing to prompt for, so the precedence is
// decided and performed here, at the press, rather than published for a surface to hand back. The
// cursor is read at the placement quantum — the slot an arrow press would arm on, as the tone
// chord reads it. A restate only selects, since a placement's fret has no entry to re-open yet;
// an insert that lands is left SELECTED, so Delete and Alt+arrows act on it next.
void EditorController::Impl::performActionImpl(
    EditorAction::AuthorFretHandPositionAtCursor /*action*/)
{
    const HandChordTarget target = handChordTarget(
        session().currentArrangement(),
        cursorPosition(placementQuantum()),
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
    if (commitMarkerModel(std::move(before), std::move(after), "Add Hand Position"))
    {
        // The insert selects what it made, and a selection made after the commit publishes with
        // this refresh: the commit's own publish ran before it existed.
        selectMarker(FretHandPositionSelection{.position = insert->placement.position});
        updateView();
    }
}

// Steps the selected placement one placement-quantum line, the tone change's step. Refused rather
// than clamped: onto a start another placement holds, and past the song's end, by the stream's
// rules at the commit; off the chart's start here, because the grid step collapses onto the start
// it came from there and the commit would record that as a move that changed nothing. A landed
// move brings the paused cursor to the new start, as every selection move of a marker does.
void EditorController::Impl::moveSelectedFretHandPosition(
    const FretHandPositionSelection& selection, const ChartStepDirection direction)
{
    constexpr const char* label = "Move Hand Position";
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection.position);
    if (match == after.placements.end())
    {
        // The selection went stale; moving nothing is the answer.
        return;
    }
    const common::core::GridPosition target =
        steppedNudgePosition(selection.position, direction == ChartStepDirection::Right);
    if (target == selection.position)
    {
        logMarkerRefusal(label, "fret-hand position would leave the chart");
        return;
    }

    match->position = target;
    if (commitMarkerModel(std::move(before), std::move(after), label))
    {
        // The position IS the placement's identity, so the commit released a selection that now
        // names nothing; the moved placement is re-selected here, and followMovedMarker publishes
        // it together with the cursor it brings along.
        selectMarker(FretHandPositionSelection{.position = target});
        followMovedMarker(target);
    }
}

// Deletes the selected placement. Nothing refuses it — a chart with no placement left is the nut
// window throughout, a legal stream — and nothing stays selected: the commit releases a selection
// naming the placement that is gone.
void EditorController::Impl::deleteSelectedFretHandPosition(
    const FretHandPositionSelection& selection)
{
    FretHandPositionsSnapshot before = FretHandPositionsSnapshot::capture(session());
    FretHandPositionsSnapshot after = before;
    const auto match = findPlacement(after.placements, selection.position);
    if (match == after.placements.end())
    {
        return;
    }

    after.placements.erase(match);
    commitMarkerModel(std::move(before), std::move(after), "Delete Hand Position");
}

} // namespace rock_hero::editor::core
