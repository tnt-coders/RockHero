/*!
\file chart_shapes.h
\brief The hand-posture derivation: the spans a note stream implies, and the postures they hold.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One hand posture: the fret held on each string while a span runs.

Array index 0 is the lowest-pitched string; a null entry means the string is not part of the
posture. The array is \ref g_max_chart_strings long — the model's own bound on a string number,
not a statement about the tuning — so a chart with fewer strings simply leaves the top slots
empty, and two postures always compare by their held frets alone.

Derived, never authored — the frets an onset's struck members hold, plus whatever was still ringing
across it, plus the stops a \ref ChartHoldMarker says the hand takes silently
(\ref deriveChartShapes). A fret carries no provenance here on purpose: the posture is what the hand
holds, and where a given stop came from is the SPAN's question (\ref ChartShape::silent_member), so
two spans holding identical frets stay one deduplicated posture however each was learned. Chord
names and fingerings carry no field here because nothing writes one; when they are authored they
become a dictionary keyed by a posture rather than members of it.
*/
struct ChartPosture
{
    /*! \brief Fret held per string; nullopt when the string is not part of the posture. */
    std::vector<std::optional<int>> frets;

    /*!
    \brief Compares two postures by their held frets.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return True when both hold the same fret on every string.
    */
    friend bool operator==(const ChartPosture& lhs, const ChartPosture& rhs) = default;
};

/*!
\brief Hand-posture span: how long one posture is held, and which posture it is.

One mechanism covers strummed chords, chugged riffs on a held shape, and arpeggios: the notes under
the span are the sounding truth, the span adds the notation layer (box or bracket). Whether it
renders as a chord box or an arpeggio bracket is a further derivation from the notes at its start
(\ref chartShapeArrivals), not a property stored here.
*/
struct ChartShape
{
    /*! \brief Musical start of the span. */
    GridPosition position;

    /*!
    \brief Span duration in beats; zero where every member is silent.

    A span runs as far as its members ring, and a hold marker rings for nothing — so a span whose
    members are ALL markers has no sounding evidence of duration at all and states its posture at
    an instant. That is not a degenerate case to guard against but the honest answer: the bracket
    draws at the span start whatever the length, and the rails a positive span draws simply have
    nothing to cover. Every span with a sounding member is strictly positive, as before.
    */
    Fraction sustain{};

    /*! \brief Index into the posture table derived alongside (\ref ChartShapes::postures). */
    std::size_t posture{0};

    /*!
    \brief True when a posture member of this span came from a hold marker rather than from sound.

    The one fact the arrival rule (\ref chartShapeArrivals) cannot re-derive from the notes, and the
    reason it is carried here instead of asked again: this walk is what resolved the markers, so
    stating the answer on the span it resolved them into is one authority publishing its result,
    where a second scan of the marker array beside the arrival would be the same rule written twice
    and free to disagree.

    The per-span summary of \ref ChartShapes::marker_shapes, which names the resolution marker by
    marker. Both are written in the same loop of the same pass, so they cannot disagree; this one
    exists because the arrival rule asks the question once per SPAN and scanning the marker array
    for each span would make one classification quadratic in the markers.

    It is what flips the span to an arpeggio. The bracket is the only mark that states a posture
    fret at all — a chord box draws the notes' own heads — so a span carrying a silently-held member
    must arrive as an arpeggio or the authored fact is stored and never shown.
    */
    bool silent_member{false};

    /*!
    \brief Compares two spans by their stored fields.
    \param lhs Left-hand span.
    \param rhs Right-hand span.
    \return True when both spans hold equal values.
    */
    friend bool operator==(const ChartShape& lhs, const ChartShape& rhs) = default;
};

/*! \brief The spans a note stream implies, with the posture table those spans index. */
struct ChartShapes
{
    /*! \brief Hand-posture spans, sorted by position. */
    std::vector<ChartShape> shapes;

