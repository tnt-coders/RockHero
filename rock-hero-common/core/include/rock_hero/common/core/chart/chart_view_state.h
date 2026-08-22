/*!
\file chart_view_state.h
\brief Seconds-resolved chart content shared by the 2D tablature lane and the 3D highway.
*/

#pragma once

#include <compare>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief One bend curve point resolved to an absolute timeline second. */
struct BendPointViewState
{
    /*! \brief Absolute timeline position of this curve point. */
    double seconds{0.0};

    /*! \brief Bend amount in semitones at this point. */
    double semitones{0.0};

    /*!
    \brief Compares two bend points by their stored fields.
    \param lhs Left-hand point.
    \param rhs Right-hand point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(
        const BendPointViewState& lhs, const BendPointViewState& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on a
        // floating member, which is why every float-bearing view state here is spelled out. Exact
        // equality is intended; the ordering query expresses it warning-free with identical
        // semantics (NaN compares unequal either way).
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*! \brief One slide waypoint resolved to an absolute timeline second. */
struct SlideViewState
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this waypoint. */
    int fret{0};

    /*!
    \brief True when this waypoint is unpitched travel rather than a pitched arrival.

    NOT "the glide trails off here". A pick slide's every waypoint carries this, because a scrape's
    whole path is unpitched travel — the turnarounds included — so reading it as an ending mis-draws
    every scrape. An ordinary note's unpitched trail-off also sets it, and there the release reading
    does hold, but the field itself only ever says "this position is not a pitched stop".
    */
    bool unpitched{false};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(const SlideViewState& lhs, const SlideViewState& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.unpitched == rhs.unpitched;
    }
};

/*!
\brief One sounding note resolved to timeline seconds: the PRESENTED form of a chart note.

Not the stored one. `ChartNote::sustain` is the actual duration the string rings, and what a
surface draws is derived from it once per chart revision by \ref presentedChartNotes — the tail
trimmed to clear the next head, floored on payload that still says something, dropped where it was
never a deliberate sustain, absent on a dead note. Every field here comes from that derivation, so
`end_seconds`, the bend curve, the slide waypoints and the flattened slide-out all describe the
presented note and nothing has to trim a second time.

**Scored = presented** (`docs/plans/in-progress/note-sustain-model.md`, ruling 4). When the scorer
exists it reads this, not the chart: what the player is asked to hold is exactly what the board
showed them. That contract is why the derivation lives in common/core rather than in a painter —
the game must be able to reach it without a surface.
*/
struct NoteViewState
{
    /*! \brief Absolute onset position. */
    double start_seconds{0.0};

    /*!
    \brief Absolute end of the presented tail; equals start_seconds when no tail is presented.

    The DRAWN and scored length, never the stored ring: a sub-quarter chug rings for its eighth and
    presents nothing. This is the whole of what the 2D lane draws; the 3D board additionally pins a
    span-held strum's heads past it (\ref ChartViewState::display_hold_ends).
    */
    double end_seconds{0.0};

    /*!
    \brief One-based chart string, counted from the lowest-pitched string.

    Never shifted for display: a surface's "show at least N strings" minimum adds empty lanes
    BELOW the chart's strings, and each surface maps chart strings onto those lanes when it lays
    out (\ref displayedLane) — so the scene holds one chart fact and the two surfaces cannot
    disagree about which string a note is on.
    */
    int string{1};

    /*!
    \brief Fret sounded; zero is the open string — or a natural harmonic, whose position lives in
    \ref harmonic_node instead. Ask \ref openString rather than testing this against zero.
    */
    int fret{0};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*!
    \brief What this note's connection claim resolves to (\ref resolveLegato).

    The chart stores a claim and never a direction, so the drawn hammer-on or pull-off mark can only
    come from here. `Unjustified` is not a state of its own on either surface: a claim nothing
    justifies draws exactly what the plain pick beside it draws, which is why no cue exists and why
    every attack outside \ref legatoClaimable leaves this at its default.
    */
    LegatoMotion legato{LegatoMotion::Unjustified};

