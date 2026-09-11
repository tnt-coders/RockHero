/*!
\file chart_edits.h
\brief Chart edit planning and the concrete undo edit applied through the editor history.

Every mutation follows one shape: a pure planner builds the authored arrays the edit should
produce, normalizes same-string sustain overlaps per 40-Q2-B (the earlier note auto-truncates,
payloads clipped to the shortened sustain, all inside the same undo entry), and diffs against the
current arrays into a removed/inserted plan. Applying, undoing, and redoing are then the same
primitive run in opposite directions, so undo round-trips are exact by construction.

A plan is one change to the ONE authored per-string array, the note stream. A silently-held stop is
a note like any other (\ref common::core::NoteAttack::None), so the arpeggio hold verb — which used
to move a record between two arrays and therefore needed a plan spanning both — is now an ordinary
in-place rewrite of one note, and the exact undo round trip falls out of the same primitive every
other verb uses.
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
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/chart/chart_technique.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One planned change to the note stream: full values removed and inserted, plus a label. */
struct [[nodiscard]] ChartEditPlan
{
    /*! \brief Notes removed from the stream, full values in chart slot order. */
    std::vector<common::core::ChartNote> removed;

    /*! \brief Notes inserted into the stream, full values in chart slot order. */
    std::vector<common::core::ChartNote> inserted;

    /*! \brief User-visible undo label. */
    std::string label;

    /*!
    \brief Reports whether the plan describes no change at all.
    \return True when the stream gains and loses nothing.
    */
    [[nodiscard]] bool empty() const noexcept
    {
        return removed.empty() && inserted.empty();
    }

    /*!
    \brief The plan that walks the chart back: the halves swapped, the label kept.

    The one statement of what "backwards" means, so undo, the verb-toggle reversal and the sustain
    gesture's retirement share it instead of each swapping the halves by hand.

    \return The inverse plan.
    */
    [[nodiscard]] ChartEditPlan reversed() const
    {
        return ChartEditPlan{.removed = inserted, .inserted = removed, .label = label};
    }
};

/*!
\brief Why a planner returned no plan: a valid no-op is not a refusal.

The two emptinesses are kept apart because sharing one `std::nullopt` makes every refusal in the
editor silent — no caller can tell "this edit is not allowed" from "this edit changes nothing", so
nothing can report the former without lying about the latter. W3's pending fret entry is the
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

/*! \brief Writes one fret at every addressed stop: the typed digit. */
struct ChartFretSet
{
    /*! \brief The fret every addressed stop takes. */
    int fret{};
};

/*! \brief Moves every addressed stop by one delta: the shape-preserving shift. */
struct ChartFretShift
{
    /*! \brief Signed fret delta applied to every addressed stop. */
    int delta{};
};

/*!
\brief What a fret retype writes, as the sum of the two things it can mean.

A sum rather than a value plus a mode flag: the shift names its DELTA and nothing else, so the
caller never computes an anchor the planner would have computed again, and "a delta with an exact
flag" is not spellable.
*/
using ChartFretWrite = std::variant<ChartFretSet, ChartFretShift>;

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
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note, common::core::Fraction default_sustain);

/*!
\brief Plans the STRIKE inside a ring: the note is SPLIT at the instant, and the new head takes
the remainder.

The grammar sentence, for the tab lane's entry verbs: bare means a NOTE and Alt means the PATH, so
a bare gesture strictly inside a ring re-strikes the string there — and a re-strike does not erase
what was already ringing, it ends it. The origin keeps everything up to the instant and the new
head carries the rest on, which is what makes the strike a split rather than a truncation.

**Lossless by construction**, because it is the same per-note segment walk
\ref planDisconnectKeyframes runs: each product spans one segment of the original ring, the
keyframes inside it ride along rebased onto the new onset, and the CHANNEL states in force at the
cut become the new head's onset values — the fret it was holding, the bend it was already pushing,
the shake it was already carrying — so nothing the path said is dropped and the sound does not
change across the cut. Every one of the note's own flags rides onto both products. A cut mid-glide
therefore leaves the origin holding the STATED fret in force (never the interpolated travel value,
which would be invented data) while the remainder travels on to its arrival.

`fret` is the one thing this adds over the disconnect walk: a digit strike states the fret it
typed, while the fretless strikes (Insert, Alt+double-click) pass nothing and the walk's own
default — the stated fret in force — becomes the running fret. So no caller computes a fret the
walk would have computed again.

The offset must be strictly INSIDE the ring. A slot at the ring's exact end is not a split at all —
the ring already stops where the new onset would start — and the callers place a plain adjacent
head there through \ref planInsertNote instead (\ref ChartPathTail::at_ring_end).

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the split arithmetic and the shared finalize.
\param note Slot of the ringing note being split; the new head lands `offset` beats along it.
\param offset Beat offset of the cut from that note's onset; strictly inside its ring.
\param fret Fret the new head states, or nothing to take the stated fret in force at the cut.
\return The plan; Invalid when no note holds the slot, when the offset is not strictly inside the
        ring, or when the gate refuses the result — a glide's arrival retreating onto or before the
        statement ahead of it among them.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSplitNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& note, common::core::Fraction offset, std::optional<int> fret);

/*!
\brief The tail a caret slot rides: which note, where along its ring, and the fret a new point
there states by default.

The create gesture's whole location question, answered once so the Insert verb, the typed point and
the occupancy resolver cannot disagree about where a point would go.
*/
struct ChartPathTail
{
    /*! \brief The note whose ring covers the slot. */
    ChartSlotKey note;

    /*! \brief Beat offset from that note's onset; strictly positive, and within the ring. */
    common::core::Fraction offset;

    /*!
    \brief The fret the path last STATED at or before the offset — the note's own where nothing
    earlier states one.

    Never the interpolated value between two stating points: rounding travel is invented data, and
    a keyframe must state a fret the hand actually takes. The falls-away terminal is not a source
    either — it states where the hand releases off the ring's END, which lies after every offset a
    keyframe may occupy.
    */
    int stated_fret{};

    /*!
    \brief True when the offset is the ring's END exactly — the release instant, rather than a
    place inside the path.

    The one fact the two entry verbs read differently, stored once by the walk that already knows
    it rather than re-derived at each call site. Alt+digit states a point here — the slide-out the
    release names — while every FRETLESS gesture reads the end as a head instead: a strike there
    splits nothing, since the ring already stops where the new onset would start, and a silent
    point there would restate the running fret as a release, which says nothing at all.
    */
    bool at_ring_end{};
};

