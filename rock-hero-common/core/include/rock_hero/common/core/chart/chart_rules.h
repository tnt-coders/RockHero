/*!
\file chart_rules.h
\brief The chart rules: their one normalizer, and the validator that asks its fixpoint.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Largest string count a chart tuning may declare.

Shared with display code so lane-count controls and validation agree on one authority. Capped at
eight while the tab view only defines string colors through the eighth lane; raise this once
ninth-and-beyond lane colors are chosen.
*/
inline constexpr int g_max_chart_strings{8};

/*!
\brief Highest fret a note, slide waypoint, or fret-hand position may reference.

Capped at the drawn 24-fret board (user ruling 2026-08-20): the cap used to hold headroom at 30
for extended-range hardware, but the highway lays out 24 frets and silently clamped anything
above them onto the last fret — a fret the model accepts and the board cannot show is a lie on
whichever surface loses. \ref g_highway_fret_count derives from this constant, so the two cannot
drift again; raising the cap is one edit here, and the moment for it is when a way exists to
STATE positions above the board (the open node-entry question), not before. Shared with import
code so fret clamping and validation agree on one authority. Harmonic nodes are bounded
separately by \ref g_max_harmonic_node, since a node is not a neck position.
*/
inline constexpr int g_max_fret{24};

/*!
\brief Highest harmonic node position accepted, in fret units.

**Not `g_max_fret`.** A fret must be a real position on the neck; a node is anywhere along the
vibrating string, and the nodes a *pinch* uses sit past the neck entirely, over the pickups —
bridge-side nodes climb with the harmonic (the 3rd partial's is at 19.02, the 4th's at 24.0, both
in real Guitar Pro scores), so capping at `g_max_fret` would reject every bridge-side node from
the 6th partial up. 48 is `12 * log2(16)` exactly, the 16th partial's bridge-side node.

Deliberately permissive: the bound's only job is refusing junk, and a tight one could only reject
a legitimate chart — including an import we do not author. There is no low bound beyond
"positive": higher harmonics crowd toward the nut, so nodes below fret 1 are legitimate. The
evidence behind the number (ergonomics, audibility, corpus reach — all ceiling near the 8th
partial) lives in `docs/plans/in-progress/technique-compatibility-and-hardening.md`.
*/
inline constexpr double g_max_harmonic_node{48.0};

/*!
\brief Highest capo position a chart tuning may declare.

Twelve is an octave: a capo above it leaves too little neck to play on, and no real part asks for
one. Shared with import code so capo clamping and validation agree on one authority.
*/
inline constexpr int g_max_capo{12};

/*!
\brief The lowest fret a hand can press or a pick can travel under a capo: the one above it.

The capo floor stated once. Every fret a slide gesture names (a scrape's start, a turnaround, an
exit), a pressed note, a template fret, and a hand window's index finger all sit at or above this;
the normalizer lifts to it, the validator measures against it, and the importer and the scrape
defaults author onto it — which is how a rule tightened here reaches every producer at once instead
of leaving one still flooring at zero.

\param capo The tuning's capo fret; 0 for none.

\return The first playable fret.
*/
[[nodiscard]] constexpr int firstPlayableFret(const int capo) noexcept
{
    return capo + 1;
}

/*!
\brief The highest node this note can carry, in fret units.

One authority for a bound that is not one number. Every node is capped by \ref g_max_harmonic_node,
but the neck caps it instead when \ref frettingFingerOnNode holds: the fretting finger is standing
on that node, and a finger cannot be past the last fret. Note that is narrower than a fret-hand
harmonic — a tap harmonic's node belongs to the picking hand, so the string's bound still applies to
it. Which cap applies therefore depends on the note,
which is why import cannot just compare against a constant — and why it used to hand validation
nodes it had no way to know were unreachable, failing a whole song's import over one label.

\param note Note whose node is in question.

\return The node ceiling, in fret units.
*/
[[nodiscard]] double harmonicNodeCeiling(const ChartNote& note);

