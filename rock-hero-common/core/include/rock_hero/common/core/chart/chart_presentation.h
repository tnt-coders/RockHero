/*!
\file chart_presentation.h
\brief The presentation rules: what a surface draws from the actual durations a chart stores.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

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
\brief What the surfaces draw: the presented stream, and where each of its tails RESTS.

Two facts about one pass, published together because a tail-less note is not one fact. A tail rules
3 and 4 emptied was never earned; a tail the tail law RESTED is a ring that really sounds and
whose remaining story the furniture above it already tells. The two consumers need opposite
answers, so the pass that knows says which is which instead of leaving each reader to guess from
an empty tail — which is what \ref chartHolds guessed wrong three ways before this verdict
existed.
*/
struct ChartPresentation
{
    /*! \brief One presented note per saved note, in the same order. */
    std::vector<ChartNote> notes;

    /*!
    \brief Where each tail RESTS, or nothing where it never does; index-parallel to \ref notes.

    THE TAIL LAW's verdict in its generalized form (user ruling 2026-09-06; the coverage half
    generalized 2026-09-07): the curtain owns everything past a note's last always-visible
    landmark. A present entry is a note-relative offset — THE LATER OF TWO LANDMARKS, and both are
    STATED HERE AND NOWHERE ELSE. The statement landmark: zero for a plain ring; the end of the
    informative payload for a ring that finishes stating and goes plain; the note's own PRESENTED
    end for a handed-over member, whose transfer finishes at the takeover — an empty remainder, so
    every pixel of its ribbon is stated portion. The coverage landmark: where the ribbon FIRST RUNS
    UNDER A SPAN — zero for a ring struck under one, the front of the first span it rings into for
    a ring struck on open board (\ref SpanCover::firstCovered). A present entry therefore always
    lies at or inside the presented tail's end, and the board draws the resting remainder — where
    one exists (\ref hasRestingRemainder) — only inside the reveal window, to the presented end
    even where that end outlives the span. An absent entry is a tail the law rests nothing of — a
    ring still stating at its end, or one no span ever stands over — rule 5 owns every condition
    and its scope — including every tail rules 3 and 4 emptied, which the law skips by
    construction rather than by a test.
    */
    std::vector<std::optional<Fraction>> rested_from;
};

/*!
\brief True where the curtain owns PART of a resting ribbon: the landmark lies strictly inside
the presented tail.

The one reading of the verdict its two distance-scoped consumers share — the projection's
\ref NoteViewState::rested and the census's resting-ring row — so "rests, but nothing of it is
curtained" (a handed-over member, whose landmark is its own end) is decided once. The hold channel
keys on the verdict itself (\ref chartHolds), never on this.

\param rested_from The note's entry in \ref ChartPresentation::rested_from.
\param presented The note as presented, whose tail end the landmark is measured against.

\return True when a resting remainder exists past the landmark.
*/
[[nodiscard]] bool hasRestingRemainder(
    const std::optional<Fraction>& rested_from, const ChartNote& presented);

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
everything not resting draws exactly as it would with no furniture in the chart.

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