    /*! \brief True when the picking hand's palm damps the string (`ChartNote::palm_mute`). */
    bool palm_mute{false};

    /*! \brief True when the string is deadened into an unpitched click (`ChartNote::dead`). */
    bool dead{false};

    /*!
    \brief Harmonic node in fret units, and the assertion that this note is a harmonic.

    Mirrors `ChartNote::harmonic_node`, carrying the chart's exact node point (the 3.2 / 2.7 /
    5.8 family): presence is the whole test, and every harmonic carries one, a pinch included
    (rule-enforced). The highway places a harmonic head at the true node; the lane, which has no
    fretboard axis, selects the diamond head from it and prints the node as the head's label.

    A pinch's node lies off the neck where the thumb grazes, so ask `nodeIsOnNeck` before
    anchoring a head to it or labeling a head with it. Both surfaces today draw only a pinch's
    fretted stop — its left-hand half — and how the right-hand node will be shown is an open
    question, not a ruling.
    */
    std::optional<double> harmonic_node{};

    /*! \brief True when the note is played with vibrato. */
    bool vibrato{false};

    /*! \brief True when the note is tremolo picked. */
    bool tremolo{false};

    /*! \brief How hard the note is struck relative to its neighbours. */
    NoteEmphasis emphasis{NoteEmphasis::Normal};

    /*! \brief Bend curve points in ascending time order; empty when not bent. */
    std::vector<BendPointViewState> bend;

    /*!
    \brief Slide waypoints in ascending time order; empty when the note does not slide.

    The unpitched slide-out is flattened onto the end as one more waypoint, so every consumer keeps
    one uniform segment model. A scrape's slide-out is its required terminal and flattens the same
    way.
    */
    std::vector<SlideViewState> slides;

    /*!
    \brief Compares two note view states by their stored fields.
    \param lhs Left-hand note.
    \param rhs Right-hand note.
    \return True when both notes store equal values.
    */
    friend bool operator==(const NoteViewState& lhs, const NoteViewState& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.string == rhs.string &&
               lhs.fret == rhs.fret && lhs.attack == rhs.attack && lhs.legato == rhs.legato &&
               lhs.palm_mute == rhs.palm_mute && lhs.dead == rhs.dead &&
               lhs.harmonic_node == rhs.harmonic_node && lhs.vibrato == rhs.vibrato &&
               lhs.tremolo == rhs.tremolo && lhs.emphasis == rhs.emphasis && lhs.bend == rhs.bend &&
               lhs.slides == rhs.slides;
    }
};

/*!
\brief True when the glide continues the same note at this waypoint rather than ending it.

Decided by the waypoint's place in the sustain and nothing else: strictly inside means the note is
still sounding, so the lane draws its linked continuation head there in the note's own head shape;
exactly at the sustain end means a shift-slide glide-end, where the note stops and the re-picked
landing draws its own head, so no linked glyph. Being \ref SlideViewState::unpitched does not
unlink a waypoint — a scrape's turnaround is one gesture continuing, and its head is what keeps the
corner from reading as a break. A flattened slide-out sits at the sustain end by rule, so it is
never linked.

A READ of two shared facts, not a stored field, so the one continuation rule cannot be restated
per surface.

\param note Note the waypoint belongs to.
\param waypoint One of the note's \ref NoteViewState::slides entries.
\return True when the waypoint is a continuation of the note.
*/
[[nodiscard]] constexpr bool linkedWaypoint(
    const NoteViewState& note, const SlideViewState& waypoint) noexcept
{
    return waypoint.seconds < note.end_seconds;
}

/*!
\brief True when nothing stops OR touches the string: a genuine open string.

Fret zero alone cannot answer this — a natural harmonic (and a tap harmonic on an open string)
also stores fret 0, with the node carrying its position, and rendering one as an open string
erased the harmonic from the board outright: every decision between the open-string treatment
(the hand-window bar, the window-spanning tail band, the faded tail edge) and the fretted treatment
must ask this instead of testing `fret == 0`.

\param note Note to classify.
\return True when the note is an open string with no harmonic node.
*/
[[nodiscard]] inline bool openString(const NoteViewState& note) noexcept
{
    return note.fret == 0 && !note.harmonic_node.has_value();
}