/*!
\brief Highest harmonic partial a *notated* node may be snapped onto during import.

**8, taken from Guitar Pro's own output**: measured corpus labels form the unbroken run 12, 7, 5,
4, 3.2, 2.7, 2.4 — exactly partials 2 through 8 — with every remaining value an alternate node of
those same partials. Neither the GP8 manual nor its format states a cap of its own.

A cap is needed because notation writes rounded labels, not measurements, and snapping only works
while true nodes stay farther apart than the label error. The margin collapses as the cap rises:
at 16, Guitar Pro's "2.4" flips to the 15th partial (2.477) instead of the intended 8th (2.312).
Raising this needs the corpus measurement re-run, not just a bigger number; the full numbers live
in `docs/plans/in-progress/technique-compatibility-and-hardening.md`.
*/
inline constexpr int g_max_snapped_partial{8};

/*! \brief Stable chart validation failure kind. */
enum class ChartErrorCode : std::uint8_t
{
    /*! \brief The document is unreadable or an element is not the expected JSON shape. */
    MalformedDocument,
    /*! \brief Tuning strings are missing or the string count is unusable. */
    InvalidTuning,
    /*! \brief A note carries an out-of-range string, fret, or position. */
    InvalidNote,
    /*! \brief Notes are not sorted by position and string, or duplicate an onset. */
    UnsortedOrDuplicateNotes,
    /*! \brief A bend or slide payload violates its note's sustain window. */
    InvalidNotePayload,
    /*! \brief A fret-hand position entry is out of range or unsorted. */
    InvalidFretHandPosition,
    /*! \brief A pick-slide note carries other techniques or a non-traveling path. */
    InvalidPickSlide
};

/*! \brief Chart validation failure with stable code and display diagnostic. */
struct [[nodiscard]] ChartError
{
    /*! \brief Stable failure code. */
    ChartErrorCode code{};

    /*! \brief Display or log diagnostic. */
    std::string message;
};

/*!
\brief Reports whether a grid position names a real place on the tempo map's grid.

Shared with song-level validation (section markers live on the same grid), so the on-grid rule
cannot drift between chart and song documents.

\param position Grid position to test.
\param tempo_map Song tempo map defining the grid.
\return True when the position's measure, beat, and sub-beat offset are all usable.
*/
[[nodiscard]] bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map);

/*!
\brief The repair a chart normalization applied — one value per rule the normalizer owns.

The kinds exist so a load can report WHAT it changed and where, grouped by rule, and so an import
can count by rule the way it always has. The user-facing sentence for each lives in
\ref chartRepairText, the one place those words are spelled.
*/
enum class ChartRepair : std::uint8_t
{
    /*! \brief A bend or vibrato left a dead note: it sounds no pitch to modulate. */
    DeadNoteModulation,
    /*! \brief A dead note's pinch harmonic became a plain pick: a damped string cannot squeal. */
    DeadPinch,
    /*! \brief Tremolo left a tap harmonic: the damping finger leaves, so nothing holds the node. */
    TapHarmonicTremolo,
    /*! \brief A bend, vibrato, or slide left a fret-hand harmonic: a touch presses nothing. */
    FretHandHarmonicPayload,
    /*! \brief A slide left an open string: nothing is pressed to travel. */
    OpenStringSlide,
    /*! \brief A tap with nowhere to strike became a plain pick (E4). */
    StrandedStrike,
    /*! \brief A tail ringing across the next onset on its own string was truncated (40-Q2-B). */
    OverlappingTail,
    /*! \brief A fret, slide position, or hand window past the last fret clamped onto the board. */
    FretPastBoard,
    /*! \brief A slide position or hand window on or below the capo was lifted above it. */
    FretBelowCapo,
    /*! \brief A pick slide whose path no longer travels became a plain pick. */
    StilledScrape,
    /*! \brief A legato claim nothing justifies was recorded as the plain pick it plays as. */
    UnjustifiedLegato
};

/*!
\brief One repair the normalizer applied, and where.

\ref ChartRepair names the rule; `where` names the element in the words a charter can find it by —
a note's grid position and string, a hand position's grid position, a template's index. The two
travel together so a load notice can group by rule and list positions, which is what makes a
normalizing load honest rather than silent.
*/
struct ChartConversion
{
    /*! \brief The rule that fired. */
    ChartRepair repair{};

    /*! \brief The element it fired on, as display text. */
    std::string where;
};

/*!
\brief The user-facing sentence for a repair kind.

\param repair Repair kind.

\return A complete sentence naming the rule and what the repair did, with no position.
*/
[[nodiscard]] std::string_view chartRepairText(ChartRepair repair);