    /*! \brief The postures the spans index, in first-appearance order and deduplicated. */
    std::vector<ChartPosture> postures;

    /*!
    \brief Per input hold marker, the span its stop joined; absent where the marker stayed inert.

    Same order and size as the `hold_markers` argument, so a caller indexes it by the marker it
    already holds. This is where a marker BECOMES visible: the posture bracket that prints its stop
    draws at that span's start, so the editor reads this to place the marker's own mark and to hit
    test it, and an absent entry means the marker resolved to nothing and therefore draws nowhere —
    which is exactly the property "nothing undrawn is clickable" needs, published by the pass that
    knows rather than re-derived by the surface.
    */
    std::vector<std::optional<std::size_t>> marker_shapes;
};

/*!
\brief Derives the hand-posture spans and postures a note stream implies.

The chart stores no spans: a span is a statement about the notes under it, so deriving it is the
only way it can never disagree with them. This is that derivation, run once per chart revision
inside \ref chartResolutions and read from there by everything that draws a chord box, an arpeggio
bracket, or a span-implied hold.

A span opens at a slot holding two or more MEMBERS, where a member is a sounding fretting-hand
onset there or a \ref ChartHoldMarker there — one sound plus one held finger opens a span, two held
fingers with nothing sounding open one, and a LONE member of either kind opens nothing (user ruling
2026-08-27, correcting the sound-only threshold this shipped with). The posture is deduplicated by
its fret vector, and consecutive onsets holding the same articulation merge into one span covering
the strums' own rings — the grouping the tab renders as a chord box over repeated strums. Tap-only
onsets are transparent to the whole derivation: taps are the tapping hand, so they neither form
postures nor close held spans, letting a ringing chord's span cover the taps above it. ANY
articulation difference is a new chord: span continuity compares each string's whole note with only
its position and duration neutralized, so attack (hammer/pull/tap/slap/pop), muting, harmonics,
vibrato, tremolo, emphasis, bends, and slides — and any technique added to \ref ChartNote later —
all split the span, while strum durations never do. The posture table stays deduplicated by frets
alone (the hand posture is identical; techniques render on the notes). A note still ringing through
a chord's onset (tie-held from before, not re-struck) joins the posture on its string; the shared
arrival rule (\ref chartShapeArrivals) then renders the partly-struck span as an arpeggio, while
fully-strummed spans stay chord boxes — no other arpeggio grouping is derived. A span closed by a
following event trims to the minimum-sustain-distance margin before it
(\ref minimumSustainDistanceBeats at the closing onset's measure), floored at the last strum, with
an exact-adjacency fallback when even that would leave no length — the same margin every other
element keeps.

A lone onset does NOT close a span when it is a re-pick of a string that span already holds — by
sound with unchanged articulation, or by an authored hold — and at least one other member is still
ringing. The hand demonstrably has not left the shape, and every fact needed to know that is
already in the stream, so this is derived rather than authored: it is the one-note-at-a-time broken
chord over a held shape. It cannot OPEN a span, only extend one, which is also what widens the
span's right-hand scan and can turn a following box into an arpeggio.

A \ref ChartHoldMarker lying inside a derived span joins that span's posture on its string — the
one thing here that is authored rather than read off the sound, because no function of a note
stream can distinguish a held finger from an absent one. Its fret is its own where it carries one,
and otherwise comes from the first note that sounds on that string later in the same span; a marker
that resolves to no fret at all, or that lies past the span's own end, contributes nothing and is
inert. A marker is still not a STRIKE — it never closes a span and never ends a held posture — but
it is a MEMBER, so two of them at one slot open a span where no shape is still RINGING, and one
beside a single sounding note does too. Under a shape that is still ringing the markers join it
rather than restating it, which is what keeps a finger added mid-shape from splitting the shape it
joins; past that shape's ring they state the next one, because a span outlives its sound only so a
later identical strum can rejoin it (rule 11) and that is a merging rule, not a claim that the hand
is still down. Because a marker's fret may only be known once the span is complete, the posture is
keyed at the span's CLOSE rather than at each onset — which is also why one span keys one posture
instead of every strum re-keying the same one.

A span every one of whose members is a marker has nothing sounding to give it length, so it runs
from its start to its start: its posture is stated at an instant, which is exactly where the
bracket that prints it draws. Its claims still resolve — a claim at the span's own start is inside
it whatever the length — so two fret-carrying markers state a posture nothing sounds, while two
fret-less ones have no later in-span note to take a fret from and stay inert, the same degrade as
every other unresolvable claim.

Articulation is read from the PRESENTED notes and span extent from the stored rings, which is the
split the box states: what the chord LOOKS like is what the surfaces draw (a tail the presentation
rules compressed carries a compressed gesture, and two strums that draw identically are one box),
while how far the hand keeps holding is the actual ring behind the picture.

The maintained plain-English spec is "Posture and shape derivation" in
`docs/developer/the-project-lifecycle.md`.

\param saved_notes Note stream in SAVED form, sorted by (position, string); read for its rings.
\param presented_notes The same notes through \ref presentedChartNotes, in the same order and of
                       the same size; read for the articulation two strums are compared by.
\param hold_markers Silently-held shape members, sorted by (position, string)
                    (\ref Chart::hold_markers).
\param tempo_map Tempo map supplying the exact beat axis and the meter at each closing onset.

\return The derived spans, the posture table they index, and each marker's resolution.
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const std::vector<ChartHoldMarker>& hold_markers, const TempoMap& tempo_map);

/*!
\brief Classifies every shape span as an arpeggio or a strummed chord box.

The second half of the same derivation, and here beside the first for that reason: \ref
deriveChartShapes says where the hand goes and how long it stays, this says which of the two marks
the notation draws. Neither is authored, so neither has a rule a document could break.

The arrival rule shared by the highway and tab projections: a span is an arpeggio when fewer
than two notes strike at its start, when a posture string is still ringing there without being
re-struck (an earlier note's PRESENTED tail crosses the span start on a posture string with no
onset at it), when a picking-hand onset — a tap or a pick slide — sounds anywhere within the
span, or when the span holds a silently-held member (\ref ChartShape::silent_member). A strum under
held content is picking around it, and a held chord under two-hand tapping is sustained through the
taps rather than fully strummed, so the shape renders as brackets around individual notes instead
of one strummed box.

A posture string that is merely SILENT at the start — a partial strum of the shape — still does not
make an arpeggio, and that clause is no longer a compromise: "merely silent" and "known held" used
to be indistinguishable, which is the whole reason the rule had to pick one; a hold marker is what
tells them apart, and it says so on the span rather than being guessed at here.

Asked of the PRESENTED stream (\ref presentedChartNotes), like every other fact a surface draws:
the question is what still SOUNDS across the span start, and a dead string's stored ring is timing
rather than sound — E25 is exactly the rule that takes a dead note's tail away, and it lives in
presentation. Everything else the rule reads (positions, strings, attacks) comes through
presentation untouched, and a ring that genuinely crosses a span start is presented whole anyway:
a span starts at a two-note onset, so such a ring runs strictly past its own first binding onset
and rule 1 exempts it.

Answers all the shapes at once because the rule needs to look BACKWARD — to each posture string's
most recent earlier note — and one forward cursor over the sorted notes carries exactly that with
no walking back. The remaining per-shape scans stay local to each span; what this batching removed
is the unbounded backward walk, which reached the first note in the song whenever a posture string
had none and which both projections then paid for every shape on every chart revision.

\param presented_notes Notes as drawn, sorted by (position, string).
\param shapes Hand-posture spans, sorted by position (\ref ChartResolutions::shapes).
\param postures Posture table the spans index (\ref ChartResolutions::postures).
\param tempo_map Song tempo map, for signature-exact sustain-crossing checks.
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