/*!
\brief Resolves the tail a slot rides, or nothing where no ring covers it.

Every ringing note has a path — its onset, whatever keyframes it states, and its terminal — so every
tail is an authoring surface for points; a plain note's path simply holds its onset fret, which is
what a new point there states by default.

At most one note can answer: the slot space holds one note per (position, string) and a ring may
reach the next onset on its own string exactly but never past it, so the covering ring is unique.
An offset of zero is not a tail at all — that is the onset, whose facts the note itself carries —
so a slot a head stands on answers nothing here.

\param notes The chart's note stream.
\param tempo_map Tempo map supplying the beat axis the offset is measured on.
\param position Slot position the caret sits at.
\param string One-based string lane the caret sits on.

\return The tail and its default fret, or nothing where no ring covers the slot.
*/
[[nodiscard]] std::optional<ChartPathTail> chartPathTailAt(
    const std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map,
    common::core::GridPosition position, int string);

/*!
\brief The fret already IN FORCE on a string at a slot: where the fretting hand was left standing.

THE SHIFT DEFAULT, and the one authority for it. A fretless head states no value, so it has to
default to something; bare, that is the open string (nothing under a finger), and under `Shift` it
is instead whatever the hand is already holding on that string — which is what makes repeated
entry at one fret a single held modifier rather than a retype per note.

The answer comes from ONE note: the last onset on that string strictly BEFORE the slot, since a
hand keeps its place until something moves it. What that note hands forward is read per the kind of
onset it is, because the fret field means a different thing under each:

- A FRETTING-HAND onset hands forward where its ring LEFT the hand — the position channel in force
  at the ring's end, the release included (\ref common::core::ringStateAt). A glide therefore hands
  forward the fret it travelled to, and a slide-out the fret it FELL TOWARD: the fall is travel the
  hand really takes, so it is where the hand ends up. (Not \ref common::core::releasedFret, which
  excludes the release because it answers the other question — what a following pull-off releases
  FROM.) A fret-hand harmonic carries no fret keyframes, so this is its own stop; a silent hold has
  no ring at all, so it is its stated fret, which is exactly the claim it exists to make.
- A RIGHT-HAND onset (\ref common::core::rightHandOnset) hands forward its HELD stop instead, since
  its own fret belongs to the other hand — a tap's landing, a scrape's travel. A scrape's stops are
  the picking hand's path along the neck, which is why it is answered here and never by the arm
  above: its release is where the pick left the string, no place a finger was left. Read as the
  RESOLVED claim (\ref common::core::chartClaimedStops), never the stored field, so a stop the
  notation states through a pull-off counts exactly as an authored one does; where it holds
  nothing, nothing is under the fretting hand and the answer is the open string.

Where nothing precedes on the string the hand has been left nowhere, so the answer is the open
string too — the bare default, which is what makes `Shift` harmless at the start of a lane.

The resolved-claim walk runs only where a right-hand onset actually answers, which keeps the whole-
chart pass off every other press.

\param notes The chart's note stream, in slot order.
\param tempo_map Tempo map supplying the beat axis the claim derivation reads.
\param position Slot position the head would take.
\param string One-based string lane the head would take.

\return The fret in force there; zero where nothing holds the string.
*/
[[nodiscard]] int fretInForceOn(
    const std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map,
    common::core::GridPosition position, int string);

