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
#include <rock_hero/editor/core/chart/chart_technique.h>
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
\brief Why a planner returned no plan: a valid no-op is not a refusal.

The two emptinesses used to share one `std::nullopt`, which made every refusal in the editor
silent — no caller could tell "this edit is not allowed" from "this edit changes nothing", so
nothing could report the former without lying about the latter. W3's pending fret entry is the
consumer that forces the split: a provisional value that plans to a no-op is VALID and must not
paint red. A bare enum rather than a code-plus-message error type on purpose: both reasons map to
fixed meanings, the callers branch rather than display, and any user-facing text belongs to the
surface that shows it.
*/
enum class ChartPlanRefusal : std::uint8_t
{
    /*! \brief The edit would change nothing a document records — legal, just empty. */
    NoChange,

    /*! \brief The result would break a chart rule (or a planner's own bound), so the whole plan
    is refused, never clamped. */
    Invalid,
};

/*!
\brief Plans placing one note, replacing any note already on its (position, string) slot.

Every note rings, so a placement authors a duration: `default_sustain` becomes the placed note's
ring, clamped against the next onset on its own string, and any earlier same-string ring crossing
the new onset truncates (40-Q2-B) — all in the one plan. The default is the caller's because it is
a SESSION fact (the grid step the user is working at), not a chart one; `note.sustain` is
overwritten rather than read, so there is only one channel for it.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param note Note to place; the caller owns position/string/fret validity.
\param default_sustain Ring the placed note gets before the same-string clamp; the session's
current grid step.
\return The plan; NoChange when the placement changes nothing, Invalid when the gate refuses it.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note, common::core::Fraction default_sustain);

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
\return The plan; NoChange when no key matched, Invalid when the gate refuses the deletion.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planDeleteNotes(
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
\return The plan; NoChange when nothing moves or changes, Invalid when a destination leaves the
        neck, collides, or fails the gate.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planMoveNotes(
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

Retyping edits exactly the selected notes' own frets — a slide's path never rides along, in
either mode (the fret-verb law: every waypoint was placed on its fret on purpose). A scrape
start retyped onto its first path position refuses through the finalize gate's always-traveling
rule; a pitched slide's equal-fret start is the legal hold encoding and passes.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param base Snapshot of the notes being retyped.
\param target Typed fret: the exact value (set-exact) or where the lowest fret lands.
\param set_exact True to assign the target to every note instead of transposing.
\return The plan; NoChange when the snapshot is empty or the retype changes nothing, Invalid
        when the gate refuses the result. The split is what lets the pending entry paint a
        refused value red without painting a valid no-op red.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, int target, bool set_exact);

/*!
\brief One step of a duration gesture: the lattice its end lands on, and which way it moves.

A gesture records its steps rather than their sum, because a step has no size of its own to sum: it
moves the ring's END onto the adjacent line of a lattice, so what it adds depends on where that end
currently sits and on the meter of the measure it lands in. A step is therefore fully described by
the note value it snapped by plus a direction.
*/
struct ChartSustainStep
{
    /*!
    \brief Note value the step snaps onto: the placement quantum at the moment of the step.

    The note VALUE, never a precomputed beat amount: the planner needs the lattice to land on, and
    the local meter scales the value there (one 1/4 step is one beat in x/4 and two in x/8), so a
    ring crossing a meter change still lands on that meter's own lines. A gesture that spans a snap
    toggle or a grid change replays each step on the lattice it was made against.
    */
    common::core::Fraction note_value;

    /*! \brief True when the step lengthens the ring, false when it shortens it. */
    bool grow{};
};

