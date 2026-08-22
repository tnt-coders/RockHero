/*!
\file chart_presentation.h
\brief The presentation rules: what a surface draws from the actual durations a chart stores.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Reports whether a note carries a technique that lives on its sustain tail.

The one classifier rule 3 (\ref presentedChartNotes) asks before dropping a short tail: removing
the tail of one of these notes would remove the technique itself, so the drop never touches them.
Whole-note techniques are deliberately absent — muting, emphasis and harmonics say the same thing
whether or not a tail is drawn, so they earn nothing.

\param note Note to classify.

\return True when a bend, slide curve, slide-out, vibrato, or tremolo rides the tail.
*/
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note);

/*!
\brief The offset of the last payload point that CHANGES something.

The last instant the tail still has information to present, and so the furthest a margin trim may
be overridden (rule 2 of \ref presentedChartNotes).

A bend point repeating its predecessor's semitones and a waypoint repeating the previous fret (a
HOLD, not a glide) both say what the tail already said, so a trailing run of them is not a reason
to keep a tail open past the margin. The note starts unbent at its own fret, which is what the
first point of each payload is measured against. Whole-note techniques — vibrato, tremolo,
emphasis, muting, harmonics — cannot change mid-sustain and so never appear here at all.

The unpitched slide-out is deliberately absent: its end is gesture geometry that trims back with
the tail rather than pinning it, and the trim compresses it separately.

\param note Note whose payload is inspected.

\return Offset of the last changing point; zero when nothing on the tail changes anything.
*/
[[nodiscard]] Fraction lastChangingPayloadOffset(const ChartNote& note);

/*!
\brief Drops the bend and slide points a tail shortened to `target` no longer contains.

The model's "payload offsets lie within the sustain" invariant survives every shortening because
this is what every shortening owes: a point left behind hands validation a note it must refuse,
and import refuses whole SONGS rather than notes, so one such point costs the song.

The slide-out is each caller's own business — a trim PLACES it rather than dropping it — and
`target` is a parameter because a caller may clip before deciding what the sustain finally
becomes. Its wider relative \ref clipPayloadsToSustain clips against the note's own sustain,
re-terminates a scrape, and drops a slide-out that no longer fits; that one belongs to the stored
40-Q2-B truncation, this one to a presentation trim that is still choosing its end.

\param note Note whose payload is clipped in place.
\param target Offset every surviving point must lie at or before.
*/
void clipPayloadsTo(ChartNote& note, Fraction target);

/*!
\brief Bumps a payload window landing on or before the note's last waypoint past it.

Payload offsets ascend strictly, so a compressed slide-out or glide end that lands on or before
the last surviving waypoint would be an unwritable note. One minimum slide window past the
waypoint is the smallest legal answer.

\param note Note whose surviving waypoints bound the window.
\param window Window the caller wants.

\return The window itself, or the first legal offset after the last waypoint.
*/
[[nodiscard]] Fraction keptStrictlyAfterLastWaypoint(const ChartNote& note, Fraction window);