/*!
\brief Plans stating one keyframe fret at an offset along a note's ring — the create gesture.

The candidate note is built with the point inserted at its sorted place and handed to the shared
finalize, so every bound this verb could restate is the rule authority's instead: an offset at or
before the onset or past the ring, a second record on one offset, a stated fret at or after a
falls-away terminal, a fret below the capo floor or past the board, a path a fret-hand harmonic or
an open string may not carry at all, a later same-string onset the fret would restate, and a scrape
a repeated position would still. None of them appears here.

Nor does the commit law: a point that says nothing the path does not already say is planted like
any other — authoring state the history never records (\ref writtenChartPlan) and the document
writer sheds (\ref common::core::keyframeSaysNothingNew) — so this planner never refuses a point
for its meaning. The scrape's still-hold — a repeated position that would stop the pick travelling —
refuses through the fixpoint, because a scrape that rests on a fret is no scrape.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param note Slot of the note the point is stated on; a slot holding no note is refused.
\param offset Beat offset from that note's onset.
\param fret Fret the point states.
\return The plan; Invalid when the gate refuses it.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planInsertKeyframe(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& note, common::core::Fraction offset, int fret);

/*!
\brief Plans the arpeggio hold verb over a scope of slots: the two-direction toggle.

The `N` verb (`docs/plans/todo/arpeggio-authoring.md`). Its scope is the ordinary one — the
selection, or the armed caret's own slot when nothing is selected — so a whole chord converts in one
press and one undo entry, and the empty-slot case the caret reaches is what authors a hold where no
note is.

The DIRECTION is the technique toggle's own law, asked of the scope as a whole: a scope whose every
occupied slot already STATES the fretting hand's stop (\ref common::core::claimedStop) releases them
all; anything else states the hold on all of them. An empty slot states nothing, so it never argues
for the releasing direction.

Per slot, then:

- **A sounding fretting-hand note** is CONVERTED: its attack becomes
  \ref common::core::NoteAttack::None, its ring goes (a silent hold has none), and every technique
  its new attack cannot state is stripped. Position, string and FRET are preserved, which is what
  makes place-then-convert the fret-stating flow: note insertion is the editor's only way to say
  "fret 5 on the A string", so the charter types the fret where the finger goes and promotes it.
- **A right-hand onset** — a tap or a scrape — gains a HELD stop at the open string instead (the
  verb's fourth case). Its onset belongs to the picking hand, so converting it would delete a sound
  the charter wrote; what the fretting hand is doing under it is exactly what \ref
  common::core::ChartNote::held records. Fret 0 and an armed caret for the same reason the
  empty-slot case uses them: typing a digit is how a stop gets stated.
- **A silent hold** is converted BACK to a plain picked note at the caller's default ring, and a
  **held stop** is simply cleared, leaving its onset untouched. The symmetric toggle, two-state like
  every other mark. The techniques a conversion stripped do not come back — the plan carries the
  whole note either way, so the verb window's reversal (and undo) restores them exactly, and
  reinventing them here would author what the charter never typed.
- **An empty slot** gains a silent hold at fret 0, with the caret armed on it, exactly as the
  neutral-create placement does: the charter then types the stop, which retypes it like any other
  selected note.

The undo entry's LABEL names what the press actually did, so the releasing direction carries three
of them: "Sound Note" where every released slot was a silent hold (the notes get their sound back),
"Release Held Stop" where every one was a held stop riding an onset (nothing gains or loses a
sound), and "Release Held Stops" for a MIXED scope — a plural rather than a fourth verb, because
both kinds ARE held-stop releases and the plural is the one word true of every slot in the press.

In the stating direction the press is REFUSED as a whole unless every slot it named still STATES a
stop once the shared finalize has settled: a claimed stop that reaches no shape states nothing and
is swept (\ref common::core::sweepInertClaimedStops). Asked of the statement rather than of the
record, because what the settle takes differs by shape — the whole note where the note IS the claim,
the field alone where a sounding onset carries it, which leaves that note identical to what it was
and therefore invisible to a diff. Whole-PLAN, never per slot — converting a whole chord states a
shape only the chord's own members make, so they are legal together and illegal one at a time, and
what decides the press is whether the shape they state is justified.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param slots The verb's scope, sorted-unique in chart slot order; an empty scope is a no-op.
\param default_sustain Ring a note converted BACK from a hold is given, clamped by the finalize;
the session's current grid step, exactly as for a placement.
\return The plan; NoChange on an empty scope, Invalid when the gate refuses the result (an off-grid
        slot, a string the tuning lacks, or a stop the capo covers) or when the press would state
        nothing.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planToggleSilentHold(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& slots, common::core::Fraction default_sustain);

/*!
\brief Withdraws the charter's held-stop statement at each slot: Delete on the held channel.

WHAT DELETE TAKES on a satellite is the STATEMENT, never the onset under it: the note keeps its
sound, and the caret stays on the stop it was on, now wearing whatever the resolution answers there
(a bare tap's DEFAULT). A planner of its own rather than the hold verb's releasing direction,
because that verb infers its direction from the CLAIM column, which a default and a fretting-hand
source's PLANT never enter: routed there, a Delete would author a held 0 on the one and convert the
other into a silent hold (THE PLANT'S FACE).

Refused whole where any named slot's stop is the NOTATION's — a tap's derived held stop, or the
plant beneath a fretting-hand source — off the one ownership table \ref planRetypeFrets reads
(\ref common::core::ChartResolutions::planted_stops): the charter typed nothing there, so there is
nothing of theirs to withdraw, and only unwriting the pull-off would. A slot carrying no held field
clears nothing, so a press over defaults alone settles as the no-op it is.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param slots The verb's scope, sorted-unique in chart slot order; an empty scope is a no-op.
\return The plan; NoChange where nothing authored was there to withdraw, Invalid where the notation
        owns a named stop or the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planClearHeldStops(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& slots);

/*!
\brief Plans deleting the selected notes and keyframes.

Funnels through the shared finalize like every plan, so the whole-matrix gate refuses a deletion
that would leave the chart invalid. A survivor whose CONNECTION the deletion broke keeps its claim
and simply plays as a pick until the next settle flattens it (\ref planSettleChart) — relational
truths are not the burst's business.

Deleting a silently-held stop needs no such care in the other direction: it is a member of no
relation, so removing one can leave nothing stale behind — only a span that stops claiming a stop
it was never sounding.

Deleting a selected KEYFRAME is the same verb one level in: it takes every statement the keyframe
makes, so the keyframe itself always goes — an emptied keyframe is no record at all
(\ref common::core::keyframeStatesNothing), and the removal rides
\ref common::core::stripKeyframeChannels, the one authority every channel-shedding rule uses. A
keyframe whose note this same call deletes needs no separate care: the note takes its whole ring
with it.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param note_keys Notes to delete, sorted ascending (the ChartSelection order — lookups
binary-search this precondition); keys with no matching note are skipped.
\param keyframe_keys Keyframes to delete, sorted ascending, same precondition; keys naming no
keyframe are skipped.
\return The plan; NoChange when no key matched, Invalid when the gate refuses the deletion.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planDeleteSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys);

/*!
\brief One press of a move gesture: the lattice its time step is taken on, and which way it moves.

A gesture records its presses rather than their sum, for the reason \ref ChartSustainStep records
its own: a time step is the placement quantum scaled by the meter of the measure the moved object
sits in, so what one press adds depends on where the run has already carried it. A press is
therefore fully described by the note value in force plus a direction. The string steps carry the
note value too and ignore it — a string lane has no lattice — which keeps one press one record.
*/
struct ChartMoveStep
{
    /*!
    \brief Note value the time step quantizes to: the placement quantum at the moment of the press.

    The note VALUE, never a precomputed beat amount, exactly as \ref ChartSustainStep stores one:
    the local meter scales the value where the step lands (one 1/4 step is one beat in x/4 and two
    in x/8), so a run that crosses a meter change steps by that meter's own amount from there on.
    */
    common::core::Fraction note_value;

    /*! \brief Which way this press moves the selection. */
    ChartStepDirection direction{};

    /*!
    \brief Compares two steps by their stored values.
    \param lhs Left-hand step.
    \param rhs Right-hand step.
    \return True when both steps store equal values.
    */
    friend constexpr bool operator==(const ChartMoveStep& lhs, const ChartMoveStep& rhs) noexcept =
        default;
};

/*! \brief What a move gesture's presses add up to: one beat delta and one string delta. */
struct ChartMoveDelta
{
    /*! \brief Signed exact beat delta along the time axis. */
    common::core::Fraction beats{};

    /*! \brief Signed string-lane delta. */
    int strings{};

    /*!
    \brief Compares two deltas by their stored values.
    \param lhs Left-hand delta.
    \param rhs Right-hand delta.
    \return True when both deltas store equal values.
    */
    friend constexpr bool operator==(
        const ChartMoveDelta& lhs, const ChartMoveDelta& rhs) noexcept = default;
};