/*!
\brief Plans the keyed notes' sustains by replaying a gesture's steps from the rings it started at.

The duration verb is a GESTURE (user ruling 2026-08-22), not a run of independent steps: the caller
records every step in press order, and every keyed note is recomputed by replaying the whole run
over its PRE-GESTURE ring. That is what makes the verb symmetric — each note replays the same steps
from where it started, so whatever shape the selection's tails had is preserved in both directions,
a member pinned at its own bound on the way out rejoins the others exactly where it left them on the
way back, and nothing blocks anything else: a passage of different-length tails can all be pushed as
far as each one can go. Stepping from the LIVE ring instead is what cannot do that — a clamp or a
floor would become the next step's starting value, and the selection would come back a different
shape than it went out.

A step moves a POSITION, not a length, which is why the gesture keeps the steps and not one delta
(user bug 2026-08-23): it moves the ring's END — the note's onset plus its ring, an absolute
position — to the adjacent lattice line strictly beyond it in the step's direction, through the one
keyboard step primitive the caret and the lane nudge already share
(\ref adjacentTempoGridPosition). From an on-lattice end that is exactly one step, as a summed
delta was; from an end between lines it SNAPS, ceiling when growing and flooring when shrinking. No
snapping rule is restated here.

`base` is the stream the gesture started from. Each keyed note's pre-gesture ring is read from it,
and the returned plan is diffed against it, so the plan always describes start → now and can replace
the entry the gesture's first step pushed. On a gesture's first step `base` IS `chart.notes` and the
result is an ordinary one-step edit.

Two rules bound the replayed ring, and neither is fed back into the replay — they judge its answer,
so a clamp never becomes the next step's starting value:

- Growth clamps at exact adjacency with the next onset on the note's own string (40-Q2-B,
  \ref common::core::sustainBoundOf), the model's one bound on a ring. A note pinned there reports
  the bound for every step past it, and leaves the bound on the step that falls back inside.
- Every note rings, so there is no empty ring to shrink to: a note whose replayed ring is not
  positive keeps the ring it CURRENTLY has — read from `chart`, not from `base`, because the value
  on screen is the one that holds — and rejoins the replay as soon as it is positive again. A
  scrape floors at the minimum gesture window instead, its path re-terminating onto the changed
  tail (shrink compresses the final point, growth rides it out).

Payload beyond a shortened ring is clipped with it, out of the PRE-GESTURE payload, so growing back
restores what an earlier step's shrink clipped away.

Three consequences of the step's law, all intended:

- A gesture whose steps all share one lattice, starting from a ring already ON that lattice, is
  exactly reversible: every step lands where its opposite steps back through, and neither bound
  enters the replay to bake itself in.
- A step from a ring that sits BETWEEN that lattice's lines snaps, so reversing it lands on the
  line below rather than on the ring the gesture started from. That is the point of the verb: a
  step means "put the end on the line", and a remainder that survived it would make the visible
  grid a lie for the rest of the session.
- A chord whose members sit at different offsets snaps each member to its OWN next line, because
  the replay runs per note from that note's own end. Members already sharing a line stay together.

\param chart Chart being edited, live: the ring bounds, and the ring a floored note holds.
\param tempo_map Tempo map supplying the beat axis and the meter each grid step lands in.
\param base Stream the gesture started from: the pre-gesture rings, and the stream the plan is
diffed against.
\param keys Notes whose sustains change, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param steps The gesture's steps in press order, replayed in that order over every keyed note.
\return The plan the gesture's entry should hold; NoChange when the replay puts every ring back
        where `base` had it (or when there are no steps yet), which means the gesture describes no
        edit at all — the caller's answer is to RETIRE the entry it pushed rather than replace it
        with one that describes nothing; Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planAdjustSustain(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const std::vector<ChartNoteKey>& keys,
    const std::vector<ChartSustainStep>& steps);

/*! \brief Why an `H` press left a selected note as it found it. */
enum class ChartLegatoSkip : std::uint8_t
{
    /*! \brief Nothing was skipped. */
    None,

    /*! \brief The onset is the picking hand's (tap, pinch, scrape) — no connection describes it. */
    PickingHandOnset,

    /*! \brief Nothing earlier on the note's own string to connect to. */
    NoPredecessor,

    /*! \brief The predecessor's ring stops before the onset, and could not be grown to reach. */
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
predecessor's ring stops short of the onset, it grows to that ONSET — exact adjacency — in the SAME
plan, but only when that makes the claim resolve. It can never author what a manual drag could not
reach, and needs no bound of its own to say so: the claiming note IS the next onset on the
predecessor's string, so the target is exactly that predecessor's
\ref common::core::sustainBoundOf. It skips a trail-off predecessor — any note with a slide-out —
because that tail is the gesture's authored window, not slack to spend. (A scrape needs no such
guard: the resolver disqualifies it outright, so its ring is never the only blocker.)

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
\return The plan; NoChange when nothing changes (an ineligible note is skipped, not a refusal),
        Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planSetAttack(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::NoteAttack attack, std::string_view label);

/*!
\brief Which of a note's independent boolean techniques a toggle verb writes.

Not a family by meaning — a palm mute and a tremolo have nothing musical in common — but by SHAPE:
each is one bool on \ref common::core::ChartNote that a verb sets over a selection under the
uniform-scope law, with eligibility asked of the per-note rule authority. Grouping them is what
keeps that law written once instead of once per technique, and every one of them acquires a new
rule for free when the rules change (a dead note's refusal of vibrato, a tap harmonic's refusal
of tremolo).
*/
enum class ChartNoteFlag : std::uint8_t
{
    /*! \brief The picking hand's palm damping the string: still pitched, but damped. */
    PalmMute,

    /*! \brief The string deadened into an unpitched click. */
    Dead,

    /*! \brief Unmeasured repeated picking: as fast as possible, no real timing. */
    Tremolo,

    /*! \brief The fretting hand oscillating the stopped pitch. */
    Vibrato,
};