/*!
\brief The fret slot the note's fretting hand occupies.

\ref fretFor through the mirrored fields: a natural harmonic's finger stands on the node, so its
slot is the fret CONTAINING the node (the ceil law chart.h derives), while every other note's slot
is its own fret. This is what fret-aligned furniture — the onset span line, the hit-glow fret
lines — aligns to; a head itself keeps the node's exact fractional position.

\param note Note to place.
\return Fret slot of the fretting hand; zero for a true open string.
*/
[[nodiscard]] inline int fretFor(const NoteViewState& note)
{
    return fretFor(note.fret, note.harmonic_node, note.attack);
}

/*! \brief What the hand holds on one string under a shape span. */
struct ShapeStringViewState
{
    /*! \brief One-based chart string (unshifted, like \ref NoteViewState::string). */
    int string{1};

    /*! \brief Fret held on the string; zero is the open string. */
    int fret{0};

    /*! \brief Finger holding the fret (0 thumb, 1-4 index through pinky); empty when unknown. */
    std::optional<int> finger{};

    /*!
    \brief Compares two posture entries by their stored fields.
    \param lhs Left-hand entry.
    \param rhs Right-hand entry.
    \return True when both entries store equal values.
    */
    friend constexpr bool operator==(
        const ShapeStringViewState& lhs, const ShapeStringViewState& rhs) noexcept = default;
};

/*! \brief One hand-posture span resolved to timeline seconds for rendering. */
struct ShapeViewState
{
    /*! \brief Absolute start of the span. */
    double start_seconds{0.0};

    /*! \brief Absolute end of the span. */
    double end_seconds{0.0};

    /*! \brief Chord template display name; may be empty for unnamed shapes. */
    std::string name;

    /*!
    \brief True when the span's notes arrive sequentially (arpeggio brackets) rather than
    together (chord box). Derived at projection time from the notes under the span start.
    */
    bool arpeggio{false};

    /*!
    \brief Posture entries from the shape's chord template, lowest string first; empty when the
    template is unknown.

    The whole held posture, stated whether or not a note sounds on the string: a posture is a
    claim about the fretting hand, not about what is struck. The board's fingering panel reads
    every span's entries; the arpeggio brackets on both surfaces read an arpeggio span's.
    */
    std::vector<ShapeStringViewState> strings;

    /*!
    \brief Compares two shape view states by their stored fields.
    \param lhs Left-hand shape.
    \param rhs Right-hand shape.
    \return True when both shapes store equal values.
    */
    friend bool operator==(const ShapeViewState& lhs, const ShapeViewState& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.name == rhs.name &&
               lhs.arpeggio == rhs.arpeggio && lhs.strings == rhs.strings;
    }
};

/*! \brief One fret-hand placement resolved to a timeline second, with its eased approach. */
struct FhpViewState
{
    /*! \brief Absolute position the hand arrives at this placement. */
    double seconds{0.0};

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand. */
    int width{4};

    /*!
    \brief Duration of the hand's eased approach ending at \ref seconds; zero arrives instantly.

    When the fretting hand starts moving toward this placement is a fact about the chart, not
    about a surface, so it is derived once here: a placement landing exactly on a slide waypoint —
    pitched glide or unpitched trail-off end alike — ramps over that glide's own segment so a
    drawn hand travels with the drawn rail, and every other placement morphs over the
    minimum-sustain-distance margin at its meter (shortened when placements crowd closer than the
    ramp). The board's hand window animates it; the lane's static marker draws the arrival alone.
    */
    double ramp_seconds{0.0};