/*!
\brief Replays a move gesture's presses into the one delta they add up to.

The reference the time steps are measured at WALKS with the replay: a press moves the selection by
the placement quantum scaled by the meter of the measure the selection has reached, so a run
crossing a meter change adds a different amount on each side of it. Sizing every press against the
measure the run STARTED in is what a summed delta would do, and it would place the whole run on the
lattice of a meter it has already left.

The reference is the front of whichever kind the selection holds, notes first: the step is uniform
over the whole selection either way, and a keyframe's meter is its note's, since the offset it steps
is measured from there.

\param tempo_map Tempo map supplying the beat axis and the meter each step lands in.
\param note_keys Notes the gesture started on, sorted ascending (the ChartSelection order).
\param keyframe_keys Keyframes the gesture started on, sorted ascending, same precondition.
\param steps The gesture's presses in press order, replayed in that order.
\return The deltas the run describes; a zero delta when it names no object or has no steps.
*/
[[nodiscard]] ChartMoveDelta chartMoveGestureDelta(
    const common::core::TempoMap& tempo_map, const std::vector<ChartSlotKey>& note_keys,
    const std::vector<ChartKeyframeKey>& keyframe_keys, const std::vector<ChartMoveStep>& steps);

/*!
\brief Plans moving the selection one step in time and/or across strings: notes by their slot,
keyframes by their offset.

ONE beat delta, applied where each kind of selected object lives. A note's place is its slot, so it
moves there; a keyframe's place is an offset along the ring it rides, so it moves there — the same
step of the same lattice, which is why one planner and one undo entry serve a mixed selection
instead of two that could half-apply. The STRING delta reaches notes only: a keyframe has no string
of its own, and the path rides the head that does.

A selected note carries its own keyframes along at UNCHANGED offsets, so a keyframe whose note the
selection also names does not step: an offset is relative to its onset, and moving both would move
it twice. Only keyframes on notes the selection left standing take the beat delta.

Refused (empty) when any moved note would leave the chart's string range or land on a slot an
unmoved note occupies — validation-preserving edits only, never clamped. Overlaps created at the
destinations truncate per 40-Q2-B: this is the ONE verb that re-strikes by truncation, so a landing
inside a tail SHORTENS that ring and rides its release back to the new end. It never DELETES a
statement, though — a landing that would clip a keyframe other than the release off the tail is
refused whole, since the statement belongs to a note the charter did not touch and the clip leaves
no record of it.

A moved keyframe's bounds are stated NOWHERE here, because the rules already carry every one of
them: an offset stepped to or below zero, past the ring, or onto — or across — a neighbour leaves
the note's offsets no longer strictly ascending inside the sustain, and a stated fret stepped onto a
later same-string onset or below the capo floor is refused just as a retyped one is
(\ref common::core::validateChartNoteAlone, \ref common::core::validateChartNotes). Crossing is
therefore a REFUSAL rather than a swap, which is the only reading a keyframe's identity allows: the
offset IS the identity, so exchanging two would leave the selection pointing at the other record.

Silently-held stops need no rule of their own here: they are notes on the same slots, so a selected
one moves like any other and the occupancy test that refuses a collision already covers them. A
hold the selection did NOT name stays where it was, and if the move takes the shape it belonged to
with it, the shared finalize's settle removes it in this same entry — the ordinary cascade, not a
case this verb has to state.

The delta is the whole GESTURE's, not one press's: a run of arrow presses is one undo entry, so the
caller replays its step list into a single delta (\ref chartMoveGestureDelta) and hands this planner
the chart state the run STARTED from. Nothing here has to know that — the plan is expressed against
the chart it is given, so a first press passes the live chart and every later one passes the
pre-gesture chart the burst's own entry reconstructs. That is why the move needs no separate `base`
parameter where the duration gesture does: a move judges nothing against the live chart, so the one
chart argument serves as both the source of the objects and the stream the plan is diffed against.

\param chart Chart the plan is expressed against: the live chart on a gesture's first press, the
state the gesture started from on every later one.
\param tempo_map Tempo map supplying the beat axis.
\param note_keys Notes to move, sorted ascending (the ChartSelection order — lookups binary-search
this precondition).
\param keyframe_keys Keyframes to step along their rings, sorted ascending, same precondition; keys
naming no keyframe are skipped.
\param beat_delta Signed exact beat delta.
\param string_delta Signed string-lane delta.
\param label User-visible undo label.
\return The plan; NoChange when nothing moves or changes, Invalid when a destination leaves the
        neck, collides, or fails the gate.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planMoveSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    common::core::Fraction beat_delta, int string_delta, std::string_view label);

/*!
\brief Plans retyping the stops a selection addresses toward a typed fret target.

Two modes: transposing (the default) shifts every stop by the same delta
so the snapshot's lowest fret lands on the target — shape-preserving, so chords reposition,
runs transpose, and a single note retypes exactly — while set-exact assigns the target to
every stop. Members can never go below zero under transposition because the lowest fret is
the anchor; a member pushed past the fret cap refuses the whole plan, never clamps.

The base is a snapshot rather than the live chart so the multi-digit entry window can replan
the whole entry from the pre-entry originals while widening; the retyped values are swapped into
the live stream for the shared finalize, whose whole-matrix gate stands in place of local fret
caps — any out-of-range or rule-violating result refuses the plan outright. It holds every note the
retype WRITES THROUGH, which is not the same set as the notes it addresses: a keyframe is stored
inside its note, so a note reached only because one of its keyframes is selected is in the snapshot
with its own stop left alone.

WHICH stops are addressed is the two key lists' answer, and they are the selection's own two
operands. A note's own stop (on `channel`) is retyped where `note_keys` names it; a keyframe's fret
is retyped where `keyframe_keys` names it. That split is the fret-verb law made structural rather
than restated: retyping a head edits exactly that head's fret — a slide's path never rides along, in
either mode, because every keyframe was placed on its fret on purpose — and retyping a keyframe
edits exactly that point, leaving the head where the charter put it. A scrape start retyped onto its
first path position refuses through the finalize gate's always-traveling rule; a pitched slide's
equal-fret start is the legal hold encoding and passes.

A KEYFRAME retypes like a head and needs no channel of its own: it has one position channel and
wears no satellite, so \ref common::core::ChartStopChannel keeps its two values and the SELECTION
KIND is what says which stop a digit reached. Transposition anchors on the lowest stop the whole
operand addresses, heads and keyframes together, which is what makes a chord slide's members move as
one delta. A keyframe stating no fret states nothing about position, so it contributes no stop and
takes none: authoring one there would state a channel the charter never pointed at, and nothing
draws such a keyframe to point at in the first place.

A selected SILENTLY-HELD stop retypes with no case of its own, which is how a bracket's own stop
is authored after the toggle stated it, and how a transposed chord carries its silent members
along: a hold is a note, its fret is a fret, and both modes reach it.

Nothing else follows a retyped hold. The span it sits in is DERIVED, so a stop that now contradicts
the note re-picking its string is not arbitrated here at all: side ruling (ii) stops recognising
that re-pick as the same hand and the span splits, which is the coherence the ruling asks for
falling out of the derivation rather than a second rule written into this planner.

The CHANNEL picks which stop of each note is addressed, and it is the same question on the anchor
and on the write, so both read one query. The channel exists on a note exactly where the satellite
that states it does, and that is now TWO populations under one rule. A bare tap's satellite carries
THE DEFAULT (\ref common::core::chartHeldStops), so the channel reaches every right-hand onset:
typing at a default AUTHORS a real held stop, where a gate on the stored field instead would let the
digit fall through and change nothing. And under THE PLANT'S FACE a fretting-hand onset a pull-off
PLANTS under wears that plant as its own satellite, so the channel reaches it too — and lands on the
refusal below, never on a held FIELD its attack forbids. The sounding channel reaches every note,
because every note has a fret. Nothing here decides WHEN the held channel applies: that is the verb
scope's answer (the caret's stop), stated once there.

A FRET-HAND HARMONIC HAS NO STOP TO RETYPE, so the sounding channel is REFUSED outright on one: the
finger stands on the node and presses nothing, and landing a digit would author a stop and a touch
naming two different places. Restating a node is press `H`, type, press `H`. Every OTHER node
travels with the stop it is measured from — a node is `stop + offset` on a logarithmic board, so a
stop that moves and a node that does not name an offset the harmonic never had — which reaches the
artificial family, a tap harmonic's own landing point, and a pinch's graze alike. Whether the moved
node is still legal is the finalize gate's answer, like every other bound here.

THE DERIVATION OWNS SOME HELD STOPS (DERIVED HELD), and the held channel is REFUSED outright where a
pull-off already states one — asked of the WIDE planted table (\ref
common::core::ChartResolutions::planted_stops), so a tap's derived stop and a fretting-hand source's
PLANT refuse alike: the charter typed at a value the notation owns, and a silent no-op would leave
the pending box saying the digit landed. A DEFAULT is owned by nobody, so it is the one thing this
refusal deliberately does not reach.

SAME-FRET SETTLE. A digit that AGREES with the derived stop is the other thing it does not reach:
asking for the value already shown is not an authoring attempt, so it settles as the no-op it is —
nothing authored, nothing refused, no undo entry — and the note simply contributes nothing to the
plan. What that changes for a SELECTION is which entries are refusal causes, not the scope of a
refusal: a disagreeing derived member still rejects the whole plan, an agreeing one drops out of it,
and every member the derivation does not own is retyped as ever. So a selection of nothing but
agreeing derived stops plans to NoChange, and a mixed one authors at its default and authored
satellites while the agreeing derived ones stand.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the shared finalize.
\param base Snapshot of every note the retype writes through, in chart slot order.
\param note_keys Notes whose OWN stop is addressed, sorted ascending (the ChartSelection order —
lookups binary-search this precondition).
\param keyframe_keys Keyframes whose fret is addressed, sorted ascending, same precondition; keys
naming no keyframe, or one stating no fret, are skipped.
\param write The fret every addressed stop takes, or the delta every one moves by.
\param channel Which stop of each named NOTE to address: its sounding fret, or its held stop.
\return The plan; NoChange when the snapshot is empty or the retype changes nothing, Invalid
        when the gate refuses the result, when the held channel names a stop the derivation owns
        and the entry disagrees with it, or when the sounding channel names a fret-hand harmonic.
        The split is what lets the pending entry paint a refused value red without painting a valid
        no-op red.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const std::vector<ChartSlotKey>& note_keys,
    const std::vector<ChartKeyframeKey>& keyframe_keys, ChartFretWrite write,
    common::core::ChartStopChannel channel);

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

    /*!
    \brief Compares two steps by their stored values.
    \param lhs Left-hand step.
    \param rhs Right-hand step.
    \return True when both steps store equal values.
    */
    friend constexpr bool operator==(
        const ChartSustainStep& lhs, const ChartSustainStep& rhs) noexcept = default;
};

