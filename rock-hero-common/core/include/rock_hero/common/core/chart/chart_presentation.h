/*!
\file chart_presentation.h
\brief The presentation rules: what a surface draws from the actual durations a chart stores.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
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

\return True when an onset bend, any keyframe, a slide-out, vibrato, or tremolo rides the tail.
*/
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note);

/*!
\brief The furthest offset the tail still has information to present.

The furthest a margin trim may be overridden (rule 2 of \ref presentedChartNotes).

Every channel is read against the value the note OPENS with — its onset bend, its own fret, its
onset vibrato — and a keyframe stating nothing about a channel carries that channel's running
value forward, so a statement counts exactly when it differs from what already stood. A repeated
bend value and a repeated fret (a HOLD, not a glide) both say what the tail already said, so a
trailing run of them is not a reason to keep a tail open past the margin.

Two SHAPES of information, which is the whole reason this is not simply "the last changing
offset". A bend value and a fret are POINTS: complete at the instant they are reached, so the tail
may stop exactly there. A vibrato START is an interval STATE — a tail ending on it would show the
shake for no time at all and read as no shake — so its information reaches \ref
g_minimum_slide_window past the statement. A vibrato END is a point again, since the interval
before it already showed everything there was.

Whole-note techniques — tremolo, emphasis, muting, harmonics — cannot change mid-ring and so never
appear here at all. The unpitched slide-out is deliberately absent: it ends wherever the ring ends
rather than pinning it, and the trim compresses it separately.

\param note Note whose payload is inspected.

\return Furthest informative offset; zero when nothing on the tail says anything new.
*/
[[nodiscard]] Fraction informativePayloadEnd(const ChartNote& note);

/*!
\brief Drops the keyframes a tail shortened to `target` no longer contains.

The model's "payload offsets lie within the sustain" invariant survives every shortening because
this is what every shortening owes: a statement left behind hands validation a note it must refuse,
and import refuses whole SONGS rather than notes, so one such keyframe costs the song.

The slide-out is untouched — it ends wherever the ring ends — and `target` is a parameter because a
caller may clip before deciding what the sustain finally becomes. Its wider relative
\ref clipPayloadsToSustain clips against the note's own sustain, re-aims a scrape's terminal, and
applies the position channel's stricter bound; that one belongs to the stored 40-Q2-B truncation,
this one to a presentation trim that is still choosing its end.

\param note Note whose payload is clipped in place.
\param target Offset every surviving keyframe must lie at or before.
*/
void clipPayloadsTo(ChartNote& note, Fraction target);

/*!
\brief Bumps a window landing on or before the note's last STATED FRET past it.

A position statement follows every earlier position statement, so a compressed ring ending in a
slide-out, or a synthesized glide arrival, that lands on or before the last stated fret would be an
unwritable note. One minimum slide window past that fret is the smallest legal answer.

Bend and vibrato statements deliberately do not bind it: they are other channels, and a bend
arriving exactly where the ring ends is ordinary imported data that must not push the ring out from
under itself.

\param note Note whose stated frets bound the window.
\param window Window the caller wants.

\return The window itself, or the first legal offset after the last stated fret.
*/
[[nodiscard]] Fraction keptAfterLastStatedFret(const ChartNote& note, Fraction window);

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

1. **Trim to the margin.** The *binding* onset is the first later sounding onset — a different
   grid position, on any string — that the ring does not *pass*, passing meaning running
   *strictly past* it. The presented tail ends at least one minimum sustain distance
   (\ref minimumSustainDistanceBeats at the note's own measure) before that onset, so no tail
   crowds the next head. A ring ending exactly *on* an onset passes nothing and binds there,
   which is the common let-ring collision rather than a corner case: a notated ring ends on a
   musical boundary and the next note starts from one. A ring that no later onset binds presents
   whole, as a last note always has. **Deliberate hold**: passing an onset — a tie merged across
   a neighbour, a cross-voice hold — is still the statement it always was and still earns the
   group its tails under rule 3, but it no longer exempts the ring from this trim.
2. **Payload floors the trim.** The margin yields to information, and only as far as the
   information reaches: the tail extends to \ref informativePayloadEnd and stops exactly there.
   Trailing non-changing statements present nothing new, so they leave with the tail. A slide-out
   is not protected payload — it ends wherever the ring ends — so it compresses back with the tail,
   floored at \ref g_minimum_slide_window and kept strictly after the last stated fret. A scrape's
   terminal is the gesture's own end and compresses by the leg rule instead: a leg starting before
   the margin line ends on it, and one starting on or after that line halves its distance to the
   onset, which is the one split that always leaves a gap however crowded the passage. A presented
   scrape's terminal therefore always equals its presented sustain, which is the shape
   \ref validateChartNoteAlone pins for the stored form.
3. **Drop short effect-free tails, per onset group.** A group — every note at one grid position —
   whose members carry no sustain technique, no deliberate hold, and no *actual* ring reaching the
   kept-sustain bound (\ref minimumKeptSustainBeats at the note's own measure) presents no tail on
   any member: every string of a chord rings from one stroke, so a lone tail beside partners that
   look unsounded is a picture no strum makes. Any member earning a tail keeps every member's.
4. **A dead note presents no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). The STORED ring is untouched — it is the timing the legato adjacency test
   reads, and pinning a dead note at zero re-broke every claim after a muted cluck once already
   (plan ruling 5) — so this is a presentation rule and nothing else applies it.

\param saved_notes Note stream in SAVED form (\ref savedChartNote), sorted by (position, string).
\param tempo_map Tempo map supplying the meter at each note and the exact beat axis.

\return One presented note per input note, in the same order.
*/
[[nodiscard]] std::vector<ChartNote> presentedChartNotes(
    const std::vector<ChartNote>& saved_notes, const TempoMap& tempo_map);

/*!
\brief Resolves each note's HELD length: how long the player keeps the string down.

The 3D board pins a head for this length, and it is not the same question as what a tail draws. A
chugged riff under a hand-shape span presents no tails at all — every ring is shorter than the
kept-sustain bound — yet the shape is what tells the player to keep holding it, so the hold
outlives the picture. The 2D lane spends none of this: it draws, lays out, hit-tests and culls by
each note's presented tail alone, because its chord box already states the posture's length
(`docs/plans/in-progress/note-sustain-model.md`, ruling 3).

`holds[i]` is the presented tail's end, except for a member of a 2+ onset group under a covering
shape span, not all dead, whose PRESENTED tail is empty: that member holds for its ACTUAL ring,
capped at the span's end. Reading the actual ring is the whole point of the model — the hold used
to be INVENTED from the span because the stored duration had already been destroyed. An all-dead
group stays choked (a dead chug is not held), as do single notes, which hold for exactly what they
present.

The span extension — which members a hand-shape span holds, how far, and how overlapping spans
compose — is this function's own engine, asked of the PRESENTED stream so it extends exactly the
members presentation emptied. Everything it reads besides the tail (positions, strings, dead flags)
comes through presentation untouched.

The actual ring is the only cap this adds, and it carries 40-Q2-B with it: a derived hold running
past a later head on its own string would draw a tail through and beyond it, which no storable
chart can express — but \ref normalizeSustainOverlaps already truncates every stored tail at its
\ref sustainBoundOf, so capping at the ring caps at that bound too. Stating the bound a second time
inside the span engine could only ever agree with the first statement, so it is not stated there.

Neither cap can change \ref predecessorHoldReaches: the onset a hold is capped at IS the successor
whose claim reads it, and a hold reaching exactly that onset still reaches. (The claim reads the
stored ring directly in any case — the hold is a display length, not a rule input.)

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
