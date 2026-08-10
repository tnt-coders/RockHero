/*!
\file chart.h
\brief Arrangement-owned chart model: the true tab of notes, shapes, and postures.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Musical grid position with exact sub-beat resolution.

Serializes as the tempo-map token grammar extended with an exact fraction:
`"<measure>:<beat>"` for whole beats and `"<measure>:<beat>+<n>/<d>"` for sub-beat positions.
*/
struct GridPosition
{
    /*! \brief One-based measure on the song grid. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};

    /*! \brief Exact fraction from this beat toward the next beat, in [0, 1). */
    Fraction offset{};

    /*!
    \brief Orders two grid positions along the timeline.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const GridPosition& lhs, const GridPosition& rhs) noexcept
    {
        // std::is_neq instead of `!= 0`: GCC's -Wzero-as-null-pointer-constant misfires on
        // ordering-vs-literal-zero comparisons (the standard comparison operator takes an
        // unspecified pointer-constructible parameter), so the named query keeps -Werror builds
        // clean without changing meaning.
        if (const auto measure_order = lhs.measure <=> rhs.measure; std::is_neq(measure_order))
        {
            return measure_order;
        }
        if (const auto beat_order = lhs.beat <=> rhs.beat; std::is_neq(beat_order))
        {
            return beat_order;
        }
        return lhs.offset <=> rhs.offset;
    }

    /*!
    \brief Compares two grid positions for equal value.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return True when both positions store equal values.
    */
    friend constexpr bool operator==(const GridPosition& lhs, const GridPosition& rhs) noexcept =
        default;
};

/*! \brief How a note's onset is produced when it is not a plain pick. */
enum class NoteAttack : std::uint8_t
{
    /*! \brief Plain picked onset. */
    Pick,
    /*!
    \brief Pinch harmonic: the pick stroke's thumb graze damps a node as the plectrum passes.

    An attack rather than a timbre because the graze happens *inside* the onset — one compound
    stroke with its own hand angle, not a pick plus a separate action. That is the same grain
    that already separates `Slap` from `Pop`. It also makes the pinch the one harmonic damped
    off the neck, which is why `nodeIsOnNeck` excludes it.
    */
    Pinch,
    /*! \brief Hammer-on from the previous note. */
    Hammer,
    /*! \brief Pull-off from the previous note. */
    Pull,
    /*! \brief Two-hand tap onset. */
    Tap,
    /*! \brief Popped (bass) onset. */
    Pop,
    /*! \brief Slapped (bass) onset. */
    Slap,
    /*!
    \brief Right-hand pick slide: the pick scrapes along the neck across the sustain.

    Fret data is right-hand travel like a tapped note's: `fret` is where the scrape starts,
    `slide_out` is the required unpitched terminal — its offset exactly at the sustain, because
    nothing rings past a scrape — and `slides` holds optional direction-turnaround waypoints,
    the whole path always traveling. The pitched techniques (mute, harmonic node, vibrato,
    tremolo, bend) are overridden while this attack is set: kept in memory so switching the
    attack back restores them, but suppressed by projections and omitted by the document
    writer. Accent is a scrape's own technique — an aggressively played scrape — never
    overridden.
    */
    PickSlide
};

/*!
\brief Snaps a notated open-string node label to the nearest true node offset.

Notation stores conventional labels rather than measured positions — the 7th partial is written
"2.7" or "2.8" against a true 2.669 — and a touch even slightly off a node chokes a high harmonic
instead of ringing it. This maps a label onto the physics: a string's nth-partial nodes sit at
`12*log2(n/(n-k))` fret units above its stop for `k = 1..n-1`, and fret positions are logarithmic,
so the stop and the offset simply add. Callers resolve the label against an open string and place
the result against the real stop.

\param notated Node label as written, in open-string fret units.
\param max_partial Highest partial to consider, so a label cannot snap onto an absurd high-order
                   node that happens to sit nearer to it.

\return Nearest true node offset. Always defined, since every partial from 2 up has nodes; a
        `max_partial` below 2 yields the octave.
*/
[[nodiscard]] double snapHarmonicNode(double notated, int max_partial);

/*!
\brief True when a harmonic's node lies on the neck, where a display can point at it.

Every harmonic damps its node with a finger on the fretboard except a **pinch**, whose thumb
grazes the string out over the body. A right-hand tap harmonic belongs on the neck side: the
damping finger is the picking hand's, but it lands on the fretboard.

Asks about the *attack* alone; callers pair it with their own `harmonic_node.has_value()`, which
reads plainly and stays visible to the optional-access checker, which cannot see through a wrapper.

\param attack How the onset is produced.

\return True unless the node is off the neck.
*/
[[nodiscard]] constexpr bool nodeIsOnNeck(NoteAttack attack) noexcept
{
    return attack != NoteAttack::Pinch;
}