/*!
\brief Derives what the surfaces draw from what the chart stores: one presented note per saved
note.

`ChartNote::sustain` stores the ACTUAL duration the string rings — Guitar Pro's notated duration
at import, what the editor's verbs author. What a surface draws, what hit testing measures, and
what the scorer will read is this derived form. Storing the truth once and deriving the picture is
what keeps the readability policy from being import-time destruction that every later reader then
has to guess back (`docs/plans/in-progress/note-sustain-model.md`).

Nothing but the tail changes: positions, strings, frets, techniques and flags come through
untouched, and payload is CLIPPED with the tail, never rescaled — a Guitar Pro bend curve is
anchored to the notated ring, so stretching it would state a curve nobody wrote.

The rules, applied in this order, which is part of the contract because they read each other's
output — rule 3 asks whether the TRIMMED note still carries a technique, and rule 4 reads the
note as rules 1 through 3 leave it:

1. **Trim to the margin.** The next *binding* onset is the first later note at a different grid
   position, on any string. The presented tail ends at least one minimum sustain distance
   (\ref minimumSustainDistanceBeats at the note's own measure) before it, so no tail crowds the
   next head. **Deliberate hold**: a ring running *strictly past* that first binding onset is
   presented in full, however many later onsets it crosses — a tie merged across a neighbour or a
   cross-voice hold is a statement, not an overrun.
2. **Payload floors the trim.** The margin yields to information, and only as far as the
   information reaches: the tail extends to \ref lastChangingPayloadOffset and stops exactly
   there. Trailing non-changing points present nothing new, so they leave with the tail. A
   slide-out is not protected payload — its end is gesture geometry — so its presented terminal
   compresses back with the tail, floored at \ref g_minimum_slide_window and kept strictly after
   the last surviving waypoint. A scrape's terminal is the gesture's own end and compresses by the
   leg rule instead: a leg starting before the margin line ends on it, and one starting on or
   after that line halves its distance to the onset, which is the one split that always leaves a
   gap however crowded the passage. A presented scrape's terminal therefore always equals its
   presented sustain, which is the shape \ref validateChartNoteAlone pins for the stored form.
3. **Drop short effect-free tails, per onset group.** A group — every note at one grid position —
   whose members carry no sustain technique, no deliberate hold, and no *actual* ring reaching the
   kept-sustain bound (\ref minimumKeptSustainBeats at the note's own measure) presents no tail on
   any member: every string of a chord rings from one stroke, so a lone tail beside partners that
   look unsounded is a picture no strum makes. Any member earning a tail keeps every member's.
4. **A dead note presents no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). Applied through \ref trimMutedTail so the presentation rule and the editor's
   in-plan repair cannot disagree about which dead notes still ring.

\param saved_notes Note stream in SAVED form (\ref savedChartNote), sorted by (position, string).
\param tempo_map Tempo map supplying the meter at each note and the exact beat axis.

\return One presented note per input note, in the same order.
*/
[[nodiscard]] std::vector<ChartNote> presentedChartNotes(
    const std::vector<ChartNote>& saved_notes, const TempoMap& tempo_map);

/*!
\brief Resolves each note's HELD length: how long the player keeps the string down.

The 3D board pins a head for this length and the 2D lane culls ranges by it, and it is not the
same question as what the tail draws. A chugged riff under a hand-shape span presents no tails at
all — every ring is shorter than the kept-sustain bound — yet the shape is what tells the player
to keep holding it, so the hold outlives the picture.

`holds[i]` is the presented tail's end, except for a member of a 2+ onset group under a covering
shape span, not all dead, whose PRESENTED tail is empty: that member holds for its ACTUAL ring,
capped at the span's end and at the next onset on its own string. Reading the actual ring is the
whole point of the model — the hold used to be INVENTED from the span because the stored duration
had already been destroyed. An all-dead group stays choked (a dead chug is not held), as do single
notes, which hold for exactly what they present.

The span extension is not restated here: it is \ref chartEffectiveSustains — the one authority for
which members a hand-shape span holds, how far, and how overlapping spans compose — asked of the
PRESENTED stream, so it extends exactly the members presentation emptied. Everything that rule
reads besides the tail (positions, strings, dead flags) comes through presentation untouched.

The actual ring is the only cap this adds, and it is also why the same-string bound inside that
rule cannot bite here: \ref normalizeSustainOverlaps truncates every stored tail at the next onset
on its own string, so a normalized chart's actual ring already sits at or inside that bound. The
bound stays as the guard for a stream that has not been normalized yet, never as a term that
decides anything in a valid chart.

Neither cap can change \ref predecessorHoldReaches: the onset a hold is capped at IS the successor
whose claim reads it, and a hold reaching exactly that onset still reaches.

\param saved_notes Saved notes, sorted by (position, string); read for their actual rings.
\param presented_notes The same notes through \ref presentedChartNotes, in the same order and of
                       the same size; read for their presented tails.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
