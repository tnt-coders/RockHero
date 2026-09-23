/*!
\file chart_presentation.h
\brief The presentation rules: where each stored ring's INK stops, and where its tail rests.
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
\brief What presentation adds to the stored stream: per note, where its ink stops and where its
tail rests. It publishes NO note of its own — the stored note is the one every surface draws.

Nothing presentation decides moves a statement. A ring is drawn from its onset toward its stored
end and simply STOPS at \ref ink_end, so every keyframe keeps its stored instant and the one drawn
past the ink end is one a reveal can show exactly where the charter wrote it. Two facts about one
pass, published together because a tail-less note is not one fact: a tail rules 2 and 3 emptied was
never earned, while a tail the tail law RESTED is a ring that really sounds and whose remaining
story the furniture above it already tells.
*/
struct ChartPresentation
{
    /*!
    \brief Where each note's ink stops, as a note-relative offset in beats; index-parallel to the
    stored stream.

    THE ONE DRAWN LENGTH, and scoring's contract: what is drawn is what is judged, so a keyframe
    before the ink end is judged at its stored instant, the leg crossing it is judged as far as it
    is drawn, and nothing past it is judged. Equal to the stored ring for a FREE tail (one no later
    onset binds, or one that ends at or before the crop); the CROP for a BOUND tail — one margin
    before the binding onset, floored onto the tick lattice, never earlier than the onset; and zero
    for a tail rules 2 and 3 emptied. Always within [0, sustain].
    */
    std::vector<Fraction> ink_end;

    /*!
    \brief Where each tail RESTS, or nothing where it never does; index-parallel to \ref ink_end.

    THE TAIL LAW's verdict, the curtain UNIVERSAL: it owns everything past a note's last
    always-visible landmark, whether or not a span stands over it. A present entry is a
    note-relative offset, and its three cases are STATED HERE AND NOWHERE ELSE: zero for a plain
    ring; the last statement's offset for a ring that finishes stating and goes plain, held to the
    ink end where the statement stands past it; the note's own ink end for a handed-over member,
    whose transfer finishes at the takeover — an empty remainder, so every pixel of its ribbon is
    stated portion. A present entry therefore always lies at or inside the ink end, and the board
    draws the resting remainder — where one exists (\ref hasRestingRemainder) — only inside the
    reveal window, to the ink end. An absent entry is a tail the law rests nothing of — a ring still
    stating at its end — including every tail rules 2 and 3 emptied, which the law skips by
    construction.
    */
    std::vector<std::optional<Fraction>> rested_from;
};

/*!
\brief True where the curtain owns PART of a resting ribbon: the landmark lies strictly inside
the drawn tail.

The one reading of the verdict its two distance-scoped consumers share — the projection's
\ref NoteViewState::rested and the census's resting-ring row — so "rests, but nothing of it is
curtained" (a handed-over member, whose landmark is its own end) is decided once. The hold channel
keys on the verdict itself (\ref chartHolds), never on this.

\param rested_from The note's entry in \ref ChartPresentation::rested_from.
\param ink_end The note's entry in \ref ChartPresentation::ink_end.

\return True when a resting remainder exists past the landmark.
*/
[[nodiscard]] bool hasRestingRemainder(
    const std::optional<Fraction>& rested_from, const Fraction& ink_end);