/*!
\brief One conversion as a log or notice line: the rule's sentence and the element it fired on.

\param conversion Conversion to render.

\return The sentence from \ref chartRepairText followed by " at <where>".
*/
[[nodiscard]] std::string chartConversionText(const ChartConversion& conversion);

/*!
\brief Flattens an attack that strikes from nowhere onto a plain pick when nothing is there to
strike (E4).

The one repair for \ref nothingToStrike, spelled once so the three places that apply it — the
normalizer, the editor's plan finalize (an edit to a note's own fret can strand its tap), and the
importer's early pass (the shape and hand-window passes read the attack, so it cannot wait) — can
never disagree about what the repaired note becomes.

\param note Note to repair in place.

\return True when the attack was flattened.
*/
[[nodiscard]] bool flattenStrandedStrike(ChartNote& note);

/*!
\brief Clips a note's payload back inside its own (possibly shortened) sustain.

The consequence every shortening of a STORED tail owes, so the payload rule "offsets lie within
the sustain" keeps holding after it. Latent payloads on a scrape clip too — they must still fit
the sustain when a toggle-back makes them real again. Its narrower relative
\ref clipPayloadsTo clips to a caller-chosen target and leaves the slide-out alone, because a
presentation trim is still deciding where its end goes; this one is for a note whose end is
already settled.

A scrape is re-terminated rather than clipped: its gesture ends exactly at the sustain, so a
sustain change moves the terminal instead of dropping it. Turnaround waypoints strictly before the
new end survive, and when compression makes the terminal fret meet its new predecessor the nearest
earlier differing fret takes over, so the path never sits still.

`end_lands_on_onset` says the new sustain end IS a following same-string onset, which the 40-Q2-B
truncation (\ref normalizeSustainOverlaps) always makes it. A pitched waypoint may not sit on a
later onset of its own string — that encoding stores no coordinates, which is what keeps it
undesyncable — so there the last point must go too; keeping it turned an ordinary note placement
into a silent refusal of the whole plan, because the truncation left behind exactly the payload
the gate then rejected. A sustain that merely ends where the user put it keeps a point at its end,
which is the normal shift-slide glide end.

\param note Note whose payload is clipped in place.
\param end_lands_on_onset True when the new sustain end is a following onset on the note's string.
*/
void clipPayloadsToSustain(ChartNote& note, bool end_lands_on_onset = false);

/*!
\brief The one bound on a note's ring: how far it may sound before its string is struck again.

`ChartNote::sustain` is the actual duration the string rings, and 40-Q2-B is the only thing that
bounds it — a re-strike stops the ring, so a tail may reach the next onset on its OWN string
exactly and never pass it (exact adjacency is what lets a slide reach its landing, and what a
legato claim reads as a hold that still reaches). Every other length a surface shows is derived
(\ref presentedChartNotes), never stored.

Stated once here because two rules need the same answer and disagreeing would be the defect:
\ref normalizeSustainOverlaps truncates to it, and the editor's duration verbs grow toward it.
Everything derived from a ring inherits the bound instead of restating it — \ref chartHolds caps a
span-implied hold at the note's own ring, which normalization already holds inside this bound.
`note` need not be a member of `notes` — only its position and string are read, so a candidate
placement asks the same question.

\param notes Note stream sorted by (position, string).
\param note Note whose ring is bounded.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return The bound in beats, or nullopt when nothing later sounds on that string.
*/
[[nodiscard]] std::optional<Fraction> sustainBoundOf(
    const std::vector<ChartNote>& notes, const ChartNote& note, const TempoMap& tempo_map);

/*!
\brief Truncates every tail ringing past its \ref sustainBoundOf (40-Q2-B); reports which.

A re-strike stops the ring, so no stored tail may cross the next onset on its string; exact
adjacency stays legal, which is what lets a slide reach its landing. The truncation clips the
payload with the tail (\ref clipPayloadsToSustain).

Stated once here rather than at each producer: \ref normalizeChart runs it on every load and
import (reporting each truncation as \ref ChartRepair::OverlappingTail), the importer runs it after
every pass that can lengthen a ring, and the editor's plan gate normalizes a candidate stream
through it before validating. The returned indices exist so the load path can name the notes it
changed; a producer that only needs the invariant ignores them.

\param notes Note stream to normalize in place, sorted by (position, string).
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Indices of the notes whose tails were truncated, ascending; empty when none were.
*/
std::vector<std::size_t> normalizeSustainOverlaps(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map);