    /*!
    \brief True when \ref ramp_seconds spans an UNPITCHED glide, so an animated hand eases with the
    unpitched release curve instead of the pitched one.

    The hand follows whatever the rail draws, and the two families are different functions of
    progress (\ref highwaySlideEaseWeight). Easing every move with the pitched curve left the
    window and the rail sharing only their endpoints. Note the consequence: the unpitched curve
    arrives at full travel with nonzero slope, so the window stops abruptly at the release — which
    is exactly what the drawn rail does at the same instant.
    */
    bool unpitched_ramp{false};

    /*!
    \brief Compares two placements by their stored fields.
    \param lhs Left-hand placement.
    \param rhs Right-hand placement.
    \return True when both placements store equal values.
    */
    friend constexpr bool operator==(const FhpViewState& lhs, const FhpViewState& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.width == rhs.width && std::is_eq(lhs.ramp_seconds <=> rhs.ramp_seconds) &&
               lhs.unpitched_ramp == rhs.unpitched_ramp;
    }
};

/*!
\brief Seconds-resolved chart content for one arrangement: the scene both surfaces draw.

Built once per chart revision by \ref makeChartViewState and shared immutably: positions are
resolved through the tempo map at projection time so rendering never queries musical positions per
frame. The 2D tablature lane renders this directly; the 3D highway composes it inside
\ref HighwayViewState beside the board-only structure it adds. One producer, so the two surfaces
cannot drift on a shared chart fact — where they are allowed to differ is in their painters, never
in their data.
*/
struct ChartViewState
{
    /*!
    \brief Number of strings the chart's tuning declares; zero means no chart is loaded.

    The CHART's count, never a display count: a surface pads to its own minimum when it lays out
    (\ref displayedStringCount), and this stays indexable into the tuning.
    */
    int string_count{0};

    /*!
    \brief Capo fret from the chart tuning; 0 means no capo.

    Carried so a surface can indicate the string floor: the chart stores absolute frets with 0
    meaning the capo'd open string, so without this nothing in the drawn content says a capo
    exists at all.
    */
    int capo{0};

    /*! \brief Sounding notes in ascending onset order. */
    std::vector<NoteViewState> notes;

    /*!
    \brief Per-note hold end in seconds — the 3D board's, one entry per \ref notes entry.

    How long a pinned head lasts: the note's presented end, except that a member of a two-or-more
    onset group under a covering hand-shape span whose presented tail is empty is held for its
    ACTUAL ring, capped at the span's end and at its own string's next onset — the strum's heads
    stay pinned at the hit line while the posture is held, instead of vanishing the instant it is
    struck. A fully dead group is choked rather than held and keeps its own end.

    **The 2D lane does not read this.** It draws, lays out, hit-tests and culls by each note's
    presented tail (\ref NoteViewState::end_seconds) alone, so the ribbons under sub-quarter chugs
    are simply absent there — the chord box over the strum already states how long the posture is
    fretted, and a ribbon repeating that used the one mark that means "this string is still
    ringing" to say something else. The board has no chord box, so pinning the heads is how it
    states the same fact (ruling 3 of `docs/plans/in-progress/note-sustain-model.md`). One chart,
    one hold, two idioms.

    Resolved here from \ref chartHolds, the ONE authority for that rule, rather than recomputed in
    seconds: it used to be computed twice, once in beats for the chart rules and once in seconds
    for the board, and both copies carried the same defect — a long span shadowed by a short one
    that started inside it silently lost its hold — and were fixed separately. That is the whole
    argument for resolving the beats answer instead of restating it.

    Feeds the board's visible-range prefix maximum, so a pinned strum stays in range for as long
    as it is held.
    */
    std::vector<double> display_hold_ends;

    /*! \brief Hand-posture spans in ascending start order. */
    std::vector<ShapeViewState> shapes;

    /*! \brief Fret-hand placements in ascending arrival order. */
    std::vector<FhpViewState> fret_hand_positions;

    /*!
    \brief Compares two chart view states by their stored fields.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartViewState& lhs, const ChartViewState& rhs) = default;
};

} // namespace rock_hero::common::core