5. **THE TAIL LAW — span furniture may REST a tail, never shorten one** (user ruling 2026-09-04;
   generalized 2026-09-06: the curtain owns everything past a note's last always-visible
   landmark). A verdict-only filter, LAST: it reads the STORED stream, judges, and MARKS where
   each tail rests (\ref ChartPresentation::rested_from), skipping any tail already empty — so
   rules 3 and 4 never enter the resting set, nothing here ever invents a length, and since the
   execution-form amendment (user ruling 2026-09-03) nothing here erases one either: the
   presented stream carries every member's rules-1-to-4 tail. THE VERDICT IS AN OFFSET — the
   landmark the curtain owns everything past, whose three cases
   \ref ChartPresentation::rested_from states — with the stated portion always visible before
   it. THE CURTAIN BELONGS TO THE SPAN (user ruling 2026-09-07, generalizing the own-span law):
   the coverage question is WHERE THE RIBBON FIRST RUNS UNDER A SPAN, not only whether one stands
   at the onset — a ring struck under a span enters it at its head, exactly the verdict it had
   before; a ring struck on open board that rings into a later bracket rests from that bracket's
   front and draws at full before it (\ref SpanCover::firstCovered). COVERAGE IS MEMBERSHIP, not
   containment (the 2026-09-06 spill amendment): past the landmark a ring outliving its span —
   into open board or into the next span alike — rests to its presented end and the reveal shows
   it there; LEAVING is no longer an out, so the junction survivor rests too. WHERE THE VERDICT
   BINDS: resting is the 3D board's form — chug chains, dry arpeggios, plain sustained chords and
   co-terminating let-ring figures rest ribbonless there, with the rails, boxes and hold-pinned
   heads stating the tenure, and each resting remainder drawing only inside the sliding reveal
   window at the hit line (\ref g_tail_reveal_lead_whole_note). The 2D lane draws the execution
   form always.

   SCOPE, on BOTH sides of the judgment: right-hand onsets and silent holds are neither members
   nor witnesses. A grip states nothing about the tapping hand, so a tap over a held chord neither
   loses its own ribbon nor takes its partners'.

   THE ATOM IS THE MEMBER (user ruling 2026-09-07: "the curtain should apply to everything in the
   span that doesn't carry technique info"). Each member rests on its own — a plain member rests,
   a member still stating at its end draws, a member whose ring ends before the span's front is
   never reached — and the stroke conjunction the law shipped with (one rest-or-draw verdict per
   stroke, so a stating partner drew its plain stackmates whole beside it) is deleted. Rule 3's
   per-group atom is untouched: it decides whether a group presents tails at all, before any of
   them can rest.

   NEVER RESTS — still stating at its own end AND NOT HANDED OVER: a ring whose final state is
   not plain (a bend held to the end, a shake that never stops, tremolo, a slide-out's travel).
   The span states where the hand IS; it has no vocabulary for a statement still in progress.
   There are no exceptions beyond this disjunction.

   A HANDOVER FINISHES: a ring whose string a later strike takes over
   (\ref ChartConnections::hands_over) is a transfer of the sound the span has no vocabulary for
   either, but the transfer COMPLETES at the takeover, so it is the finished-statement split with
   an empty remainder — the whole drawn ribbon is the stated portion and the landmark is the
   ribbon's own end. The takeover terminates whatever the ring was still stating, which is why
   the handover outranks the never-rests disjunction: a shake or a bend into a pull-off ends
   where the successor takes the string, and the landmark is the ribbon's end either way. It
   therefore rests and shows every pixel of its ribbon — its presence is the ribbon it keeps,
   forced by the transfer and not by the technique clause; read as a statement in progress it
   refused the verdict, and the stroke conjunction of the day then drew a co-struck partner's
   whole ring in front of the curtain that owned it (the co-struck source sighting, 2026-09-06 —
   the conjunction is gone since 2026-09-07, but the handover's own landmark is unchanged).

   IT WRITES NO LENGTH: every landmark it marks is one the presented stream already carries —
   the informative payload's end (\ref informativePayloadEnd), which rule 2 floors the presented
   tail at, or the presented tail's own end — so the curtain never starts past the ink, and
   authoring a span stays REVERSIBLE: deleting it restores every ribbon at its exact original
   length, because nothing was ever rewritten.

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

`holds[i]` is ONE RULE (user sighting 2026-09-03): a LIVE fretting-hand member whose tail does
not stand AT REST, covered by a shape span, holds for the REST OF THE SPAN — while the grip is
held, the board pins what is held. "At rest" is the VERDICT's question
(\ref ChartPresentation::rested_from), not tail emptiness: since the execution-form amendment a
resting member carries its rules-1-to-4 tail again, but that ribbon is the board's near-line
reveal, and its hold is still the tenure — resting and
rule-3/rule-4-emptied members take the same extension because they are one physical fact: under
grip tenure a covered member's un-renewed death would have BROKEN the grip, so coverage past a
member's ring IS the record that the finger never lifted (a re-strike replaces the sound, never
the hand). There is no strum-size gate — a lone covered chug is a grip member exactly as a
strummed one is. Three populations stand outside, each for its own reason. A DEAD member is choked
rather than held — a dead chug is percussion, not a grip — and skipping it one member at a time
is also what chokes an entirely dead group, so no unanimity rule is stated anywhere. A RIGHT-HAND
onset is no part of what a grip states (\ref rightHandOnset), so the span's reach is never its to
inherit. A HANDED-OVER member (\ref ChartConnections::hands_over) holds for exactly its stored
ring, which the same-string clamp (\ref sustainBoundOf) and the adjacency the claim itself
required (\ref predecessorHoldReaches) end precisely where the pull-off or hammer-on lands: the
head reflects the SOUNDING state (user law 2026-09-06), and unlike a repeat chain's tail-less
boxes the destination draws its own head there to take the display over — which is why this
carve-out and the no-ring-cap ruling above never collide. A member whose tail stands and never
rests states its own hold — its ribbon
already says where the ring ends. A RESTING member's stored ring is the floor its hold starts
from; the span extension raises it only where the ring falls short, so since the spill amendment
a spilling ring's hold legitimately outlives the reach — the string genuinely rings there and
the reveal shows it.

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
\param connections The resolved connections (\ref chartConnections): its `saved_notes` supply the
                   ACTUAL ring a resting or handed-over member holds, and its `hands_over` marks
                   the members whose pin ends at that ring — the handover — rather than riding the
                   grip's tenure.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
