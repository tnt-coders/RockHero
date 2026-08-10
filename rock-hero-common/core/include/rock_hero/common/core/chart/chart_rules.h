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
but a FRET-HAND harmonic's is capped by the neck instead: the fretting finger is standing on that
node, and a finger cannot be past the last fret. Which cap applies therefore depends on the note,
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
\brief Reports whether a shape span arrives as an arpeggio rather than a strummed chord box.

The arrival rule shared by the highway and tab projections: a span is an arpeggio when fewer
than two notes strike at its start, when a posture string is still ringing there without being
re-struck (an earlier note's sustain crosses the span start on a template string with no onset
at it), or when a tapped note sounds anywhere within the span — a strum under held content is
picking around it, and a held chord under two-hand tapping is sustained through the taps, not a
full strum, so the shape renders as brackets around individual notes rather than one strummed
box. A posture string that is merely silent at the start (a partial strum of the shape) does not
make an arpeggio.

\param chart Chart holding the sorted note stream and template table.
\param shape Shape span to classify.
\param tempo_map Song tempo map, for signature-exact sustain-crossing checks.
\return True when the span renders arpeggio-style.
*/
[[nodiscard]] bool chartShapeArrivesAsArpeggio(
    const Chart& chart, const ChartShape& shape, const TempoMap& tempo_map);

/*!
\brief The note with every technique it cannot execute stripped off.

For importers, which must not lose a whole song to one contradictory note. The editor's planners
take the opposite policy on purpose — they refuse the edit, because an author who asked for the
impossible should be told — but an imported score is not an author's request, and a bend on a note
the same score marks dead has already told us it is junk.

Lives beside the rules it satisfies so a new incompatibility cannot be written without its shed
here in view. It covers exactly the rules a single note can be made to obey by DROPPING something:
range violations are the importer's own clamping, a missing pinch node is data to supply rather
than technique to remove, and the rules that read a note's neighbours (a hammer needing somewhere
to land, a pull-off needing something to release) are `normalizeChartLegato`'s.

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
\brief The legato attack a note's same-string predecessor justifies, or `Pick` when none does.

The one authority for which direction a hammer-on/pull-off connection runs, and the only place the
question is answered. It was answered in four — the importer, the `H` verb, the repair and the gate
— and three had drifted: the importer compared Guitar Pro's ONSET frets, the verb never asked
whether the predecessor was a fret-hand harmonic and never refused a pull-off onto a harmonic. The
gate stays the checker rather than a fifth copy, because its per-clause messages say which rule a
document broke.

Judged against the RELEASED fret — where the predecessor's finger ends, so a glide hands over its
last waypoint and a scrape its slide-out's end — never against predecessor identity. Three things
disqualify a predecessor outright: none exists, it is a fret-hand harmonic (a touch holds nothing
to hand over), or it is no longer holdable at this onset. Past the kept-sustain bound a
disconnected tail is a proven release, which is why a sustain edit that breaks the connection
repairs the legato that depended on it, in the same undo entry.

Then the released fret picks the direction: above the note is a pull-off, below it a hammer-on. A
pull-off carries no harmonic (it releases onto a plain stopped pitch), and a hammer-on needs
somewhere to land (a fret, or a node to strike). Equal frets justify nothing — there is no
connection to record, and inventing one would be inventing data.

Deliberately unbounded in time: a hammer-on from a note eight bars back is musically odd, but a
predecessor still holding is a predecessor, and the author asserting legato is the authority on
whether the notes connect.

Callers add their own policy on top rather than finding it here. The `H` verb refuses to derive
across a scrape predecessor even though this accepts one, because deriving *onto* a gesture is a
guess while accepting an authored pull from one is not; and the repair never lets a `Tap` become a
pull-off, a tap being a picking-hand articulation no predecessor can convert.

\param note Note whose attack is in question.
\param predecessor Nearest earlier note on the same string, or `nullptr` when there is none.
\param predecessor_effective_sustain That predecessor's held length, span-extended
       (\ref chartEffectiveSustains) — a span implies its strum is held.
\param tempo_map Song tempo map supplying the beat axis for the hold test.

\return `Pull`, `Hammer`, or `Pick` when nothing is justified.
*/
[[nodiscard]] NoteAttack derivedLegatoAttack(
    const ChartNote& note, const ChartNote* predecessor, Fraction predecessor_effective_sustain,
    const TempoMap& tempo_map);

/*!
\brief Validates the note stream alone — every intra-note and note-relational rule.

The single authority for the technique compatibility matrix's note rules, split out so the editor
planners can gate a CANDIDATE stream through the same checks the document reader applies: a plan
whose candidate fails here refuses, which is what makes authoring an invalid chart impossible by
construction rather than by per-verb discipline.

\param notes Note stream to validate, sorted by (position, string).
\param shapes Hand-posture spans the notes play under; a span implies its strum is held, which
the legato hold test must judge against (chartEffectiveSustains).
\param tuning Tuning the notes play under; supplies the capo and string count.
\param tempo_map Song tempo map the note positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartNotes(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const ChartTuning& tuning, const TempoMap& tempo_map);

/*!
\brief Validates the chart's structural rules against the song's tempo map.

Enforces the corpus-validated rule set: a usable tuning; template arrays matching the string
count; notes sorted by (position, string) with no duplicate onsets, on valid grid positions,
with strings and frets in range; positive sustains; slide offsets strictly positive, ascending,
and within the sustain; bend offsets non-negative, ascending, and within the sustain; shape
spans positive, sorted, and referencing existing templates; sorted fret-hand positions; and, on
pick-slide notes, no pitched techniques (a saved scrape carries none — the writer omits the
in-memory overrides; accent is a scrape's own technique) plus the required unpitched slide-out
terminal exactly at the sustain and an always-traveling path (consecutive neck positions, the
start fret included, must strictly differ — a scrape cannot sit still).

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