/*!
\brief The note member one \ref ChartNoteFlag names.

The one place the axis maps onto a field. Returned as a pointer-to-member rather than read through
an accessor so the planner that WRITES the flag and the callers that READ it for the uniform-scope
decision share the single mapping — a read accessor would need a reference-returning twin, which is
the same rule stated twice and free to disagree.

\param which Flag to resolve.

\return Pointer to the \ref common::core::ChartNote member carrying that flag.
*/
[[nodiscard]] constexpr bool common::core::ChartNote::* chartNoteFlagField(
    ChartNoteFlag which) noexcept
{
    switch (which)
    {
        case ChartNoteFlag::PalmMute:
        {
            return &common::core::ChartNote::palm_mute;
        }
        case ChartNoteFlag::Dead:
        {
            return &common::core::ChartNote::dead;
        }
        case ChartNoteFlag::Tremolo:
        {
            return &common::core::ChartNote::tremolo;
        }
        case ChartNoteFlag::Vibrato:
        {
            return &common::core::ChartNote::vibrato;
        }
    }
    // Total above; a value outside the enum is a caller bug, and answering it with the palm flag
    // would be an invented answer that looks like a working verb.
    std::unreachable();
}

/*!
\brief Plans setting one of the keyed notes' boolean techniques, leaving the others alone.

One planner for every such verb because they are the same edit over different fields; the flags are
independent properties of a note, so a note may end up carrying any combination the rules allow.

Eligibility is asked of the per-note rule authority rather than restated, so a mixed selection
applies to the notes that can take the flag and silently skips the rest — and each flag inherits
its OWN rules that way, which are not the same rules. `dead` is refused wherever a technique needs
the pitch it removes (a bend, a vibrato, a pinch's squeal); `tremolo` is refused on a tap harmonic,
whose damping finger leaves the string so nothing holds the node under re-picking; `vibrato` is
refused on a dead note and on a fret-hand harmonic, which has no press to shake. An on-neck
harmonic node is refused by none of them, because on a dead note it names where the hand stands
rather than what rings, and a palm mute is refused by nothing at all. A pick slide takes neither
mute, because its saved form records neither.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param keys Notes whose mute changes, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param which Which mute the write targets.
\param value Value that mute receives.
\param label User-visible undo label.
\return The plan; NoChange when nothing changes (an ineligible note is skipped, not a refusal),
        Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planSetNoteFlag(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, ChartNoteFlag which, bool value, std::string_view label);

/*!
\brief Plans setting the keyed notes' emphasis to one value of the axis.

One planner for both emphasis verbs, and unlike the two mutes it takes a VALUE rather than a field
selector: emphasis is a single three-valued axis (\ref rock_hero::common::core::NoteEmphasis), so
the ghost and the accent are two ends of one field rather than two independent properties. Striking
a ghosted note as an accent therefore replaces the ghost instead of joining it, which is what an
axis means and why no note can ever be both.

Eligibility is asked of the per-note rule authority rather than restated, exactly as the mute and
attack verbs ask it. No rule refuses an emphasis today — dynamics compose with every attack,
mute and articulation there is, and a scrape's emphasis is its own — so the gate never fires;
it is here so the verb tracks that authority if it ever changes, rather than encoding "nothing
refuses this" as a second fact maintained by hand.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param keys Notes whose emphasis changes, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param value Emphasis every keyed note receives.
\param label User-visible undo label.
\return The plan; NoChange when nothing changes (an ineligible note is skipped, not a refusal),
        Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartNotesEditPlan, ChartPlanRefusal> planSetEmphasis(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::NoteEmphasis value,
    std::string_view label);

/*!
\brief The law one technique's toggle verb runs: what to call it, whether a note already carries it,
and the planner that writes or clears it.

The one table behind the toggle verb, so a technique joining the family adds a row here and nothing
in the controller: the verb reads the selection through `carries` to decide set-or-clear (the
uniform-scope law), plans through `plan`, and labels the entry and its reversal from `noun`.
*/
struct ChartTechniqueLaw
{
    /*! \brief The undo noun: "Palm Mute" labels the set, "Remove Palm Mute" the clear. */
    std::string_view noun;

    /*! \brief True when the note already carries the technique as the verb would write it. */
    bool (*carries)(const common::core::ChartNote& note);

    /*!
    \brief Plans setting (`set`) or clearing the technique across `keys` under `label`, with the
    per-note eligibility the planner owns.
    */
    std::expected<ChartNotesEditPlan, ChartPlanRefusal> (*plan)(
        const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
        const std::vector<ChartNoteKey>& keys, bool set, std::string_view label);
};

/*!
\brief The law for every uniformly planned technique.

Total over the seven techniques a set-or-clear plan describes. `ChartTechnique::Legato` is NOT
among them — its plan is \ref planSetLegato, which decides set-or-clear itself from what the
resolver justifies — so asking for it is a caller error, not a row.

\param technique Technique the verb is toggling; never `Legato`.

\return That technique's law.
*/
[[nodiscard]] ChartTechniqueLaw chartTechniqueLaw(ChartTechnique technique);

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