/*!
\brief Reports whether the attack is produced by the picking hand at the neck (tap, pick slide).

These onsets never anchor, cover, or ring into a fretting-hand posture; the fret-hand
generator, posture derivation, chord grouping, and camera framing all share this predicate.
*/
[[nodiscard]] constexpr bool rightHandOnset(NoteAttack attack) noexcept
{
    return attack == NoteAttack::Tap || attack == NoteAttack::PickSlide;
}

/*! \brief Muting applied to a note. */
enum class NoteMute : std::uint8_t
{
    /*! \brief No muting. */
    None,
    /*! \brief Palm mute: pitched but damped. */
    Palm,
    /*! \brief Full fret-hand mute: percussive, unpitched. */
    Full
};

/*! \brief One point of a bend curve, positioned relative to the note onset. */
struct BendPoint
{
    /*! \brief Beat-fraction offset from the note onset, within the sustain. */
    Fraction offset{};

    /*! \brief Bend amount in semitones at this point; 0.5 is a quarter-tone curl. */
    double semitones{0.0};

    /*!
    \brief Compares two bend points by their stored fields.
    \param lhs Left-hand bend point.
    \param rhs Right-hand bend point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(const BendPoint& lhs, const BendPoint& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.offset == rhs.offset && std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*!
\brief One pitched slide-curve waypoint: by this offset the fret hand has glided to the fret.

Waypoints describe the note's own pitch curve — legato junctions, holds, and shift-slide glides
toward a re-picked landing — and are always pitched; the only unpitched gesture is the separate
\ref SlideOut terminal, so an "unpitched middle" cannot be written. A waypoint never sits on a
later onset of its own string: a shift-slide glide ends the minimum sustain distance before its
landing, and the landing note renders its own head. On a pick slide the waypoints are optional
direction turnarounds — unpitched right-hand travel bound by the same ordering rules — and the
gesture's terminal is its required \ref SlideOut.
*/
struct SlideWaypoint
{
    /*! \brief Beat-fraction offset from the note onset; strictly positive, within the sustain. */
    Fraction offset{};

    /*! \brief Target fret reached at this offset. */
    int fret{0};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(const SlideWaypoint& lhs, const SlideWaypoint& rhs) noexcept =
        default;
};

/*!
\brief Unpitched slide-out: pressure releases and the pitch falls away off the note's end.

No landing note exists — that is what distinguishes a slide-out from a pitched glide, which is
plain \ref SlideWaypoint data — so the gesture legitimately owns its own end offset and target
fret; there is no other event to desync from. A pick slide's slide-out is its required terminal,
pinned exactly to the sustain end, which a truncation may park exactly on the silencing next
onset.
*/
struct SlideOut
{
    /*! \brief Beat-fraction offset from the note onset where the slide-out ends; strictly after
        every curve waypoint, within the sustain. */
    Fraction offset{};

    /*! \brief Fret the slide-out gestures toward; never a sounded landing. */
    int fret{0};

    /*!
    \brief Compares two slide-outs by their stored fields.
    \param lhs Left-hand slide-out.
    \param rhs Right-hand slide-out.
    \return True when both store equal values.
    */
    friend constexpr bool operator==(const SlideOut& lhs, const SlideOut& rhs) noexcept = default;
};

/*!
\brief One string sounding once: the only event kind in the note stream.

A strummed chord is simultaneous notes at one position; shape spans supply the notation layer.
A zero sustain means the note has no sustain tail.
*/
struct ChartNote
{
    /*! \brief Musical onset position. */
    GridPosition position;

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*! \brief Sustain duration in beats; zero means no sustain. */
    Fraction sustain{};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*! \brief Muting applied to the note. */
    NoteMute mute{NoteMute::None};

    /*!
    \brief String position of the harmonic node, in fret units — and the assertion that this
    note *is* a harmonic.

    There is no separate harmonic field: the node is what makes a note a harmonic, so its
    presence is the claim, and a node cannot disagree with a kind that no longer exists. Node
    points are not fret positions (the 3.2 / 2.7 / 5.8 family), which is why this is a `double`
    while `fret` stays the integer the fretting hand stops.

    Which hand damps the node is carried by `attack`: every attack damps with a finger on the
    neck except `Pinch`, whose thumb grazes the string over the body — ask `nodeIsOnNeck` rather
    than testing the attack directly. On a pinch the value is where the picking hand grazes,
    which no surface shows yet (roadmap 25-Q5).

    **Every** harmonic has one, a pinch included: the overtone that squeals is *determined* by
    where the thumb lands, so an absent node is missing data rather than a different technique,
    and `chart_rules` refuses a `Pinch` carrying none.
    */
    std::optional<double> harmonic_node{};