/*!
\brief Plans the keyed notes' sustains by replaying a gesture's steps from the rings it started at.

The duration verb is a GESTURE, not a run of independent steps: the caller records every step in
press order, and every keyed note is recomputed by replaying the whole run over its PRE-GESTURE
ring. That is what makes the verb symmetric — each note replays the same steps from where it
started, so whatever shape the selection's tails had is preserved in both directions, a member
pinned at its own bound on the way out rejoins the others exactly where it left them on the way
back, and nothing blocks anything else: a passage of different-length tails can all be pushed as far
as each one can go. Stepping from the LIVE ring instead is what cannot do that — a clamp or a floor
would become the next step's starting value, and the selection would come back a different shape
than it went out.

A step moves a POSITION, not a length, which is why the gesture keeps the steps and not one delta:
it moves the ring's END — the note's onset plus its ring, an absolute position — to the adjacent
lattice line strictly beyond it in the step's direction, through the one keyboard step primitive
the caret and the lane nudge already share (\ref adjacentTempoGridPosition). From an on-lattice end
that is exactly one step, as a summed delta would be; from an end between lines it SNAPS, ceiling
when growing and flooring when shrinking. No snapping rule is restated here.

`base` is the stream the gesture started from. Each keyed note's pre-gesture ring is read from it,
and the returned plan is diffed against it, so the plan always describes start → now and can replace
the entry the gesture's first step pushed. On a gesture's first step `base` IS `chart.notes` and the
result is an ordinary one-step edit.

Two rules bound the replayed ring, and neither is fed back into the replay — they judge its answer,
so a clamp never becomes the next step's starting value. A step both rules absorb for EVERY keyed
note, moving no ring at all, is refused and never recorded: a lone note at its bound simply stops,
with no unseen overshoot to pay back, while a chord member pinned beside a moving one rides the
recorded steps and rejoins where it parted.

- Growth clamps at exact adjacency with the next onset on the note's own string (40-Q2-B,
  \ref common::core::sustainBoundOf), the model's one ceiling on a ring. A note pinned there
  reports the bound for every step past it, and leaves the bound on the step that falls back
  inside.
- Shrinking stops at the ring's FLOOR, exclusive: the last keyframe's offset where the note carries
  one, the onset otherwise. Every note rings, and an authored keyframe lies strictly inside its
  ring, so a replayed ring at or below the floor has nowhere legal to end: the note keeps the ring
  it CURRENTLY has — read from `chart`, not from `base`, because the value on screen is the one
  that holds — and rejoins the replay as soon as it clears the floor again. A scrape's path is
  derived, so it floors at the minimum gesture window instead, its path re-terminating onto the
  changed tail (shrink compresses the final point, growth rides it out).

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
        with one that describes nothing; Invalid when the gate refuses the result, or when the
        step moved no ring at all, so that the caller records nothing for it.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planAdjustSustain(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const std::vector<ChartSlotKey>& keys,
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
    std::optional<ChartEditPlan> plan;

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
\ref common::core::sustainBoundOf. It skips a trail-off predecessor — any note ending in a release —
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
    const std::vector<ChartSlotKey>& keys, std::string_view label);

/*!
\brief Plans the settle sweep: every legato claim the chart no longer justifies flattens.

The editor half of \ref common::core::sweepUnjustifiedLegato: the sweep decides WHAT flattens, this
turns it into an undo entry. Nothing else here is relational, which is why the sweep runs at settle
points instead of inside \ref finalizePlan — mid-burst a broken claim simply displays as the pick it
plays as, and the burst stays one undo step.

`base` is the chart state the returned plan is expressed against, which is not always the current
chart: folding the flatten into the burst's own entry needs a plan spanning the whole burst, so the
caller passes the pre-burst chart and reverses the burst before applying. A caller pushing the
flatten as its own entry passes the chart itself.

It is the whole CHART rather than its note stream because the base a fold diffs against is the
state the replaced entry was applied to, and only the chart carries it: diffing here is what keeps
that entry's own notes from being dropped when the caller walks the chart back through the burst's
reversal.

\param chart Chart being settled; its notes are swept and its shapes supply the hold test.
\param tempo_map Tempo map supplying the beat axis.
\param base Chart state the plan is diffed against.
\param label User-visible undo label.

\return The planned change, or empty when THE SWEEP found nothing to flatten — which is exactly
        when the caller must leave its coalescing windows armed. A present plan can itself be empty
        (the flatten exactly cancelled the burst it is diffed against); that is still a commit,
        because walking the chart to `base` is what removes the claim, and the caller retires the
        entry it would have replaced with nothing.
*/
[[nodiscard]] std::optional<ChartEditPlan> planSettleChart(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const common::core::Chart& base, std::string_view label);