/*!
\brief Derives where each stored ring's ink stops, and where its tail rests.

`ChartNote::sustain` stores the ACTUAL duration the string rings — Guitar Pro's notated duration
at import, what the editor's verbs author — and every surface draws that note, keyframes at their
stored instants. What presentation adds is the INK END: the instant a surface stops drawing the
ring, so no tail crowds the next head. The ink is cropped, never compressed, and nothing moves.

The rules, in order:

1. **Crop to the margin.** The *binding* onset is the first later sounding onset — a different
   grid position, on any string — that the ring does not *pass*, passing meaning running *strictly*
   past it. A ring ending exactly *on* an onset passes nothing and binds there, the common let-ring
   collision. The ink stops one minimum sustain distance (\ref minimumSustainDistanceBeats, the
   margin read at the binding onset, floored onto the tick lattice) before that onset, and never
   before the note's own onset. A ring no later onset binds, or one that ends at or before that
   crop, draws in full. Passing an onset — a tie merged across a neighbour, a cross-voice hold —
   earns the group its tails under rule 2 but does not exempt the ring from the crop. The stretch
   from the crop to the head is the ring's ENDING ZONE: keyframes standing there stay stored and
   are neither drawn nor judged, and the leg crossing the crop is drawn on its true path toward the
   zone's first keyframe and stops at the crop.
2. **Drop short effect-free tails, per onset group.** A group — every note at one grid position —
   whose members carry no sustain technique, no deliberate hold, and no *actual* ring lasting LONGER
   than the kept-sustain bound (\ref g_minimum_kept_sustain_seconds, the ring measured in seconds
   through the tempo map) draws no tail on any member: every string of a chord rings from one
   stroke, so a lone tail beside partners that look unsounded is a picture no strum makes. Any
   member earning a tail keeps every member's.
3. **A dead note draws no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). The stored ring is untouched — it is the timing the legato adjacency test
   reads — so this is a presentation rule and nothing else applies it.
4. **THE TAIL LAW — a tail that shows no technique information RESTS**: the curtain owns
   everything past a note's last always-visible landmark. A verdict-only filter, LAST: it reads the
   stored stream, judges, and MARKS where each tail rests (\ref ChartPresentation::rested_from),
   skipping any tail already empty. THE CURTAIN IS UNIVERSAL: span furniture is no part of the
   question — every fretting-hand tail in scope rests unless it is still stating at its own end,
   over open board exactly as under a bracket. WHERE THE VERDICT BINDS: resting is the 3D board's
   form — chug chains, dry arpeggios, plain sustained chords and lone plain notes rest ribbonless
   there, with the rails, boxes and hold-pinned heads stating the tenure where furniture exists,
   and each resting remainder drawing only inside the sliding reveal window at the hit line
   (\ref g_tail_reveal_lead_whole_note). The 2D lane draws the execution form always.

   SCOPE: a right-hand onset is neither a member nor a witness. THE ATOM IS THE MEMBER: a plain
   member rests, a member still stating at its end draws. NEVER RESTS — still stating at its own
   end AND NOT HANDED OVER: a ring whose final state is not plain (a bend held to the end, a shake
   that never stops, tremolo, a slide-out's travel). A HANDOVER FINISHES: a ring whose string a
   later strike takes over (\ref ChartConnections::hands_over), or whose end arrives into the next
   head, is a transfer that completes at the takeover, so it rests from its ink end with an empty
   remainder and every pixel of its ribbon stated. IT WRITES NO LENGTH: every landmark it marks
   lies at or inside the ink end.

\param connections The saved stream and the same-string relations the law reads
                   (\ref chartConnections).
\param tempo_map Tempo map supplying the meter at each note and the exact beat axis.

\return Per stored note, in the same order: the ink end and the law's verdict.
*/
[[nodiscard]] ChartPresentation chartPresentation(
    const ChartConnections& connections, const TempoMap& tempo_map);

/*!
\brief Resolves each note's HELD length: how long the player keeps the string down.

The 3D board pins a head for this length, and it is not the same question as what a tail draws. A
chugged riff under a hand-shape span draws no tails at all — no ring runs longer than the
kept-sustain bound — yet the shape is what tells the player to keep holding it, so the hold
outlives the picture. The 2D lane spends none of this.

`holds[i]` is ONE RULE: a LIVE fretting-hand member whose tail does not stand AT REST, covered by a
shape span, holds for the REST OF THE SPAN — while the grip is held, the board pins what is held.
"At rest" is the VERDICT's question (\ref ChartPresentation::rested_from), not tail emptiness.
Three populations stand outside. A DEAD member is choked rather than held. A RIGHT-HAND onset is no
part of what a grip states (\ref rightHandOnset). A HANDED-OVER member (\ref
ChartConnections::hands_over) holds for exactly its stored ring, which the same-string clamp and
the adjacency the claim required end precisely where the pull-off or hammer-on lands. A member whose
tail stands and never rests states its own hold. A COVERED RESTING member holds at least its own
stored ring, raised to the span's reach where the ring falls short; a LONE resting note holds for
the tail it draws — its ink end — because under the universal curtain every plain note rests, and a
floor on its stored ring would run its head pin into the next note's margin.

The span is the WHOLE answer, and the note's own ring does not cap it: a ring shorter than the
span's remainder can only be one the player's own re-strike cut, and a re-strike does not release
the shape (\ref deriveChartShapes bounds a span by its members' ring chains).

\param presentation The ink ends and the tail law's verdict (\ref chartPresentation).
\param connections The resolved connections (\ref chartConnections): its `saved_notes` supply the
                   stored ring a covered resting or handed-over member holds, and its `hands_over`
                   marks the members whose pin ends at that ring.
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.

\return Per-note held length in beats, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
