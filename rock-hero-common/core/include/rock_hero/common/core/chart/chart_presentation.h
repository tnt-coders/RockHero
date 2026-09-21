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
\brief What the surfaces draw: the presented stream, and where each of its tails RESTS.

Two facts about one pass, published together because a tail-less note is not one fact. A tail rules
3 and 4 emptied was never earned; a tail the tail law RESTED is a ring that really sounds and
whose remaining story the furniture above it already tells. The two consumers need opposite
answers, so the pass that knows says which is which instead of leaving each reader to guess from
an empty tail — a guess \ref chartHolds in particular cannot make correctly, since the two kinds
of emptiness call for opposite hold lengths.
*/
struct ChartPresentation
{
    /*! \brief One presented note per saved note, in the same order. */
    std::vector<ChartNote> notes;

    /*!
    \brief Where each tail RESTS, or nothing where it never does; index-parallel to \ref notes.

    THE TAIL LAW's verdict, the curtain UNIVERSAL: it owns everything past a note's last
    always-visible landmark, whether or not a span stands over it. A present entry is a
    note-relative offset, and its three cases are STATED HERE AND NOWHERE ELSE: zero for a plain
    ring; the end of its last statement for a ring that finishes stating and goes plain; the
    note's own PRESENTED end for a handed-over member, whose transfer finishes at the takeover — an
    empty remainder, so every pixel of its ribbon is stated portion. A present entry therefore
    always lies at or inside the presented tail's end, and the board draws the resting remainder —
    where one exists (\ref hasRestingRemainder) — only inside the reveal window, to the presented
    end. An absent entry is a tail the law rests nothing of — a ring still stating at its end — rule
    5 owns that one condition and its scope, including every tail rules 3 and 4 emptied, which the
    law skips by construction rather than by a test.
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
every LENGTH is theirs, and the law adds a verdict beside it without moving one.

1. **Trim to the margin.** The *binding* onset is the first later sounding onset — a different
   grid position, on any string — that the ring does not *pass*, passing meaning running *strictly
   past* it. The presented tail ends at least one minimum sustain distance (\ref
   minimumSustainDistanceBeats, a tenth of a second read at the binding onset) before that onset,
   so no tail crowds the next head. A ring ending exactly *on* an onset passes nothing and binds
   there, which is the
   common let-ring collision rather than a corner case: a notated ring ends on a musical boundary
   and the next note starts from one. A ring that no later onset binds presents whole, as a last
   note always has. **Deliberate hold**: passing an onset — a tie merged across a neighbour, a
   cross-voice hold — is a statement, and earns the group its tails under rule 3, but it does not
   exempt the ring from this trim.
2. **The tail always reaches the last keyframe.** The margin yields to the note's last statement
   and no further: the tail extends to the last keyframe's offset — one \ref g_minimum_slide_window
   past it where that statement leaves the string shaking, since a shake is an interval and a tail
   ending on its first instant would show none of it — and stops exactly there. Nothing else is
   asked: a stored note's last keyframe always says something, because the keyframe commit law
   (\ref keyframeSaysNothingNew) sheds one that does not. A RELEASED ring — a slide-out, a
   scrape's terminal — therefore never trims: the release is its last keyframe, at the ring's end,
   and the stored ring already keeps it clear of the next head on its string
   (\ref keyframeClearanceOf); a head on another string may sit inside it.
3. **Drop short effect-free tails, per onset group.** A group — every note at one grid position —
   whose members carry no sustain technique, no deliberate hold, and no *actual* ring lasting LONGER
   than the kept-sustain bound (\ref g_minimum_kept_sustain_seconds, the ring measured in seconds
   through the tempo map) presents no tail on any member: every string of a chord rings from one
   stroke, so a lone tail beside partners that look unsounded is a picture no strum makes. Any
   member earning a tail keeps every member's. The bound is a duration, so the same written value
   earns at a slow tempo and not at a fast one.
4. **A dead note presents no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). The STORED ring is untouched — it is the timing the legato adjacency test
   reads, and pinning a dead note at zero would break every claim after a muted cluck (plan ruling
   5) — so this is a presentation rule and nothing else applies it.

5. **THE TAIL LAW — a tail that shows no technique information RESTS**: the curtain owns
   everything past a note's last always-visible landmark. A verdict-only filter, LAST: it reads the
   STORED stream, judges, and MARKS where each tail rests (\ref ChartPresentation::rested_from),
   skipping any tail already empty — so rules 3 and 4 never enter the resting set, nothing here ever
   invents a length, and nothing here erases one either: the presented stream carries every member's
   rules-1-to-4 tail, which is the execution form. THE VERDICT IS AN OFFSET — the landmark the
   curtain owns everything past, whose three cases \ref ChartPresentation::rested_from states — with
   the stated portion always visible before it. THE CURTAIN IS UNIVERSAL: span furniture is no part
   of the question at all. Every fretting-hand tail in scope rests unless it is still stating at its
   own end, over open board exactly as under a bracket, so the law asks no coverage question of any
   kind — there is no span to spill past, and the curtain simply owns the ribbon from the landmark
   to its presented end. WHERE THE VERDICT BINDS: resting is the 3D board's form — chug chains, dry
   arpeggios, plain sustained chords, co-terminating let-ring figures and lone plain notes rest
   ribbonless there, with the rails, boxes and hold-pinned heads stating the tenure where furniture
   exists, and each resting remainder drawing only inside the sliding reveal window at the hit line
   (\ref g_tail_reveal_lead_whole_note). The 2D lane draws the execution form always.

   SCOPE, on BOTH sides of the judgment: a right-hand onset is neither a member nor a witness. A
   grip states nothing about the tapping hand, so a tap over a held chord neither loses its own
   ribbon nor takes its partners'.

   THE ATOM IS THE MEMBER. Each member rests on its own — a plain member rests, a member still
   stating at its end draws — and a stroke conjunction (one rest-or-draw verdict per stroke, so a
   stating partner would draw its plain stackmates whole beside it) is deliberately not the rule.
   Rule 3's per-group atom is untouched: it decides whether a group presents tails at all, before
   any of them can rest.

   NEVER RESTS — still stating at its own end AND NOT HANDED OVER: a ring whose final state is
   not plain (a bend held to the end, a shake that never stops, tremolo, a slide-out's travel).
   The curtain owns only what the ribbon has stopped saying anything with; it has no vocabulary
   for a statement still in progress. There are no exceptions beyond this disjunction, and with
   the curtain universal this disjunction IS the whole law.

   A HANDOVER FINISHES: a ring whose string a later strike takes over (\ref
   ChartConnections::hands_over) is a transfer of the sound with no vocabulary of its own either,
   but the transfer COMPLETES at the takeover, so it is the finished-statement split with an empty
   remainder — the whole drawn ribbon is the stated portion and the landmark is the ribbon's own
   end. The takeover terminates whatever the ring was still stating, which is why the handover
   outranks the never-rests disjunction: a shake or a bend into a pull-off ends where the successor
   takes the string, and the landmark is the ribbon's end either way. It therefore rests and shows
   every pixel of its ribbon — its presence is the ribbon it keeps, forced by the transfer and not
   by the technique clause. Read instead as a statement still in progress, it would refuse the
   verdict and draw its whole ring in front of the curtain that owns it.

   IT WRITES NO LENGTH: every landmark it marks is one the presented stream already carries —
   the last statement's end, which rule 2 floors the presented tail at, or the presented tail's
   own end — so the curtain never starts past the ink. Nothing
   is ever rewritten, which is why every ribbon keeps its exact original length whatever the
   verdict says.

\param connections The saved stream and the same-string relations the law reads
                   (\ref chartConnections): the rings it judges, and the handover it may not hide.
\param tempo_map Tempo map supplying the meter at each note and the exact beat axis.

\return One presented note per input note, in the same order, and the law's verdict beside it.
*/
[[nodiscard]] ChartPresentation presentedChartNotes(
    const ChartConnections& connections, const TempoMap& tempo_map);

/*!
\brief Resolves each note's HELD length: how long the player keeps the string down.

The 3D board pins a head for this length, and it is not the same question as what a tail draws. A
chugged riff under a hand-shape span presents no tails at all — no ring runs longer than the
kept-sustain bound — yet the shape is what tells the player to keep holding it, so the hold
outlives the picture. The 2D lane spends none of this: it draws, lays out, hit-tests and culls by
each note's presented tail alone, because its chord box already states the posture's length
(`docs/plans/in-progress/note-sustain-model.md`, ruling 3).

`holds[i]` is ONE RULE: a LIVE fretting-hand member whose tail does not stand AT REST, covered by a
shape span, holds for the REST OF THE SPAN — while the grip is held, the board pins what is held.
"At rest" is the VERDICT's question (\ref ChartPresentation::rested_from), not tail emptiness: a
resting member still carries its rules-1-to-4 tail, but that ribbon is the board's near-line reveal,
and its hold is still the tenure — resting and rule-3/rule-4-emptied members take the same extension
because they are one physical fact: under grip tenure a covered member's un-renewed death would have
BROKEN the grip, so coverage past a member's ring IS the record that the finger never lifted (a
re-strike replaces the sound, never the hand). There is no strum-size gate — a lone covered chug is
a grip member exactly as a strummed one is. Three populations stand outside, each for its own
reason. A DEAD member is choked rather than held — a dead chug is percussion, not a grip — and
skipping it one member at a time is also what chokes an entirely dead group, so no unanimity rule is
stated anywhere. A RIGHT-HAND onset is no part of what a grip states (\ref rightHandOnset), so the
span's reach is never its to inherit. A HANDED-OVER member (\ref ChartConnections::hands_over) holds
for exactly its stored ring, which the same-string clamp (\ref sustainBoundOf) and the adjacency the
claim itself required (\ref predecessorHoldReaches) end precisely where the pull-off or hammer-on
lands: the head reflects the SOUNDING state, and unlike a repeat chain's tail-less boxes the
destination draws its own head there to take the display over — which is why this carve-out and the
no-ring-cap rule above never collide. A member whose tail stands and never rests states its own hold
— its ribbon already says where the ring ends. A COVERED RESTING member holds at least its own
stored ring: the span extension raises it from there to the reach where the ring falls short, and a
spilling ring's hold legitimately outlives the reach — the string genuinely rings there and the
reveal shows it. That floor is keyed on COVERAGE, not on the verdict: under the universal curtain
resting says nothing about a span, so a LONE resting note — every plain note outside any furniture
is one — holds for the tail it presents.

The span extension — which members a hand-shape span holds, how far, and how overlapping spans
compose — is this function's own engine, asked of the PRESENTED stream so it extends exactly the
members whose tails no surface draws. Everything it reads besides the tail (positions, strings,
attacks, dead flags) comes through presentation untouched.

The span is the WHOLE answer, and the note's own ring does not cap it. A ring ends for two reasons
and only one of them lifts a finger: the string stopped sounding, or the string was struck again.
THE CONTINUITY LAW already bounds a span by the first — a span reaches the MINIMUM of its members'
ring chains (\ref deriveChartShapes) — so a ring shorter than the span's remainder can only be a
ring the player's own re-strike cut, and a re-strike does not release the shape. Capping at it would
drop a repeat chain's pinned heads at the second box of the chain: every member's ring in a stored
chug chain ends exactly where the next strike begins (that adjacency is what merges the chain into
one span at all), and the boxes that follow draw no heads of their own to take the display over. The
ring is what the TAIL draws; the span is what the pinned head draws. One fact each.

Nothing here can change \ref predecessorHoldReaches, which reads the stored ring directly — the
hold is a display length, not a rule input.

\param presentation The presented stream and the tail law's verdict beside it
                    (\ref presentedChartNotes), sorted by (position, string). Taken together rather
                    than apart, because a tail and the reason it is empty are one answer.
\param connections The resolved connections (\ref chartConnections): its `saved_notes` supply the
                   ACTUAL ring a covered resting or handed-over member holds, and its `hands_over`
                   marks the members whose pin ends at that ring — the handover — rather than
                   riding the grip's tenure.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