/*!
\brief The plan as the undo history records it: both sides in their WRITTEN form, re-diffed.

THE KEYFRAME COMMIT LAW's history half. A keyframe that says nothing the path does not already say
(\ref common::core::keyframeSaysNothingNew) is authoring state, never document, and the history
holds document states: every entry is this form of the transition it describes, so no entry can
ever put a silent point back. A transition whose only content is such a point — Insert planting a
slide's start, a silent point stepped along its tail — re-diffs to EMPTY and is applied with no
entry at all; the entry that later gives the point its meaning (the landing typed) diffs from the
written state before it, and so carries the start and the landing together.

\param plan A transition between in-memory streams.
\return The same transition between their written forms; empty when they agree.
*/
[[nodiscard]] ChartEditPlan writtenChartPlan(const ChartEditPlan& plan);

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
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSetAttack(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, common::core::NoteAttack attack, std::string_view label);

/*!
\brief Which of a note's independent boolean techniques a toggle verb writes.

Not a family by meaning — a palm mute and a tremolo have nothing musical in common — but by SHAPE:
each is one bool on \ref common::core::ChartNote that a verb sets over a selection under the
uniform-scope law, with eligibility asked of the per-note rule authority. Grouping them is what
keeps that law written once instead of once per technique, and every one of them acquires a new
rule for free when the rules change (a dead note's refusal of a bend, a tap harmonic's refusal of
tremolo).

Membership is EXACTLY "one bool", which is why vibrato is not here: its field is a width axis
(\ref common::core::VibratoState) along an interval channel, so it has its own planner
(\ref planSetVibrato) and joining this family would have meant a shape the pointer-to-member
mapping below cannot even spell.
*/
enum class ChartNoteFlag : std::uint8_t
{
    /*! \brief The picking hand's palm damping the string: still pitched, but damped. */
    PalmMute,

    /*! \brief The string deadened into an unpitched click. */
    Dead,

