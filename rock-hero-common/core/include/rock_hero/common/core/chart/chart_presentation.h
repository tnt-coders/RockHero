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
shape span, not all dead, whose PRESENTED tail is empty: that member holds for the REST OF THE
SPAN. An all-dead group stays choked (a dead chug is not held), as do single notes, which hold for
exactly what they present.

The span extension — which members a hand-shape span holds, how far, and how overlapping spans
compose — is this function's own engine, asked of the PRESENTED stream so it extends exactly the
members presentation emptied. Everything it reads besides the tail (positions, strings, dead flags)
comes through presentation untouched.

The span is the WHOLE answer, and the note's own ring does not cap it (user ruling 2026-08-29). A
ring ends for two reasons and only one of them lifts a finger: the string stopped sounding, or the
string was struck again. THE CONTINUITY LAW already bounds a span by the first — a span reaches the
MINIMUM of its members' ring chains (\ref deriveChartShapes) — so a ring shorter than the span's
remainder can only be a ring the player's own re-strike cut, and a re-strike does not release the
shape. Capping at it made the board drop a repeat chain's pinned heads at the second box of the
chain: every member's ring in a stored chug chain ends exactly where the next strike begins (that
adjacency is what merges the chain into one span at all), and the boxes that follow draw no heads
of their own to take the display over. The ring is what the TAIL draws; the span is what the pinned
head draws. One fact each.

Nothing here can change \ref predecessorHoldReaches, which reads the stored ring directly — the
hold is a display length, not a rule input.

\param presented_notes Notes through \ref presentedChartNotes, sorted by (position, string); read
                       for their presented tails.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

/*!
\brief Resolves how much of each member's ring its covering span's INK already owns — C3.

The cover predicate, and the whole of it. LAW IV gives every displayed fact one owner, and inside a
hand-shape span the furniture is the more specific owner of a member's sustain: the span's extent is
the MINIMUM of its members' ring chains, so the mark drawn over that stretch states exactly what
each member's own ribbon would state there. The ribbon yields; the furniture keeps the ink. The
engraving analogue is a chord carrying one stem per voice rather than one per string.

SUPPRESSED, and that word is the ruling's own (user, 2026-08-30 — the W4 naming): the decided fact
here is that ink does not draw. "Covered" would lie, because a BOX-class span covers its members'
rings just as fully and suppresses nothing — the class is the whole key — and "absorbed" collides
with [D2] edge (e)'s ABSORBED LANDING, which is a different thing entirely (a landing arriving under
a standing statement). The cause family keeps its own words: \ref SpanCover and
\ref ChartShape::covers_travel are about coverage, which is what this decides suppression FROM.

**THE BRACKET SUPPRESSES; THE BOX DOES NOT** (user ruling 2026-08-29). Ownership belongs to
furniture
that STANDS where the ribbons would be, and only an arpeggio's bracket does: it is drawn across the
stretch its members arrive over, which is the stretch their tails would occupy. A chord box is drawn
at an INSTANT and states a strum, so it never stood in for a ring at all and the members' own tails
are the whole of what says how long they sound. Under a box they draw by the ordinary presented
rules and nothing else — which is where "a chord of a quarter note or longer shows its tails" comes
from: rule 3's kept-sustain earning in \ref presentedChartNotes already answers exactly that, so
there is no length threshold here and never should be.

**A span COVERING A GLIDE suppresses nothing either** ([D2] amendment 1,
\ref ChartShape::covers_travel). The span states the departing grip and covers the transit, so over
that stretch the mark and the ribbons beneath it no longer say the same thing, and the warrant above
lapses with it. Its travelling members draw their sliding tails (the technique exemption already),
and its static and OPEN members draw straight through the figure — where they were suppressed, an
open string ringing under the slide vanished beneath a mark that had stopped saying what its ribbon
says, which is the picture of a figure that string never played.

A LANDING SUCCESSOR needs no clause of its own, and that is worth saying because the ruling once
read like it might: a successor's members are rings struck under the span BEFORE it, so their
suppression is decided by that predecessor — which covers the travel that founded the successor and
therefore suppresses nothing. The continued tails draw because the figure they belong to says so,
not because a reader tested \ref ChartShape::landing_opened.

**INK ONLY.** This never trims a presented sustain and no rule reads it back: `ChartNote::sustain`
in the presented stream, `NoteViewState::end_seconds`, hit testing, and everything the future scorer
will read all go on seeing the whole ring (the [D3] rider, user-signed 2026-08-29). What consults
this is the tail-drawing site on each surface, and nothing else: the editor lane's caret peek asks
only whether the note's STORED ring covers the caret, so no reason for absent ink reaches it.

**ALL OR NOTHING PER NOTE** (user ruling 2026-08-30). `suppressed[i]` is a yes or no: the span's ink
owns note `i`'s tail only where it owns the WHOLE ring — the ring ends at or before the span's end —
and a ring that outlives the span draws WHOLE, from its own head, through the mark and out. The
rule this replaced drew the surviving stretch from the span's end, which put a ribbon on both
surfaces with no head in front of it: ink appearing at a bracket's edge, stating a note nobody
struck. A drawn tail now begins at a head by construction rather than by arithmetic. What the rule
exists for is untouched — a ring a re-strike cut ends inside the span and still hides — so what
changed is only the case where the ring outlives the mark that was standing in for it.

Three exemptions, each one the law's own, and the list is complete because THE CONSEQUENCE of a
fourth would be a mark left drawing over a ribbon that is gone. A tail carrying a slide, bend,
vibrato, tremolo or any keyframe is EXEMPT (\ref hasSustainTechnique): the tail is the canvas those
marks live on, so hiding it would hide a statement the span has no way to make. A right-hand onset
is exempt because it is a member of nothing — a tap sounding over a held shape says nothing about
the fretting hand, so the shape's furniture owns none of its ring. A silently-held stop has no tail
to own, and neither does a member presentation left tail-less: a chug hides nothing, so it reports
nothing hidden.

Coverage is positional, with no posture matching, and that is exact rather than approximate: the
growth law splits a span at any fretting-hand stop the standing shape does not state, so every
fretting-hand sounding inside a span is on a string it states, at the stop it states.

\param presented_notes Notes through \ref presentedChartNotes, sorted by (position, string).
\param shapes Hand-posture spans sorted by position.
\param arrivals Each span's CLASS through \ref chartShapeArrivals: true where it arrives as an
                arpeggio. One entry per span, in span order.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per note, true where the covering span's ink owns the whole ring and the tail draws nothing;
        false where the tail draws whole from its own head. One entry per note, in note order.
*/
[[nodiscard]] std::vector<bool> chartSuppressedTails(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const std::vector<bool>& arrivals, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