/*!
\brief Repairs every rule one note can be made to obey on its own, in place; reports which fired.

The per-note authority of the chart normalizer (\ref normalizeChart), and the fixpoint the per-note
validator asks: a note is valid exactly when this changes nothing. That is what keeps every rule
stated ONCE — the repair policy (clamp, lift, drop, demote, trim) is written here and nowhere
else, and the validator never restates a rule as a refusal beside it.

What it owns: the board and capo ranges (a fret, waypoint, or exit past the last fret clamps
onto it; a scrape's start or any exit on or below the capo lifts above it; a waypoint on or below
the capo is dropped); the technique exclusions (a dead note's modulation, the dead pinch, the
tap harmonic's tremolo, a fret-hand harmonic's payload, an open string's slide); the stranded
strike (\ref flattenStrandedStrike); and last, a pick slide whose path no longer travels after all
of that, which becomes the plain pick it sounds like.

A dead note's tail is deliberately NOT here (E25). It is a presentation rule
(\ref presentedChartNotes rule 4): a dead note carries its actual ring like any other — that ring
is the timing information the legato adjacency test reads — and no surface draws it.

What it deliberately does NOT own stays a refusal in \ref validateChartNoteAlone, because no
repair can express it without inventing data: a string the tuning lacks, a negative fret, a
non-positive sustain, a node off the string or behind its stop, a pinch without its node, a
pressed note on a capo'd fret, a scrape without its terminal. And nothing relational belongs here:
a connection claim
nothing justifies is not a technique to shed but a claim that resolves to a plain pick
(\ref resolveLegato), which is why \ref normalizeChart ends with \ref sweepUnjustifiedLegato
instead.

Which side loses when two techniques contradict is settled by whether the loser still says
something true. The deadening wins outright: a bend or vibrato has no second reading once the
pitch is gone, so it drops, but a harmonic node SURVIVES the deadening (user ruling 2026-08-18)
because it stops being a pitch and goes on being a POSITION — exactly how a dead note's own
`fret` already reads. The pinch is the one harmonic the deadening takes with it, because its node
lies off the neck (\ref nodeIsOnNeck) and so survives as neither pitch nor hand position; attack
and node go together, since a pinch carrying no node is missing data rather than shed technique.

One pass reaches the fixpoint: every stage reads only what earlier stages have already settled,
so applying this twice changes nothing the second time.

\param note Note to normalize in place.
\param tuning Tuning the note plays under; supplies the capo.

\return The repairs that fired, in stage order; empty when the note was already normal.
*/
[[nodiscard]] std::vector<ChartRepair> normalizeChartNote(
    ChartNote& note, const ChartTuning& tuning);

/*!
\brief Fits a fret-hand window onto the playable board, in place.

The window's width shrinks to the frets above the capo when it is wider than that, its index
finger lifts above the capo, and the whole window slides down until it fits under the last fret —
in that order, so the ceiling can never push it back below the capo.

\param position Hand position to normalize.
\param tuning Tuning the hand plays under; supplies the capo.

\return The repairs that fired; empty when the window already fit.
*/
[[nodiscard]] std::vector<ChartRepair> normalizeFretHandPosition(
    FretHandPosition& position, const ChartTuning& tuning);

/*!
\brief Brings a whole chart to its normal form, in place, and reports every repair with its place.

THE one normalizer: every path that brings a chart into memory — the package reader and the
Guitar Pro importer — calls this and nothing else, so the two cannot drift, and the validator
that follows refuses only what no repair can express. It applies \ref normalizeChartNote to every
note, bounds every ring at its own string's next onset with \ref normalizeSustainOverlaps (the
one stream-level note rule, 40-Q2-B), applies \ref normalizeFretHandPosition to every hand
position, then settles the relational claims with
\ref sweepUnjustifiedLegato — last, because a truncated tail can be the hold a neighbour's claim
depended on, and the claim must be judged against the stream as it will actually stand.

A rule change therefore repairs-and-reports instead of bricking a saved project: the caller
reports the conversions (the editor opens the session dirty and shows them once; the importer
counts them), and the file is untouched until the user saves.

\param chart Chart to normalize in place.
\param tempo_map Song tempo map, for the connection hold test.

\return Every repair applied, with the element it touched; empty when the chart was already
        normal, which is what callers test to know whether memory still equals disk.
*/
[[nodiscard]] std::vector<ChartConversion> normalizeChart(Chart& chart, const TempoMap& tempo_map);