    /*! \brief True when the note is played with vibrato. */
    bool vibrato{false};

    /*!
    \brief True when the note is unmeasured noise picking — as fast as possible, no real
    timing.

    The charting standard reserves this for true noise (an outro strummed purely for sound);
    measured fast repetition is spelled out as discrete notes instead, so every timed pick is
    its own chart event. Deliberately named for the technique: tremolo picking is *pitched*
    noise — the fret still sets a measurable pitch — where a scrape is *unpitched* noise carried
    by its attack, which is why the two never share a field and a pick slide never sets this
    flag.
    */
    bool tremolo{false};

    /*! \brief True when the note is accented. */
    bool accent{false};

    /*! \brief Bend curve across the sustain; empty when the note is not bent. */
    std::vector<BendPoint> bend;

    /*! \brief Pitched slide-curve waypoints across the sustain; empty when the curve is flat. */
    std::vector<SlideWaypoint> slides;

    /*! \brief Unpitched slide-out off the note's end; absent when the tail simply ends. */
    std::optional<SlideOut> slide_out{};

    /*!
    \brief Compares two notes by their stored fields.
    \param lhs Left-hand note.
    \param rhs Right-hand note.
    \return True when both notes store equal values.
    */
    friend bool operator==(const ChartNote& lhs, const ChartNote& rhs) = default;
};

/*!
\brief Returns the note's unpitched slide-out as a nullable pointer.
\param note Note whose tail is inspected.
\return Address of the slide-out when present, or nullptr when the tail simply ends.

Binding the optional behind a parameter lets call sites null-check instead of dereferencing an
optional, and keeps clang-tidy's unchecked-optional-access analysis reliable inside note loops,
where a has_value() guard on the loop variable's own member is not otherwise credited.
*/
[[nodiscard]] inline const SlideOut* slideOutOrNull(const ChartNote& note) noexcept
{
    return note.slide_out.has_value() ? &*note.slide_out : nullptr;
}

/*!
\brief Reusable hand posture: per-string frets and fingerings.

Array index 0 is the lowest-pitched string; null entries mean the string is not part of the
posture. Fingers use 0 for the thumb and 1-4 for index through pinky.
*/
struct ChordTemplate
{
    /*! \brief Display name; may be empty for unnamed shapes. */
    std::string name;

    /*! \brief Fret held per string; nullopt when the string is not part of the posture. */
    std::vector<std::optional<int>> frets;

    /*! \brief Finger per string; nullopt when unspecified or unused. */
    std::vector<std::optional<int>> fingers;