    /*! \brief Unmeasured repeated picking: as fast as possible, no real timing. */
    Tremolo,
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
the pitch it removes (a bend, a shake, a pinch's squeal); `tremolo` is refused on a tap harmonic,
whose damping finger leaves the string so nothing holds the node under re-picking. An on-neck
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
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSetNoteFlag(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, ChartNoteFlag which, bool value, std::string_view label);

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
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSetEmphasis(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, common::core::NoteEmphasis value,
    std::string_view label);

/*!
\brief The nodes this note's own fret names AND this note can reach — the harmonic verb's operand.

THE FRET YOU TYPE IS THE NODE. A natural harmonic's finger stands where it would otherwise have
pressed, so the fret-stating flow the editor already has states the node too: type 12, press `H`.
This resolves the note's fret against the stop the string actually SPEAKS from
(\ref common::core::physicalStopFret asked of the note with its own fret zeroed — the held stop
under a right-hand onset, the capo otherwise) and hands that OFFSET to
\ref common::core::harmonicNodeCandidates. One formula covers every hand: fret 5 open names 4.98,
absolute fret 7 under a capo at 2 names 6.98 because our frets are absolute where Guitar Pro's
labels are capo-relative, and a tap holding 5 and landing on 17 names 17 — the tap harmonic, whose
node is measured from the stop it holds.

REACHABILITY IS THE RULE AUTHORITY'S ANSWER. Each candidate is dropped by asking whether the write
it would produce survives \ref common::core::validateChartNoteAlone on its saved form, so the neck
ceiling, the node-beyond-the-stop rule and the attacks whose saved form records no node at all all
bite here without one of them being restated. Today only the last of those removes anything — a
scrape's node and a silently-held stop's are stripped by the writer, so those notes offer no rows —
because the two positional bounds cannot be crossed from a label at all: a fret-hand harmonic's
stop is the capo, so its node is at most `g_max_capo + 12` against a neck of `g_max_fret`, and any
other stop is at most `g_max_fret`, so its node is at most `g_max_fret + 12` against a string of
\ref common::core::g_max_harmonic_node. Asking the authority rather than encoding "nothing refuses
this" is what makes the filter track those bounds if they ever move. A `Pinch` returns nothing at
all: its node is the picking thumb's and \ref ChartTechnique::PinchHarmonic owns it.

Empty therefore means "this press leaves the note alone", and a list of more than one means the
typed fret names two nodes — the offset of 3, and nothing else in the whole ladder — which is
exactly when the verb arms its picker.

\param note Note whose fret is read as a label.
\param tuning Tuning supplying the capo and the string count the rules judge against.
\param tempo_map Tempo map the rule authority validates positions against.
\return The reachable candidates, ascending by position; empty when the fret names none.
*/
[[nodiscard]] std::vector<common::core::HarmonicNodeCandidate> chartHarmonicNodeCandidates(
    const common::core::ChartNote& note, const common::core::ChartTuning& tuning,
    const common::core::TempoMap& tempo_map);

/*!
\brief Plans the fret-hand harmonic: the fret each keyed note already states becomes the node its
finger touches.

Per note the verb asks \ref chartHarmonicNodeCandidates for what that fret names, writes `fret = 0`
with the chosen node placed at the real stop, and runs \ref common::core::normalizeChartNote so a
payload a touch cannot carry (the bend, the shake, the travel of a finger that presses nothing) is
stripped by the ONE authority rather than by a list copied into this verb. A note whose fret names
nothing it can reach is SKIPPED, never repaired: moving the hand to the nearest node would author a
position the charter never typed.

THE CHOICE BINDS ONLY WHAT IT NAMES. `chosen_partial` picks among the candidates of a note that has
more than one — the ambiguous label, where the picker asked the charter — and a note with a single
candidate takes it whatever was chosen. Absent, every note takes the node nearest its own label,
which is what a press that stated no choice means and the same answer for every unambiguous note.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param keys Notes the touch is stated on, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param chosen_partial Partial the charter chose, or absent for the nearest node.
\param label User-visible undo label.
\return The plan; NoChange when every note skipped, Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSetHarmonic(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, std::optional<int> chosen_partial,
    std::string_view label);

/*!
\brief Plans removing a harmonic from the keyed notes: the clear BOTH harmonic rows run.

Shared rather than one clear per row because each row's noun is a harmonic, so its clear has to
remove one. Three writes: a `Pinch` becomes the plain pick it was picked as; a note touching an
on-neck node with nothing pressed presses where it was touching (`fret = 0` plus that node becomes
the node's nearest fret); and the node goes.

The press-where-you-touched arithmetic inverts \ref planSetHarmonic exactly for every label the set
can produce — 4.98 back to 5, 3.86 to 4, 3.16 to 3, 7.02 to 7, 19.02 to 19 — which is why no memory
of an overridden technique is needed: the fret comes back by arithmetic, and what the set's
normalization stripped is restored by the verb window's reversal or by undo, the argument the
arpeggio hold already makes for its own strip. The on-neck guard is what stops an open-string
pinch's bridge-side graze from being pressed as a fret it never named.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param keys Notes the harmonic leaves, sorted ascending (the ChartSelection order — lookups
binary-search this precondition).
\param label User-visible undo label.
\return The plan; NoChange when no keyed note carried one, Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planClearHarmonic(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, std::string_view label);

/*!
\brief Plans the keyframe disconnect: `Shift+L` severs a gesture at each selected keyframe.

The split-tail law applied at a keyframe instead of at a bare tail point (W10's addendum): the
note's path ENDS at the keyframe and a new head takes the remainder. The origin keeps the keyframe —
its travel really does arrive there, and dropping it would delete the leg the split was made at — so
the junction is an equal-fret handover, which is exactly the shape W10's ruling 2 names ("the
handed-over keyframe fret equalling the new head's").

**Where the arrival lands, and why it is not the split instant.** No keyframe sits on a later onset
of its own string (\ref common::core::keyframeClearanceOf): the head states those coordinates
itself, and the second copy is the desyncable encoding the format exists to make unrepresentable —
and a fret left AT the product's end would be its release by position, which the gate's clearance
repair would then shorten the ring under. A glide into a re-picked landing therefore arrives the
minimum sustain distance BEFORE it — the format's own shift-slide shape, and the importer's policy
rule 13 for exactly this figure — so the arrival retreats by that margin while the origin's RING
still runs to
the new head, because a re-strike is what stops a ring. The retreat costs nothing visible: the
presentation trim ends the drawn tail at that same margin regardless. Without it this verb could
never produce a legal chart at all, since every split would store the landing's coordinates twice.

Every selected keyframe on a note splits it, in offset order, so a chain selected at two junctions
becomes three notes: the uniform-scope law, one level inside the note.

**One split authority, two verbs.** The segment walk below is the same one \ref planSplitNote runs
for the tab lane's STRIKE, so the split is lossless by construction for both and neither restates
what a product carries. All this verb supplies is the instants — each selected keyframe — and the
`Legato` attack that says the gesture was SEVERED rather than re-struck; the fret it leaves to the
walk's default, which at a keyframe is that keyframe's own statement.

What each product carries. The remainder is the same note restarted at the junction: its fret is
the keyframe's, its ring is what is left, and the CHANNEL states in force at the split become its
onset values — the bend it was already pushing and the shake it was already carrying, so the sound
does not change across a split. Its later keyframes ride along, rebased onto the new onset, and the
release reaches only the LAST product, since it is the keyframe at the ring's end and the ring's end
is now there. The origin's own onset facts are untouched.

**The split head's attack, and the one thing this cannot yet say.** W10 ruled the split head stores
plain `Legato` — never `Pick` (which would author a strike that is not in the music) and never a
stored tie (struck-ness is derivable) — and that is what this writes. The addendum's proposed
default is that the product is an UNSTRUCK tie; expressing that needs `LegatoMotion::Continuation`,
W10's amendment to the equal-fret arm of \ref common::core::resolveLegato, which is not built. Under
today's resolver an equal-fret claim resolves to `Unjustified`, so the settle sweep flattens it to
`Pick` and the split product reads as struck until that amendment lands. The default is a
PROPOSAL, not a ruling; nothing here is written as if it were one.

Refusals, both from W10's ruling 2 ("technique verbs split only at stated frets"):

- A keyframe stating no FRET is refused. A head must sit on a stated fret, and the fret between
  stating points is interpolated travel — rounding it was killed explicitly as invented data.
- A keyframe at the ring's END is refused: there is no remainder for a new head to take, and the
  note already stops there.
- A junction with no room for the retreated arrival — one within a margin of the onset, or of the
  statement before it — refuses through the gate rather than clamping onto it, because a clamped
  arrival would be an arrival time nobody authored.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for the split arithmetic and the shared finalize.
\param keyframe_keys Keyframes to disconnect at, sorted ascending (the ChartSelection order); keys
naming no keyframe are skipped.
\param label User-visible undo label.
\return The plan; NoChange when no key named a keyframe, Invalid when a named keyframe cannot carry
        a head or when the gate refuses the result (a scrape, whose terminal the origin would lose;
        a destination slot another note holds).
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planDisconnectKeyframes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartKeyframeKey>& keyframe_keys, std::string_view label);

/*!
\brief Plans the vibrato channel's toggle across a selection — the ONE writer of that channel.

Vibrato is the only technique the toggle verb writes that is interval STATE rather than a
whole-note fact, so it is the only one with two authoring scopes: the note's own `vibrato` is the
channel's opening statement at offset zero, and each keyframe may state a change from there
(\ref common::core::Keyframe). Both are the same channel, so one planner writes both — splitting
them would be the channel stated twice, free to disagree about what a press means.

The value is a WIDTH rather than a flag (\ref common::core::VibratoState), which is what lets one
planner serve both tiers: `V` writes `Narrow` or `Off` and `Shift+V` writes `Wide` or `Off`, so a
press that replaces one tier with the other is one write of the new width and not a clear followed
by a set. Nothing here knows which key was pressed.

The caller has already decided the direction under the uniform-scope law, so this writes `set` at
every selected anchor and then applies the **dissolve law's static half**: a statement that
restates the state already in force where it stands changes neither the path function nor the
state, so it is dropped, and a keyframe the drop empties dissolves with it — through
\ref common::core::stripKeyframeChannels, the one strip authority. That single rule is what makes
every case of the user's described flow fall out without a branch: clearing the shake from a
vibrato-start point leaves the point stating nothing and it goes; stating the shake again inside a
region it already covers leaves no point behind; and stating it at a glide's arrival, where the
state genuinely changes, keeps the point that says so.

Only statements this press WROTE are judged for redundancy. A restatement the charter (or an
importer) put somewhere else says nothing to this verb, and quietly rewriting it would make an
unrelated press an editor of data the user never pointed at.

\param chart Chart being edited.
\param tempo_map Tempo map supplying the beat axis for overlap arithmetic.
\param note_keys Notes whose ONSET statement changes, sorted ascending (the ChartSelection order).
\param keyframe_keys Keyframes whose statement changes, sorted ascending, same precondition.
\param set Width written at every selected anchor.
\param label User-visible undo label.
\return The plan; NoChange when nothing changes (an ineligible note is skipped, not a refusal, and
        a redundant statement is a no-op), Invalid when the gate refuses the result.
*/
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planSetVibrato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    common::core::VibratoState set, std::string_view label);

/*!
\brief The law one technique's toggle verb runs: what to call it, whether the selection already
carries it, and the planner that writes or clears it.

The one table behind the toggle verb, so a technique joining the family adds a row here and nothing
in the controller: the verb reads `carried` to decide set-or-clear (the uniform-scope law), plans
through `plan`, and labels the entry and its reversal from `noun`.

Both members take the whole SELECTION rather than one note, because the selection is what the
uniform-scope law scopes a verb to and not every technique lives in one place: vibrato is a channel
along the ring, so a selected keyframe carries it and takes it exactly as a selected note does,
while every row but its two reads `selection.notes()` and nothing else. Handing each row one
operand and letting it read the parts it has a meaning for is what keeps a technique with no
keyframe scope from carrying a guard about keyframes.
*/
struct ChartTechniqueLaw
{
    /*! \brief The undo noun: "Palm Mute" labels the set, "Remove Palm Mute" the clear. */
    std::string_view noun;

