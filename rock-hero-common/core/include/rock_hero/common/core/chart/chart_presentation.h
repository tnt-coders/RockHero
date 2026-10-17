/*!
\file chart_presentation.h
\brief The presentation rules: where each stored ring's INK stops, and where its tail rests.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/head_margins.h>
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
pass, published together because a tail-less note is not one fact: a tail emptied by tail rules 2
and 3 was never earned, while a tail the tail law RESTED is a ring that really sounds and whose
remaining story the furniture above it already tells.
*/
struct ChartPresentation
{
    /*!
    \brief Where each note's ink stops, as a note-relative offset in whole notes; index-parallel to
    the stored stream.

    THE ONE DRAWN LENGTH, and scoring's contract: what is drawn is what is judged, so a keyframe
    before the ink end is judged at its stored instant, the leg crossing it is judged as far as it
    is drawn, and nothing past it is judged. Equal to the stored ring for a FREE tail (one no later
    onset binds, or one that ends at or before the crop); the CROP for a BOUND tail — one margin
    before the binding onset, floored onto the tick lattice, never earlier than the onset; and zero
    for a tail emptied by tail rules 2 and 3. Always within [0, sustain].
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
    stating at its end — including every tail emptied by tail rules 2 and 3, which it skips by
    construction.
    */
    std::vector<std::optional<Fraction>> rested_from;
};

/*!
\brief True where the curtain owns PART of a resting ribbon: the landmark lies strictly inside
the drawn tail.

The one reading of the verdict its two distance-scoped consumers share — the projection's
\ref NoteViewState::rested and the census's resting-ring row — so "rests, but nothing of it is
curtained" (a handed-over member, whose landmark is its own end) is decided once. The note-length
channel keys on the verdict itself (\ref chartNoteLengths), never on this.

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

It applies tail rules 1 to 4, in order, as \ref design_rulesets_tails lists them: crop each ink
end a margin before its binding onset, drop the tails of a short effect-free onset group, draw no
tail on a dead note that makes no noise, and mark where each tail that shows no technique rests.

\param connections The saved stream and the same-string relations the law reads
                   (\ref chartConnections).
\param margins The margin before every head of `connections.saved_notes` (\ref HeadMargins): the
               tail rule 1 crop reads it at the binding onset.
\param tempo_map Tempo map supplying the time axis tail rule 2 measures each ring on.

\return Per stored note, in the same order: the ink end and the law's verdict.
*/
[[nodiscard]] ChartPresentation chartPresentation(
    const ChartConnections& connections, const HeadMargins& margins, const TempoMap& tempo_map);

/*!
\brief Resolves each note's LENGTH: how long the 3D board shows the note as still in play.

The board pins the note's head at the hit line for this length, and it is not the same question
as what a tail draws. A
chugged riff under a hand-shape span draws no tails at all — no ring runs longer than the
kept-sustain bound — yet the shape is what tells the player to keep holding it, so the note
length outlives the picture. The 2D lane spends none of this.

`note_lengths[i]` is ONE RULE, and for a note inside a span it follows from the span, which
holds the shape: a LIVE fretting-hand member whose tail does not stand AT REST, covered by a
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
\param shapes Hand-posture spans sorted by place.

\return Per-note length in whole notes, sized like the inputs.
*/
[[nodiscard]] std::vector<Fraction> chartNoteLengths(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes);

} // namespace rock_hero::common::core