    /*!
    \brief Compares two templates by their stored fields.
    \param lhs Left-hand template.
    \param rhs Right-hand template.
    \return True when both templates store equal values.
    */
    friend bool operator==(const ChordTemplate& lhs, const ChordTemplate& rhs) = default;
};

/*!
\brief The fret the **fretting hand** occupies for this note.

Not the same as `note.fret`, which is the **stop**. A fret-hand harmonic — `fret == 0` plus a
node, with neither tapping-hand attack — holds no stop, so the hand is at the node, the only
place it touches the string. Every other node-bearing note keeps the hand on its fret: a pinch
and a two-hand tap because the node belongs to the picking hand, and a harmonic over a real stop
(`fret > 0` — the harp and artificial-harmonic family) because the fretting hand is pressing that
stop while the picking hand damps the node.

Fret `N` occupies the neck from wire `N-1` to wire `N` (`highwayNoteCenterX` is the midpoint of
those two), so the fret containing a node at `p` is `ceil(p)`: 2.669 lies in fret 3 and 3.156 in
fret 4. **Neither `round` nor `floor` works.** A fret-hand window over frets `[f, f+w-1]` covers
fret units `[f-1, f+w-1]`, so when the harmonic is the window's edge note the window only reliably
covers `[H-1, H]` for the fret `H` it was given. Measured over every node below fret 25, `floor`
leaves the head outside the window 18 times and `round` 7 times; `ceil` never does.

\param note Note to place.

\return Fret the fretting hand is on; zero when nothing stops the string.
*/
[[nodiscard]] int fretFor(const ChartNote& note);

/*!
\brief The note as a saved document records it: in-memory latent overrides stripped.

The one seam between memory and document. A pick slide overrides the pitched techniques in
memory — kept so toggling the attack back restores them — but a saved scrape never carries them;
the writer emits this form, and the editor's plan gate validates it, so the two can never
disagree about what a legal document is.

\param note Note as held in memory.

\return The note as the document writer records it.
*/
[[nodiscard]] ChartNote savedChartNote(const ChartNote& note);

/*!
\brief True when the note is a harmonic damped by the fretting hand touching its node.

The key E7/E9/E19 turn on: a node with no real stop (`fret == 0` — the string speaks from the nut
or the capo) and no attack whose node belongs to the OTHER hand or to nothing at all. `Pinch` is
excluded because its thumb grazes off the neck. `PickSlide` is excluded because a scrape's node is
never a sounding node at all: E2 forbids one in any saved chart, so a node found on a scrape is
purely the in-memory latent the attack toggle preserves (chart.h's override contract), and reading
it as a fretting-hand touch made three things go wrong at once — the legato repair (which runs on
the in-memory stream) refused to release from a scrape while validation (which runs on the SAVED
stream, where the node is stripped) allowed it, silently downgrading a pull-off that D7 ruled
valid; and the importer's shed pass stripped the scrape's REQUIRED slide-out terminal, producing a
chart that E2 then rejected on re-read. `Tap` is NOT excluded — an open-string tap harmonic has
nothing pressed either, which is exactly what those rules test. Contrast `fretFor`'s node branch,
which additionally excludes `Tap` because the hand-placement question cares which HAND owns the
node, not whether a stop is pressed.

\param note Note to classify.

\return True when the fretting hand touches the node and presses nothing.
*/
[[nodiscard]] inline bool fretHandHarmonic(const ChartNote& note) noexcept
{
    return note.harmonic_node.has_value() && note.fret == 0 && note.attack != NoteAttack::Pinch &&
           note.attack != NoteAttack::PickSlide;
}

/*! \brief Where a note sounds on the fret axis, and whether that place is a node or a fret. */
struct SoundingPosition
{
    /*! \brief True when the position is a harmonic NODE rather than a fret. */
    bool at_node{false};

    /*!
    \brief The position in fret units — fractional for a node, the stop's own number otherwise.
    */
    double position{0.0};
};

/*!
\brief Where a note SOUNDS on the fret axis at a given stop.

The one authority for a fact both surfaces need and each used to derive: a harmonic sounds at its
NODE, not at the stop under the finger, and the node RIDES that stop — fret spacing is logarithmic,
so the node's offset above the stop is constant in fret units and a glide that moves the stop moves
the node by the same amount. That is what lets one rule serve every point of a gesture: the onset
passes the note's own fret, a slide junction the fret it has travelled to.

A pinch is the exception the `at_node` flag exists for as much as the position is: its node is over
the body rather than on the neck, so a pinch sounds at its stop as far as any neck coordinate goes
(the squeal's own cue is roadmap 25-Q5). Callers need the flag because a node and a fret are read
differently — 2D labels a node to one decimal and a fret as a whole number, 3D places a node on the
fret line and a fret at its slot's midpoint.

\param harmonic_node The note's node, if it has one.
\param attack The note's attack, which decides whose hand owns the node.
\param note_fret The note's own stop.
\param fret_at_point The stop being asked about — `note_fret` at the onset.

\return The sounding place, and whether it is a node.
*/
[[nodiscard]] constexpr SoundingPosition soundingPositionAt(
    const std::optional<double>& harmonic_node, NoteAttack attack, int note_fret,
    int fret_at_point) noexcept
{
    if (harmonic_node.has_value() && nodeIsOnNeck(attack))
    {
        return SoundingPosition{
            .at_node = true,
            .position = *harmonic_node + static_cast<double>(fret_at_point - note_fret),
        };
    }
    return SoundingPosition{.at_node = false, .position = static_cast<double>(fret_at_point)};
}

/*!
\brief True when the FRETTING finger is standing on the note's node.

A refinement of \ref fretHandHarmonic rather than a near twin, and it is spelled as one so the two
can never drift: that predicate asks whether anything is PRESSED at a node on the neck, and this
adds the one further question of which HAND owns it. A two-hand tap harmonic's node belongs to the
picking hand, which is on the neck rather than off it, so a tap is the single exclusion.

Restating the conditions here instead let the two disagree about a pick slide. This one used to
read `nodeIsOnNeck`, which excludes only a pinch, so a scrape carrying a latent node at fret 0
counted as a fretting finger and \ref fretFor sent the hand to the node — exactly the failure
\ref fretHandHarmonic excludes the scrape to avoid.

Two things turn on this one fact and each used to spell it out: where the fretting hand sits
(\ref fretFor returns the node's fret instead of the note's), and how far up the node may lie — a
finger cannot be past the last fret, so the neck caps it rather than the string.

Deliberately NOT what the 3D board asks when placing a note: a note sounds from its node whichever
hand is damping it, so the board's own axis ignores which hand that is.

\param note Note to classify.

\return True when the fretting hand's finger is the one touching the node.
*/
[[nodiscard]] inline bool frettingFingerOnNode(const ChartNote& note) noexcept
{
    return fretHandHarmonic(note) && note.attack != NoteAttack::Tap;
}

/*!
\brief The fret the note's finger occupies when the note ends — what a following pull-off
releases from.

A note that glided hands over its last pitched waypoint, not its onset fret (a 5→7 slide releases
from 7); a scrape hands over its slide-out's end, the travel's terminus. An unpitched trail-off on
an ordinary note is already a release, so the last pitched position still rules.

\param note Note whose end position is read.

\return Fret at the note's end.
*/
[[nodiscard]] int releasedFret(const ChartNote& note);

/*!
\brief Hand-posture span referencing a chord template.

One mechanism covers strummed chords, chugged riffs on a held shape, and arpeggios: the notes
under the span are the sounding truth, the shape adds the notation layer (name, box or bracket,
fingering). Whether the span renders as a chord box or an arpeggio bracket derives from whether
its notes arrive together or sequentially.
*/
struct ChartShape
{
    /*! \brief Musical start of the span. */
    GridPosition position;

