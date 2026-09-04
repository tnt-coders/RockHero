/*!
\file chart_presentation.h
\brief The presentation rules: what a surface draws from the actual durations a chart stores.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
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
\brief What the surfaces draw: the presented stream, and which of its tails a SPAN accounts for.

Two facts about one pass, published together because a tail-less note is not one fact. A tail rules
3 and 4 emptied was never earned; a tail the tail law HID is a ring that really sounds and whose
whole story the furniture above it already tells. The two consumers need opposite answers, so the
pass that knows says which is which instead of leaving each reader to guess from an empty tail —
which is what \ref chartHolds guessed wrong three ways before this bit existed.
*/
struct ChartPresentation
{
    /*! \brief One presented note per saved note, in the same order. */
    std::vector<ChartNote> notes;

    /*! \brief True where the tail law hid a standing tail; index-parallel to \ref notes. */
    std::vector<bool> hidden;
};

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
output — rule 3 asks whether the TRIMMED note still carries a technique, rule 4 reads the note as
rules 1 through 3 leave it, and the tail law reads the stream as all four leave it. EVERY tail
decision is here, so there is one pass and no ordering contract between two of them; rules 1
through 4 see the chart's ACTUAL rings, which is what makes the law's own promise structural —
everything not hidden draws exactly as it would with no furniture in the chart.

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

5. **THE TAIL LAW — span furniture may HIDE a tail, never shorten one** (user ruling 2026-09-04,
   settled on the covered comparison in the grip-tenure migration). A drop-only filter, LAST: it
   reads the STORED stream, judges, and empties the tails it hides, skipping any tail already
   empty — so rules 3 and 4 never enter the hidden set and nothing here ever invents a length.
   ONE comparison: **a tail hides exactly when ITS OWN SPAN — the span standing at the note's
   ONSET — COVERS the whole ring** (the ring dies at or inside that span's close) **and the ring
   states nothing of its own**. Only a ring dying PAST the close is LEAVING and draws whole, the
   junction survivor included. The old law's FIGURE, its cross-span walk, and its STRING, END and
   CROSSING conjuncts are gone — CROSSING deleted by ruling ("the last note in the span shouldn't
   get treated special"), STRING and END dead as proofs under growth-in-place and renewal — so
   chug chains, dry arpeggios, plain sustained chords and co-terminating let-ring figures go
   ribbonless, and the rails, boxes and hold-pinned heads state the tenure instead.

   SCOPE, on BOTH sides of the judgment: right-hand onsets and silent holds are neither members
   nor witnesses. A grip states nothing about the tapping hand, so a tap over a held chord neither
   loses its own ribbon nor takes its partners'.

   THE ATOM IS THE STROKE, matching rule 3: the verdict is a CONJUNCTION over the stroke's
   tail-standing members, so one stroke has one tail verdict and a chord can never show a ribbon on
   the string that stopped and none on the string still sounding.

   PRESENCE — nothing of its own: a ring carrying a sustain technique (\ref hasSustainTechnique), or
   one whose string a later strike takes over (\ref ChartConnections::hands_over), is never hidden.
   The span states where the hand IS; it has no vocabulary for what the string is DOING, nor for
   a transfer of the sound. There are no exceptions beyond this disjunction.

   IT COMPUTES NOTHING. No length, no endpoint, no threshold and no constant of its own — the only
   number it reads is the margin rule 1 already keeps. That is what makes authoring a span
   REVERSIBLE: deleting it restores every ribbon at its exact original length, because nothing was
   ever rewritten.

\param connections The saved stream and the same-string relations the law reads
                   (\ref chartConnections): the rings it judges, and the handover it may not hide.
\param shapes The hand-posture spans and the postures they index (\ref deriveChartShapes) — the
              furniture the law is measured against. Empty means no furniture, and then rules 1
              through 4 are the whole answer.
\param tempo_map Tempo map supplying the meter at each note and the exact beat axis.

\return One presented note per input note, in the same order, and the law's verdict beside it.
*/
[[nodiscard]] ChartPresentation presentedChartNotes(
    const ChartConnections& connections, const ChartShapes& shapes, const TempoMap& tempo_map);

/*!
\brief Resolves each note's HELD length: how long the player keeps the string down.

The 3D board pins a head for this length, and it is not the same question as what a tail draws. A
chugged riff under a hand-shape span presents no tails at all — every ring is shorter than the
kept-sustain bound — yet the shape is what tells the player to keep holding it, so the hold
outlives the picture. The 2D lane spends none of this: it draws, lays out, hit-tests and culls by
each note's presented tail alone, because its chord box already states the posture's length
(`docs/plans/in-progress/note-sustain-model.md`, ruling 3).

`holds[i]` is ONE RULE (user sighting 2026-09-03): a LIVE fretting-hand member with no DRAWN tail,
covered by a shape span, holds for the REST OF THE SPAN — while the grip is held, the board pins
what is held. Hidden and rule-3/rule-4-emptied members take the same extension because they are
one physical fact: under grip tenure a covered member's un-renewed death would have BROKEN the
grip, so coverage past a member's ring IS the record that the finger never lifted (a re-strike
replaces the sound, never the hand). There is no strum-size gate — a lone covered chug is a grip
member exactly as a strummed one is. Two populations stand outside, each for its own reason. A
DEAD member is choked rather than held — a dead chug is percussion, not a grip — and skipping it
one member at a time is also what chokes an entirely dead group, so no unanimity rule is stated
anywhere. A RIGHT-HAND onset is no part of what a grip states (\ref rightHandOnset), so the span's
reach is never its to inherit. A member DRAWING its tail states its own hold — its ribbon already
says where the ring ends. A hidden member's stored ring survives only as the floor where no span
covers the read (\ref ChartPresentation::hidden is what keeps that floor from collapsing onto the
presented zero); it can never exceed the reach, because covered MEANS at or inside the close.

The span extension — which members a hand-shape span holds, how far, and how overlapping spans
compose — is this function's own engine, asked of the PRESENTED stream so it extends exactly the
members whose tails no surface draws. Everything it reads besides the tail (positions, strings,
attacks, dead flags) comes through presentation untouched.

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

\param presentation The presented stream and the tail law's verdict beside it
                    (\ref presentedChartNotes), sorted by (position, string). Taken together rather
                    than apart, because a tail and the reason it is empty are one answer.
\param saved_notes The stream those notes were presented from (\ref ChartConnections::saved_notes),
                   index-parallel; read for the ACTUAL ring a hidden member holds.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const std::vector<ChartNote>& saved_notes,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
