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

#include <cstdint>
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

Funnels through the shared finalize like every note plan, so the whole-matrix gate refuses a
deletion that would leave the chart invalid. A survivor whose CONNECTION the deletion broke keeps
its claim and simply plays as a pick until the next settle flattens it (\ref planSettleLegato) —
relational truths are not the burst's business.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param keys Notes to delete, sorted ascending (the ChartSelection order — lookups binary-search
this precondition); keys with no matching note are skipped.
\return The plan, or empty when no key matched.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planDeleteNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys);

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

Two modes: transposing (the default) shifts every note by the same delta
so the snapshot's lowest fret lands on the target — shape-preserving, so chords reposition,
runs transpose, and a single note retypes exactly — while set-exact assigns the target to
every note. Members can never go below zero under transposition because the lowest fret is
the anchor; a member pushed past the fret cap refuses the whole plan, never clamps.

The base is a snapshot rather than the live chart so the multi-digit entry window can replan
the whole entry from the pre-entry originals while widening; the retyped values are swapped into
the live stream for the shared finalize, whose whole-matrix gate replaces the old local fret
caps — any out-of-range or rule-violating result refuses the plan outright.

A pick-slide member's path translates with its start fret — the whole gesture shifts by the
member's delta, preserving travel.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param base Snapshot of the notes being retyped.
\param target Typed fret: the exact value (set-exact) or where the lowest fret lands.
\param set_exact True to assign the target to every note instead of transposing.
\return The plan, or nullopt when refused, nothing changes, or the snapshot is empty.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, int target, bool set_exact);

/*!
\brief Plans adjusting the keyed notes' sustains by an exact beat delta.

Sustains floor at zero — at the minimum gesture window on pick slides, whose path re-terminates
onto the changed tail (shrink compresses the final point, growth rides it out). Growth clamps
against the next same-string onset (40-Q2-B) and payload points beyond a shortened sustain are
clipped with it.

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

/*! \brief Why an `H` press left a selected note as it found it. */
enum class ChartLegatoSkip : std::uint8_t
{
    /*! \brief Nothing was skipped. */
    None,

    /*! \brief The onset is the picking hand's (tap, pinch, scrape) — no connection describes it. */
    PickingHandOnset,

    /*! \brief Nothing earlier on the note's own string to connect to. */
    NoPredecessor,

    /*! \brief The predecessor's hold ends too early, and its tail could not be grown to reach. */
    PredecessorReleased,

    /*! \brief A predecessor that reaches, but no connection between the two stops. */
    NoConnection,

    /*!
    \brief Number of reasons, not a reason — the tally array's size.

    Structural on purpose: a new reason widens the array at compile time instead of throwing
    `std::out_of_range` out of a keystroke handler. Keep it last.
    */
    Count
};

/*!
\brief One `H` press's outcome: the change to apply, and what the resolver refused.

The skip channel exists so an all-skipped press is never a dead key. It counts only notes the
resolver REFUSED — a note already carrying the claim the press would set is unchanged, not skipped,
which is what lets the caller tell "nothing left to claim, so this press means clear" from "this
press had nothing to say".
*/
struct [[nodiscard]] ChartLegatoPlan
{
    /*! \brief The planned change, or empty when no selected note gained a claim. */
    std::optional<ChartNotesEditPlan> plan;

    /*! \brief How many selected notes the resolver refused a claim for. */
    int skipped{0};

    /*! \brief The reason most of those notes were refused for. */
    ChartLegatoSkip reason{ChartLegatoSkip::None};
};

/*!
\brief Plans claiming a legato connection for every selected note whose claim resolves.

The planner is the oracle and \ref common::core::resolveLegato is the only authority: each selected
note in the connection family (\ref common::core::legatoClaimable) is asked what a claim on it would
resolve to, and the claim is written exactly where the answer is a real motion. There is no separate
eligibility list to keep in step — a note is skipped because the resolver refused it, never because
a rule restated here said so. Direction is never written: the stored claim is the whole authored
statement, and both surfaces read the motion back through the same resolver.

The assist authors the missing half of a claim rather than demanding it first: when the
predecessor's hold stops short of the onset, its tail grows to the margin point in the SAME plan,
but only when that makes the claim resolve and only within `sustainGrowthLimit`, so the assist can
never author what a manual drag could not reach. It skips a gesture-carrying predecessor — a scrape,
or any note with a slide-out — because that tail is the gesture's authored window, not slack to
spend.

\param chart Chart the plan is built against.
\param tempo_map Tempo map the plan resolves distances through.
\param keys Notes to set, sorted-unique in chart order.
\param label User-visible undo label.

\return The planned change plus the skip report; the plan is empty when no selected note's claim
        resolves, which is what makes the press mean clear.
*/
[[nodiscard]] ChartLegatoPlan planSetLegato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, std::string_view label);

/*!
\brief Plans flattening every legato claim the chart no longer justifies — the settle sweep's plan.

The editor half of \ref common::core::sweepUnjustifiedLegato: the sweep decides WHAT flattens, this
turns it into an undo entry. Nothing else here is relational, which is why the sweep runs at settle
points instead of inside \ref finalizePlan — mid-burst a broken claim simply displays as the pick it
plays as, and the burst stays one undo step.

`base` is the stream the returned plan is expressed against, which is not always the current chart:
folding the flatten into the burst's own entry needs a plan spanning the whole burst, so the caller
passes the pre-burst stream and reverses the burst before applying. A caller pushing the flatten as
its own entry passes `chart.notes`.

\param chart Chart being settled; its notes are swept and its shapes supply the hold test.
\param tempo_map Tempo map supplying the beat axis.
\param base Stream the plan is diffed against.
\param label User-visible undo label.

\return The planned change, or empty when THE SWEEP found nothing to flatten — which is exactly when
        the caller must leave its coalescing windows armed. A present plan can itself be empty (the
        flatten exactly cancelled the burst it is diffed against); that is still a commit, because
        walking the chart to `base` is what removes the claim.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planSettleLegato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, std::string_view label);

/*!
\brief Plans setting the keyed notes' attack, with the pick-slide entry and exit special cases.

Notes already carrying the attack are left alone. Entering a pick slide keeps the note's fret
as the scrape start and synthesizes the default path toward the far default endpoint (downward
from the neck's upper half, upward from the lower), replacing any pitched glide the note
carried — undo restores that; the overridden techniques stay in memory per the chart contract
(chart.h), so toggling the attack back within the session restores them untouched. A zero
sustain first extends to the minimum gesture window so the path can travel. Leaving a pick
slide clears the path — gesture geometry has no meaning as a pitched glide — and touches
nothing else.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param keys Notes whose attack changes, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param attack Attack every keyed note receives.
\param label User-visible undo label.
\return The plan, or empty when nothing changes.
*/
[[nodiscard]] std::optional<ChartNotesEditPlan> planSetAttack(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::NoteAttack attack, std::string_view label);

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