    /*!
    \brief True when every object the verb would write already carries the technique.

    False for a selection this verb has no operand in at all, which makes such a press mean SET and
    therefore plan to nothing — the same inert outcome an empty selection has always had.
    */
    bool (*carried)(const common::core::Chart& chart, const ChartSelection& selection);

    /*!
    \brief Plans setting (`set`) or clearing the technique across the selection under `label`, with
    the per-note eligibility the planner owns.
    */
    std::expected<ChartEditPlan, ChartPlanRefusal> (*plan)(
        const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
        const ChartSelection& selection, bool set, std::string_view label);
};

/*!
\brief The law for every uniformly planned technique.

Total over the thirteen techniques a set-or-clear plan describes. `ChartTechnique::Legato` is NOT
among them — its plan is \ref planSetLegato, which decides set-or-clear itself from what the
resolver justifies — so asking for it is a caller error, not a row.

\param technique Technique the verb is toggling; never `Legato`.

\return That technique's law.
*/
[[nodiscard]] ChartTechniqueLaw chartTechniqueLaw(ChartTechnique technique);

/*!
\brief Applies a whole planned change atomically to a chart, across every authored array.

Verifies every removed record still matches by full value and every inserted slot is free, then
swaps in the new arrays; a failed precondition anywhere leaves the chart entirely untouched, which
is what makes a plan crossing the two arrays one atomic gesture rather than two that could half
apply.

Applying the plan's own \ref ChartEditPlan::reversed walks the chart back, so undo, redo and the
verb-toggle reversal are this one primitive run in opposite directions.

\param chart Chart to mutate.
\param plan Planned change to apply forwards.
\return Empty success, or PreflightRejected when the chart no longer matches the plan.
*/
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyChartChange(
    common::core::Chart& chart, const ChartEditPlan& plan);

/*! \brief Inverse-command edit replaying a planned chart change in either direction. */
struct [[nodiscard]] ChartEdit final : IEdit
{
    /*!
    \brief Captures a planned chart change.
    \param plan_value The applied plan whose directions this edit replays.
    */
    explicit ChartEdit(ChartEditPlan plan_value)
        : plan(std::move(plan_value))
    {}

    /*!
    \brief Removes the inserted records and restores the removed ones.
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
    ChartEditPlan plan;
};

} // namespace rock_hero::editor::core