    /*! \brief Span duration in beats; strictly positive. */
    Fraction sustain{};

    /*! \brief Index into the chart's chord template table. */
    std::size_t chord{0};

    /*!
    \brief Compares two shapes by their stored fields.
    \param lhs Left-hand shape.
    \param rhs Right-hand shape.
    \return True when both shapes store equal values.
    */
    friend bool operator==(const ChartShape& lhs, const ChartShape& rhs) = default;
};

/*! \brief Fret-hand position: where the hand sits on the neck from this point on. */
struct FretHandPosition
{
    /*! \brief Musical position the hand arrives at this placement. */
    GridPosition position;

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand; four unless the passage stretches wider. */
    int width{4};

    /*!
    \brief Compares two fret-hand positions by their stored fields.
    \param lhs Left-hand entry.
    \param rhs Right-hand entry.
    \return True when both entries store equal values.
    */
    friend constexpr bool operator==(
        const FretHandPosition& lhs, const FretHandPosition& rhs) noexcept = default;
};

/*! \brief Instrument tuning for one arrangement. */
struct ChartTuning
{
    /*!
    \brief Open-string pitches from the lowest-pitched string upward, as note names with octave
    such as "E2". The array length defines the arrangement's string count everywhere.
    */
    std::vector<std::string> strings;

    /*! \brief Capo fret; zero means no capo. */
    int capo{0};

    /*! \brief Fine tuning offset in cents. */
    double cent_offset{0.0};

    /*!
    \brief Compares two tunings by their stored fields.
    \param lhs Left-hand tuning.
    \param rhs Right-hand tuning.
    \return True when both tunings store equal values.
    */
    friend bool operator==(const ChartTuning& lhs, const ChartTuning& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.strings == rhs.strings && lhs.capo == rhs.capo &&
               std::is_eq(lhs.cent_offset <=> rhs.cent_offset);
    }
};

/*!
\brief The true tab of one arrangement.

Notes say what sounds; shapes say what the hand holds; templates are reusable postures. There is
exactly one chart per arrangement — difficulty is a derived rating, never authored variants.
*/
struct Chart
{
    /*! \brief Instrument tuning; the strings array length is the string count everywhere. */
    ChartTuning tuning;

    /*! \brief Reusable hand postures referenced by shapes, in table order. */
    std::vector<ChordTemplate> templates;

    /*! \brief Every sounding onset, sorted by (position, string). */
    std::vector<ChartNote> notes;

    /*! \brief Hand-posture spans, sorted by position. */
    std::vector<ChartShape> shapes;

    /*! \brief Fret-hand positions, sorted by position. */
    std::vector<FretHandPosition> fret_hand_positions;

    /*!
    \brief Compares two charts by their stored fields.
    \param lhs Left-hand chart.
    \param rhs Right-hand chart.
    \return True when both charts store equal values.
    */
    friend bool operator==(const Chart& lhs, const Chart& rhs) = default;
};

} // namespace rock_hero::common::core
