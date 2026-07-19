/*!
\file chart_edits.h
\brief Chart note edit planning and the concrete undo edit applied through the editor history.

Every mutation follows one shape: a pure planner builds the note stream the edit should produce,
normalizes same-string sustain overlaps per 40-Q2-B (the earlier note auto-truncates, payloads
clipped to the shortened sustain, all inside the same undo entry), and diffs against the current
stream into a removed/inserted plan. Applying, undoing, and redoing are then the same primitive
run in opposite directions, so undo round-trips are exact by construction.
*/

#pragma once

#include "chart/chart_selection.h"
#include "controller/editor_undo_history.h"

#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One planned chart-note mutation: full values removed and inserted, plus its label. */
struct [[nodiscard]] ChartNotesEditPlan
{
    /*! \brief Notes removed from the stream, full values in chart order. */
    std::vector<common::core::ChartNote> removed;

    /*! \brief Notes inserted into the stream, full values in chart order. */
    std::vector<common::core::ChartNote> inserted;

    /*! \brief User-visible undo label. */
    std::string label;
};

/*!
\brief Plans placing one note, replacing any note already on its (position, string) slot.

The placed note's sustain clamps against the next same-string onset and any earlier same-string
sustain ringing across the onset truncates (40-Q2-B), all in the one plan.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param note Note to place; the caller owns position/string/fret validity.
\return The plan, or empty when the placement changes nothing.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note);

/*!
\brief Plans deleting the notes matching the given keys.
\param chart Chart being edited.
\param keys Notes to delete, sorted ascending (the ChartSelection order — lookups binary-search
this precondition); keys with no matching note are skipped.
\return The plan, or empty when no key matched.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planDeleteNotes(
    const common::core::Chart& chart, const std::vector<ChartNoteKey>& keys);

/*!
\brief Plans moving the keyed notes by an exact beat delta and/or a string delta.

Refused (empty) when any moved note would leave the chart's string range or land on a slot
occupied by an unmoved note — validation-preserving edits only, never clamped. Overlaps created
at the destinations truncate per 40-Q2-B.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis.
\param keys Notes to move, sorted ascending (the ChartSelection order — lookups binary-search
this precondition).
\param beat_delta Signed exact beat delta.
\param string_delta Signed string-lane delta.
\param label User-visible undo label.
\return The plan, or empty when refused or nothing changes.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planMoveNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::Fraction beat_delta, int string_delta,
    std::string_view label);

/*!
\brief Plans retyping a snapshot of selected notes toward a typed fret target.

Two modes (settled 2026-07-17): transposing (the default) shifts every note by the same delta
so the snapshot's lowest fret lands on the target — shape-preserving, so chords reposition,
runs transpose, and a single note retypes exactly — while set-exact assigns the target to
every note. Members can never go below zero under transposition because the lowest fret is
the anchor; a member pushed past the fret cap refuses the whole plan, never clamps.

The base is a snapshot rather than the live chart so the multi-digit entry window can replan
the whole entry from the pre-entry originals while widening.

\param base Snapshot of the notes being retyped.
\param target Typed fret: the exact value (set-exact) or where the lowest fret lands.
\param set_exact True to assign the target to every note instead of transposing.
\return The plan (empty-removed when nothing changes), or nullopt when refused (fret cap) or
the snapshot is empty.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planRetypeFrets(
    const std::vector<common::core::ChartNote>& base, int target, bool set_exact);

/*!
\brief Plans adjusting the keyed notes' sustains by an exact beat delta.

Sustains floor at zero; growth clamps against the next same-string onset (40-Q2-B) and payload
points beyond a shortened sustain are clipped with it.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis.
\param keys Notes whose sustains change, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param beat_delta Signed exact beat delta.
\return The plan, or empty when nothing changes.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planAdjustSustain(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::Fraction beat_delta);

/*!
\brief Applies a removed/inserted note change atomically to a chart.

Verifies every removed note still matches by full value and every inserted slot is free, then
swaps in the new stream; a failed precondition leaves the chart untouched.

\param chart Chart to mutate.
\param to_remove Full note values to remove.
\param to_insert Full note values to insert, keeping (position, string) order.
\return Empty success, or PreflightRejected when the chart no longer matches the plan.
*/
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyChartNotesChange(
    common::core::Chart& chart, const std::vector<common::core::ChartNote>& to_remove,
    const std::vector<common::core::ChartNote>& to_insert);

/*! \brief Inverse-command edit replaying a planned chart-note change in either direction. */
struct [[nodiscard]] ChartNotesEdit final : IEdit
{
    /*!
    \brief Captures a planned chart-note change.
    \param plan_value The applied plan whose directions this edit replays.
    */
    explicit ChartNotesEdit(ChartNotesEditPlan plan_value)
        : plan(std::move(plan_value))
    {}

    /*!
    \brief Removes the inserted notes and restores the removed ones.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the planned change.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label for the planned change. */
    [[nodiscard]] std::string label() const override;

    /*! \brief The applied plan replayed by undo and redo. */
    ChartNotesEditPlan plan;
};

} // namespace rock_hero::editor::core
