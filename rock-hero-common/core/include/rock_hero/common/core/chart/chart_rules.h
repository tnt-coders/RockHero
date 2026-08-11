/*!
\file chart_rules.h
\brief Structural validation rules for chart documents.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

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

Frets above the low twenties do not exist on real instruments; the cap leaves headroom for
extended-range hardware without accepting junk data. Shared with import code so fret clamping
and validation agree on one authority. Harmonic nodes are bounded separately by
\ref g_max_harmonic_node, since a node is not a neck position.
*/
inline constexpr int g_max_fret{30};

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
    /*! \brief A chord template's arrays disagree with the tuning's string count. */
    InvalidTemplate,
    /*! \brief A note carries an out-of-range string, fret, or position. */
    InvalidNote,
    /*! \brief Notes are not sorted by position and string, or duplicate an onset. */
    UnsortedOrDuplicateNotes,
    /*! \brief A bend or slide payload violates its note's sustain window. */
    InvalidNotePayload,
    /*! \brief A shape span is empty, unsorted, or references a missing template. */
    InvalidShape,
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
\brief Classifies every shape span as an arpeggio or a strummed chord box.

The arrival rule shared by the highway and tab projections: a span is an arpeggio when fewer
than two notes strike at its start, when a posture string is still ringing there without being
re-struck (an earlier note's sustain crosses the span start on a template string with no onset
at it), or when a picking-hand onset — a tap or a pick slide — sounds anywhere within the span. A
strum under held content is picking around it, and a held chord under two-hand tapping is sustained
through the taps rather than fully strummed, so the shape renders as brackets around individual
notes instead of one strummed box. A posture string that is merely silent at the start (a partial
strum of the shape) does not make an arpeggio.

Answers all the shapes at once because the rule needs to look BACKWARD — to each posture string's
most recent earlier note — and one forward cursor over the sorted notes carries exactly that with
no walking back. The remaining per-shape scans stay local to each span; what this batching removed
is the unbounded backward walk, which reached the first note in the song whenever a posture string
had none and which both projections then paid for every shape on every chart revision.

\param chart Chart holding the sorted note stream, shape spans, and template table.
\param tempo_map Song tempo map, for signature-exact sustain-crossing checks.
\return One flag per shape, in `chart.shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(const Chart& chart, const TempoMap& tempo_map);

/*!
\brief The note with every technique it cannot execute stripped off.

For importers, which must not lose a whole song to one contradictory note. The editor's planners
take the opposite policy on purpose — they refuse the edit, because an author who asked for the
impossible should be told — but an imported score is not an author's request, and a bend on a note
the same score marks dead has already told us it is junk.

Lives beside the rules it satisfies so a new incompatibility cannot be written without its shed
here in view. It covers exactly the rules a single note can be made to obey by DROPPING something:
range violations are the importer's own clamping, a missing pinch node is data to supply rather
than technique to remove, and nothing relational belongs here at all — a connection claim nothing
justifies is not a technique to shed but a claim that resolves to a plain pick (\ref resolveLegato),
which is why the importer runs \ref sweepUnjustifiedLegato at completion instead. The tap landing
rule is not among these either, for the opposite reason: it reads only the note itself — an open
string with no node has nowhere to strike — so it belongs to the one-note half and is enforced in
\ref validateChartNoteAlone, which means a builder must not produce that note rather than expecting
a shed here.

Which side loses is settled by how much of the note each fact determines: the pitch identity (a
harmonic's node) outranks how the string is articulated (the mute), which outranks modulation of a
pitch over time (bend, vibrato). A dead note's own mute therefore survives against a bend, and
falls against a harmonic. Dropping the lower-ranked side is always the smaller lie: keeping the
mute over a harmonic would silence a note the score named precisely, and keeping a bend over the
mute would give a dead note a pitch to bend.

\param note Note as a source described it.

\return The note reduced to what a player can actually execute.
*/
[[nodiscard]] ChartNote executableChartNote(ChartNote note);

/*!
\brief Validates every rule a single note can break on its own.

The technique matrix splits cleanly in two: most rules read one note (which techniques may share it,
what range each field may hold, where a node may lie relative to its stop) and a few read a note's
NEIGHBOURS (a waypoint may not sit on a later onset of its string). This is the first half, and
\ref validateChartNotes calls it per note before applying the second — so a rule written here is
enforced by every consumer at once.

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
this paragraph — a summary here drifts, and this one did, describing "positive sustains" when zero
is the normal encoding for a note with no sustain (\ref ChartNote::sustain) and only a NEGATIVE
sustain is refused.

Broadly: a usable tuning; template arrays matching the string count; notes sorted by
(position, string) with no duplicate onsets, on valid grid positions, with strings and frets in
range; non-negative sustains; slide offsets strictly positive, ascending, and within the sustain;
bend offsets non-negative, ascending, and within the sustain; shape spans positive, sorted, and
referencing existing templates; sorted fret-hand positions whose window fits the neck; capo
floors; harmonic-node range, beyond-the-stop, and neck-ceiling bounds; pinch-requires-a-node;
full-mute exclusions; the tap landing rule (both tapping attacks); tap-harmonic tremolo; the
fret-hand-harmonic slide, bend, and vibrato exclusions; the cent-offset bound;
and, on pick-slide notes, no pitched techniques (a saved scrape carries none — the writer omits
the in-memory overrides; accent is a scrape's own technique) plus the required unpitched slide-out
terminal exactly at the sustain and an always-traveling path (consecutive neck positions, the
start fret included, must strictly differ — a scrape cannot sit still).

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