/*!
\brief Validates every rule a single note can break on its own.

The technique matrix splits cleanly in two: most rules read one note (which techniques may share it,
what range each field may hold, where a node may lie relative to its stop) and a few read a note's
NEIGHBOURS (a waypoint may not sit on a later onset of its string). This is the first half, and
\ref validateChartNotes calls it per note before applying the second — so a rule written here is
enforced by every consumer at once.

Two halves, and only the first is a list of refusals: the structural rules no repair can express
(a string the tuning lacks, a negative fret, a non-positive sustain — every string rings for some
length, and no repair can invent the one a chart failed to state — a node off the string or behind
its stop, a
pinch without its node, a pressed note on a capo'd fret, a position off the grid, a bend or slide
payload outside its sustain or out of order, a scrape without its terminal at the sustain, a saved
scrape still carrying a latent technique), and then the FIXPOINT — the note must already equal its
own normal form (\ref normalizeChartNote). Every other rule a note can break on its own is stated
once, as that normalizer's repair, and enforced here for free; nothing is restated as a refusal
beside it. Everything that reads ONE note lives here, so the editor's per-note eligibility can ask
the whole question of the note as it would be written; only ordering and the waypoint-on-a-later-
onset rule read neighbours, and those stay in \ref validateChartNotes.

Split out because an editor verb that applies to the derivable SUBSET of a selection needs exactly
this question per note: the whole-stream gate refuses an entire plan when one note is ineligible, so
before this existed the verbs hand-copied a couple of these predicates to skip such notes, and any
rule the copy did not name silently killed the edit for the whole selection instead.

\param note Note to validate.
\param tuning Tuning the note plays under; supplies the capo and the string count.
\param tempo_map Song tempo map the note's position must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartNoteAlone(
    const ChartNote& note, const ChartTuning& tuning, const TempoMap& tempo_map);

/*!
\brief Validates the note stream — every intra-note rule, plus the ordering and payload rules.

The single authority for the technique compatibility matrix's note rules, split out so the editor
planners can gate a CANDIDATE stream through the same checks the document reader applies: a plan
whose candidate fails here refuses, which is what makes authoring an invalid chart impossible by
construction rather than by per-verb discipline.

Validation is deliberately blind to what a note's NEIGHBOURS make of it, one waypoint rule aside:
the relational questions are the connection resolver's (\ref resolveLegato), which answers them as
what a claim plays as rather than as whether a file is legal. That is why no shape spans are needed
here — the hold test that wanted them belongs to the resolver.

\param notes Note stream to validate, sorted by (position, string).
\param tuning Tuning the notes play under; supplies the capo and string count.
\param tempo_map Song tempo map the note positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartNotes(
    const std::vector<ChartNote>& notes, const ChartTuning& tuning, const TempoMap& tempo_map);

/*!
\brief Validates the chart's structural rules against the song's tempo map.

The single gate every chart passes, whether it came from a package, an import, or an edit. It runs
the structural checks over the chart's own arrays and then delegates the per-note rules to
\ref validateChartNotes, so the authoritative list is the two functions' code rather than
this paragraph — a summary here drifts, and this one did once, describing "positive sustains" while
zero was still the encoding for a note with no tail.

Broadly, the structural half: a usable tuning and the cent-offset bound; notes sorted by
(position, string) with no duplicate onsets, on valid grid positions; strings in range;
non-negative frets and strictly positive sustains;
slide and bend offsets ascending within the sustain, and no waypoint on a later onset of its own
string; sorted fret-hand positions of positive width; harmonic-node range, beyond-the-stop, and
neck-ceiling bounds;
pinch-requires-a-node; on pick-slide notes, no pitched techniques (a saved scrape carries none — the
writer omits the in-memory overrides) and the required unpitched slide-out terminal exactly at the
sustain. Then the fixpoint half, stated once each as a repair of the normalizer: every note and
hand position must already equal its own normal form (\ref normalizeChartNote,
\ref normalizeFretHandPosition).

Hand-posture spans and their postures are absent by construction, not by omission: they are
derived from the notes (\ref deriveChartShapes), so there is no authored value that could be
invalid and nothing for a rule to refuse.

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
