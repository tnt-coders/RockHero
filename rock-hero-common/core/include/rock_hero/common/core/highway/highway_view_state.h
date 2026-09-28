/*!
\file highway_view_state.h
\brief Seconds-resolved, camera-agnostic frame content for the 3D note highway.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Display-mapping flags the renderer applies while drawing, carried with the projected scene.

Every field here is read per frame in `highway_renderer.cpp` — the fret axis reflects, the lanes
stack, the padding resolves — so the chart scene underneath stays one chart fact and no consumer
projects a second, per-display copy of it. The cost of riding the memoized view state is that
changing one of these re-projects the chart, so a switch a viewer flips while WATCHING the board —
a diagnostic, a sighting rig — belongs on the renderer's own draw-time surface instead, never here.
*/
struct HighwayDisplayOptions
{
    /*! \brief True to reflect the fret axis for left-handed display. */
    bool mirrored{false};

    /*! \brief True to stack the lowest-pitched string on top instead of the bottom. */
    bool invert_string_order{false};

    /*!
    \brief Minimum number of string lanes to display, padding the chart's own string count.

    The editor mirrors the 2D tab's "show at least N strings" setting so the 3D preview shows the
    same lanes. Resolved by the renderer per frame through \ref displayedStringCount and
    \ref displayedLane — the same two functions the tab lane's geometry resolves it through — so
    the chart scene underneath stays one chart fact. Zero (the game default) adds no lanes.
    */
    int minimum_string_count{0};

    /*!
    \brief Compares two option sets by their stored fields.
    \param lhs Left-hand options.
    \param rhs Right-hand options.
    \return True when both option sets store equal values.
    */
    friend constexpr bool operator==(
        const HighwayDisplayOptions& lhs, const HighwayDisplayOptions& rhs) noexcept = default;
};

/*!
\brief A stop as the DRAWN 3D board places it: a node held inside the board's last fret.

\ref soundingStopAt answers the chart question and is deliberately unbounded by the board: a
string has harmonic nodes past its last fret, so a node runs to \ref g_max_harmonic_node, which is
48 fret units. The drawn board lays out \ref g_highway_fret_count frets and has nowhere to put a
position past the last one, so this holds a node at the board's edge.

The cap is a DISPLAY limit and nothing else. The chart still carries the exact node, validation
still accepts it, and the 2D lane still prints it as a number, so the two surfaces deliberately
disagree about a node past the board: 2D names it, 3D draws the note at the last fret. That is a
decided asymmetry rather than a latent bug, and the decision it is pending is tracked — see
docs/plans/roadmap/57-positions-past-the-drawn-board.md, whose first question is a corpus
measurement that may close it by shrinking the domain to the board instead.

Every 3D consumer must ask this rather than \ref soundingStopAt, or the board and the camera frame
different places: a third-partial artificial harmonic is then framed at its stop while drawn at
its node, entirely off screen.

A plain fret is never clamped here, and never needs to be: \ref g_max_fret is the drawn board's
own 24, and \ref g_highway_fret_count derives from it, so a fret past the board is not
representable. Only a NODE can lie past the last fret (a bridge-side harmonic), which is why this
function exists at all.

\param stop The stop as the chart states it.
\return The stop as the board draws it, with a node held inside the board.
*/
[[nodiscard]] inline ChartStop highwayDrawnStop(const ChartStop& stop)
{
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<double>& node = stop.node;
    if (node.has_value())
    {
        return nodeStop(std::min(*node, static_cast<double>(g_highway_fret_count)));
    }
    return stop;
}

/*!
\brief \ref highwayDrawnStop for where a projected NOTE sounds at a point in its travel.
\param note Note whose sounding place is wanted.
\param fret_at_point Stop being labeled — the onset fret, or a slide keyframe's fret.
\return Where to draw, with a node held inside the board.
*/
[[nodiscard]] inline ChartStop highwayDrawnStop(const NoteViewState& note, const int fret_at_point)
{
    return highwayDrawnStop(
        soundingStopAt(note.harmonic_node, note.attack, note.fret, fret_at_point));
}

/*!
\brief A stop's coordinate on the fret axis in fret units: a node's exact position, or the fret.

The fret-UNITS coordinate the fret axis is laid out in — a fret's own wire number, a node's exact
place — and what the hand-travel interpolation below works in. NOT a placement: \ref highwayStopX
is the world-X authority, and it puts a node on its wire but a fret at its slot's MIDPOINT, so
`highwayFretLineX(highwayStopPosition(stop))` is half a slot from `highwayStopX(stop)` for every
pressed fret. NOT \ref handFretOf's ceil either: that asks which integer fret CONTAINS the node,
for the hand window; this is the node itself.

\param stop Stop to read.
\return Fret units along the axis.
*/
[[nodiscard]] inline double highwayStopPosition(const ChartStop& stop)
{
    return stop.node.value_or(static_cast<double>(stop.fret));
}

/*!
\brief World X of a stop on the fret axis: a node on its own wire, a fret at its slot's midpoint.

The one placement authority for every 3D mark that sits on the fret axis — heads, slide paths,
floor numbers and posture brackets — rather than the slide path and the floor numbers each
spelling it for themselves. A fret axis takes a fractional coordinate directly, so a node needs no
rounding of any kind here.

\param stop Stop to place, as the board draws it (\ref highwayDrawnStop).
\param metrics Board metrics the fret axis is laid out by.
\param mirrored True when the board draws left-handed (world X reflected).
\return World X of the stop.
*/
[[nodiscard]] inline double highwayStopX(
    const ChartStop& stop, const HighwayMetrics& metrics, const bool mirrored)
{
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<double>& node = stop.node;
    if (node.has_value())
    {
        return highwayFretLineX(*node, metrics, mirrored);
    }
    return highwayNoteCenterX(stop.fret, metrics, mirrored);
}

/*!
\brief One arrival of a hand's window on the board: the extent the hand settles on, and the eased
approach ending there.

THE ONE MOTION ELEMENT BOTH HANDS SHARE, resolved by one rule (\ref highwayHandWindowAt): the
fretting hand's track (\ref HighwayViewState::fret_hand) is an arrival per placement, and the
picking hand's (\ref HighwayViewState::pick_hand) is an arrival per stop of the taps' travel.
Edges are fret-line coordinates (line 0 is the nut side of fret 1): a settled window covering
frets [fret, fret + width - 1] has edges fret - 1 and fret + width - 1.
*/
struct HighwayHandArrival
{
    /*! \brief Absolute position the hand arrives; arrivals ascend by this. */
    double seconds{0.0};

    /*! \brief Fret-line coordinate of the settled window's low-fret edge. */
    double low_line{0.0};

    /*! \brief Fret-line coordinate of the settled window's high-fret edge. */
    double high_line{4.0};

    /*!
    \brief Duration of the eased approach ending at \ref seconds; zero arrives instantly.

    A placement's is \ref FhpViewState::ramp_seconds. On the picking hand's track a strike's first
    arrival is instant, and every later arrival ramps over the leg from the arrival before it.
    */
    double ramp_seconds{0.0};

    /*!
    \brief True when the ramp spans an UNPITCHED glide (\ref FhpViewState::unpitched_ramp), so
    the approach eases with the unpitched curve instead of the pitched one.
    */
    bool unpitched_ramp{false};

    /*!
    \brief The final stretch of the ramp over which the approach settles into the arrival with a
    continuous slope; zero settles on the ramp's own curve.

    THE CROP ZONE: from the rail's ink end to the arrival. A leg the ink end cuts is drawn to the
    crop and no further while the hand completes at the true instant, so over this stretch the
    window leaves the leg's curve where the rail stops and comes to rest exactly at the arrival,
    in place of the curve's stop with slope. Zero where the rail reaches the arrival.
    */
    double settle_seconds{0.0};

    /*!
    \brief Compares two arrivals by their stored fields.
    \param lhs Left-hand arrival.
    \param rhs Right-hand arrival.
    \return True when both arrivals store equal values.

    Exact field equality: arrivals are compared against values the projection produced, so is_eq
    keeps GCC's -Wfloat-equal satisfied that the exactness is intended. Callers checking an eased
    mid-glide extent compare with a tolerance instead.
    */
    friend constexpr bool operator==(
        const HighwayHandArrival& lhs, const HighwayHandArrival& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.low_line <=> rhs.low_line) &&
               std::is_eq(lhs.high_line <=> rhs.high_line) &&
               std::is_eq(lhs.ramp_seconds <=> rhs.ramp_seconds) &&
               lhs.unpitched_ramp == rhs.unpitched_ramp &&
               std::is_eq(lhs.settle_seconds <=> rhs.settle_seconds);
    }
};

/*!
\brief One hand's light: WHERE its window stands over time, and WHEN it is lit.

The same pair for both hands (\ref HighwayViewState::fret_hand, \ref HighwayViewState::pick_hand),
so every floor layer draws either hand through one path and the hands differ only in the evidence
their producers accept. Where the light stands at an instant is \ref highwayHandWindowAt over
\ref track; how bright it is there is \ref highwayLightLevel over the stretch of \ref lit covering
that instant; what flashes on it is \ref pops.
*/
struct HighwayHandLight
{
    /*! \brief Where the hand's window stands, over time: arrivals ascending. */
    std::vector<HighwayHandArrival> track;

    /*!
    \brief When the light is lit: disjoint stretches in ascending order (\ref mergeLitEvidence),
    empty where the chart proves nothing about the hand.
    */
    std::vector<HighwayLitStretch> lit;

    /*! \brief The hand's strike-glow pops, ascending by onset, releases already clamped. */
    std::vector<HighwayStrikePop> pops;

    /*!
    \brief Compares two hand lights by their stored fields.
    \param lhs Left-hand light.
    \param rhs Right-hand light.
    \return True when both lights store equal values.

    Defaulted: each member is a vector of a type that hand-writes its own exact comparison, so no
    floating compare is spelled here.
    */
    friend bool operator==(const HighwayHandLight& lhs, const HighwayHandLight& rhs) = default;
};

/*!
\brief One tapping-hand onset (a lone tap or a tapped chord) derived from the notes: the STRIKE's
facts alone, for the tapped chord box and the strike pop.

Where the picking hand's light stands and when it is lit are not the strike's: they live on the
hand (\ref HighwayViewState::pick_hand), whose one track runs through every strike.
*/
struct HighwayTapOnsetViewState
{
    /*! \brief Absolute onset position shared by the simultaneous taps. */
    double start_seconds{0.0};

    /*! \brief Lowest tapped fret at the onset. */
    int fret_low{0};

    /*! \brief Highest tapped fret at the onset. */
    int fret_high{0};

    /*! \brief Number of simultaneous taps; two or more are a tapped chord (\ref tappedChord). */
    int count{0};

    /*!
    \brief The struck notes' DRAWN hold end: the latest member's ring end — the true ring rather
    than the drawn tail, so a tapped chord's rails run as long as the fretting hand's boxes hold —
    kept one margin clear of a head standing there (\ref drawnHoldExtent, the rule a posture span's
    rails are drawn by), so abutting holds show a gap. Whether the next strike repeats an
    established position is measured from here too (\ref g_hand_rest_seconds): a position stays
    established while its notes ring. The picking hand's LIGHT releases at the drawn end instead
    (\ref makePickHandLight), because the per-strike pulse is its look.
    */
    double release_seconds{0.0};

    /*!
    \brief Compares two tap-onset views by their stored fields.
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.

    Exact second equality for the same reason as the arrival's: these are compared against values
    the projection produced.
    */
    friend constexpr bool operator==(
        const HighwayTapOnsetViewState& lhs, const HighwayTapOnsetViewState& rhs) noexcept
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               lhs.fret_low == rhs.fret_low && lhs.fret_high == rhs.fret_high &&
               lhs.count == rhs.count && std::is_eq(lhs.release_seconds <=> rhs.release_seconds);
    }
};

/*!
\brief Returns whether a strike is a tapped chord: two or more taps struck together.

THE TAPPED-CHORD RULE: only a tapped chord draws the tapped box, its rails and its box-side pop; a
single tap pops its own slot's wires.

\param strike The strike to classify.
\return True for two or more simultaneous taps.
*/
[[nodiscard]] constexpr bool tappedChord(const HighwayTapOnsetViewState& strike) noexcept
{
    return strike.count >= 2;
}

/*!
\brief Returns whether the board marks a keyframe: a pitched linked keyframe within the note's ink
end.

An unpitched slide-out is a pressure release with no target to mark, a scrape's stops are the
PICKING hand's travel, and a keyframe past the ink end is in the ring's ending zone — the board
draws no reveal, so no mark of it appears. One predicate, asked by every consumer that draws or pops
a keyframe's fret, so none of them can disagree about which keyframes exist on the board.

\param note The note carrying the keyframe.
\param keyframe One of the note's keyframes.
\return True when the board draws a mark at the keyframe and pops its landing.
*/
[[nodiscard]] constexpr bool highwayMarksKeyframe(
    const NoteViewState& note, const SlideStopViewState& keyframe) noexcept
{
    return !isScrape(note.attack) && linkedKeyframe(keyframe) && keyframe.fret > 0 &&
           instantDrawn(keyframe.seconds, note.ink_end_seconds);
}

/*!
\brief Which box treatment one onset group draws — LAW IV's answer for the chord-box family.

Three states rather than two booleans, because they are one decision with one answer: a group that
draws no box cannot also be a repeat, and a repeat is not a second flag on top of a box but the
narrower of the two ways of drawing one. Spelling it as a sum makes the middle state — "a box, but
one that keeps its own heads" — a value with a name instead of the gap between two flags.
*/
enum class HighwayChordBoxTreatment : std::uint8_t
{
    /*!
    \brief No box: the group draws as plain note heads.

    Fewer than two fretting-hand members, and nothing else (LAW IV). A box marks SIMULTANEITY, so a
    lone note has none to mark — while a partial restrike inside a span is still two strings struck
    together and wears a box.
    */
    None,

    /*!
    \brief A full-height box over the group's own note heads: these strings were struck together.

    Every strum's default. It says simultaneity and the heads under it say which strings, which is
    why a partial restrike wears one honestly — and it wears THE STANDARD box, not a narrowed one:
    inside an arpeggio span the context is already carried by the span's borders and the brackets
    standing on the fretboard, so a box scoped to the struck strings would restate what nothing
    asked it to, and would look worse for it.
    */
    Full,

    /*!
    \brief The half-height REPEAT box, which draws no heads at all.

    The simile mark's idea, specialized and stricter: the same strings sounding at the same places
    struck again, immediately after the onset it repeats, inside one statement (\ref ChartStop —
    where each head SOUNDS, so a node chord and an open chord are two onsets however the fret
    column reads). It stands in for the heads it suppresses, so it carries the group's emphasis and
    its mute marks itself — which is what lets a profile CHANGE repeat rather than re-head.
    */
    Repeat,
};

/*!
\brief One onset group: the simultaneous notes of a strum, classified for display.

Membership decides the rolling flip and the shadow, and \ref box states which of the three chord-box
treatments the strum draws. Derived from the chart once per revision by \ref makeHighwayChordGroups:
the classification reads the derived hand-shape spans across the whole song, so deriving it inside
the renderer's visible window both re-ran it every frame and could not see past the window's edge.
*/
struct HighwayChordGroupViewState
{
    /*! \brief Absolute onset second shared by the group's members. */
    double start_seconds{0.0};

    /*! \brief Index of the group's first note in the state's note stream. */
    std::size_t first{0};

    /*! \brief Number of simultaneous notes at the onset. */
    std::size_t count{0};

    /*!
    \brief Members struck by the fretting hand: the precondition of every chord box.

    Taps and pick slides are the other hand — a fretted note under a simultaneous right-hand
    onset is a single note, and a tapped dyad gets the tapped box from the tap onsets. Two or more
    of these is what makes the group a STRUM at all; whether that strum then draws a box, and
    which, is \ref box.
    */
    std::size_t fretting_hand_count{0};

    /*!
    \brief The strum's own emphasis, for the box that STANDS IN for its heads.

    A repeat box draws no note heads (\ref HighwayChordBoxTreatment::Repeat), so the box is the
    only surface left to carry the group's dynamics; every other box draws over heads that state
    their own, and restating it there would be the same claim in two places.

    Summarized the way the axis reads out loud: ACCENTED when any member is, because one struck
    accent makes the strum an accented strum, and QUIET only when every member is ghosted,
    because a chord is not played softly while part of it is not.
    */
    NoteEmphasis emphasis{NoteEmphasis::Normal};

    /*!
    \brief True when EVERY member is palm muted.

    One commonality per mute flag rather than one shared mute value, because the flags are
    independent on the note and a group can be unanimous in one and split in the other. Unanimity
    is the only answer a box can use: a box speaks for its whole strum, so a mute only part of the
    strum carries says nothing the box could draw — one dead string inside a palm-muted chord
    leaves \ref all_dead false and the box still reads as the palm mute it is.

    Independent of \ref all_dead the whole way down: a strum unanimous in both wears BOTH box
    marks, stacked the way the note heads stack theirs, so nothing here picks between them.
    */
    bool all_palm_muted{false};

    /*!
    \brief True when EVERY member is dead.

    A dead chug's repeat box wears the X itself, which is why the treatment can suppress the heads
    without losing what they said. Unanimous like \ref all_palm_muted and independent of it.
    */
    bool all_dead{false};

    /*! \brief Which chord-box treatment this strum draws (\ref HighwayChordBoxTreatment). */
    HighwayChordBoxTreatment box_treatment{HighwayChordBoxTreatment::None};

    /*!
    \brief True when an ARPEGGIO span's opening mark draws at this onset.

    The OTHER box producer, published beside \ref box_treatment because "is a box standing over
    this onset" has two answers and a reader with only one of them draws the wrong picture: the
    strike glow lights a boxed cluster's window EDGES instead of its per-fret lines, and a lone
    note under a bracket would otherwise light its fret lines straight through the mark already
    standing over them.

    Answered here rather than off whatever boxes a frame happens to have built, and that is not a
    convenience: a renderer's box list is clamped to the visible board, while the glow reads
    clusters that have already passed the hit line, so the frame's list is silent about exactly the
    onsets whose glow is still fading.

    A mark and a strum can coincide, and then both are true — the coincidence rule suppresses the
    chord box and shows the arpeggio one, which changes WHICH box draws and never whether one does.
    */
    bool arpeggio_mark{false};

    /*!
    \brief Onset of the next note-showing strum, capping this group's span-hold display;
           infinity when none follows in the song.

    Whole-song on purpose. Derived over the visible window this was wrong at the window's edge:
    the last visible group's cap read as infinity even when a note-showing strum sat just past
    it, self-correcting only as that strum scrolled in.
    */
    double hold_cap_seconds{0.0};

    /*!
    \brief Compares two chord-group views by their stored fields.
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.

    Hand-written for the two own doubles (exact equality is right: these are compared against
    values the projection produced, and infinity caps compare equal to themselves).
    */
    friend bool operator==(
        const HighwayChordGroupViewState& lhs, const HighwayChordGroupViewState& rhs) noexcept
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) && lhs.first == rhs.first &&
               lhs.count == rhs.count && lhs.fretting_hand_count == rhs.fretting_hand_count &&
               lhs.emphasis == rhs.emphasis && lhs.all_palm_muted == rhs.all_palm_muted &&
               lhs.all_dead == rhs.all_dead && lhs.box_treatment == rhs.box_treatment &&
               lhs.arpeggio_mark == rhs.arpeggio_mark &&
               std::is_eq(lhs.hold_cap_seconds <=> rhs.hold_cap_seconds);
    }
};

/*! \brief The derived onset grouping: the groups, and each note's index into them. */
struct HighwayChordGrouping
{
    /*! \brief Onset groups in ascending time order. */
    std::vector<HighwayChordGroupViewState> groups;

    /*! \brief Each note's index into \ref groups, sized and ordered like the note stream. */
    std::vector<std::size_t> note_group;

    /*!
    \brief Compares two groupings by their stored fields.
    \param lhs Left-hand grouping.
    \param rhs Right-hand grouping.
    \return True when both groupings store equal values.
    */
    friend bool operator==(const HighwayChordGrouping& lhs, const HighwayChordGrouping& rhs) =
        default;
};

/*! \brief One beat bar on the board, resolved to a timeline second. */
struct HighwayBeatViewState
{
    /*! \brief Absolute position of the beat. */
    double seconds{0.0};

    /*! \brief True when the beat is a measure downbeat (drawn wider and brighter). */
    bool measure_downbeat{false};

    /*!
    \brief Compares two beat views by their stored fields.
    \param lhs Left-hand beat view.
    \param rhs Right-hand beat view.
    \return True when both views store equal values.
    */
    friend constexpr bool operator==(
        const HighwayBeatViewState& lhs, const HighwayBeatViewState& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               lhs.measure_downbeat == rhs.measure_downbeat;
    }
};

/*! \brief One section label resolved to a timeline second. */
struct HighwaySectionViewState
{
    /*! \brief Absolute position the section starts at. */
    double seconds{0.0};

    /*!
    \brief Section name upper-cased for the board, such as "VERSE" or "CHORUS".

    Display-ready on purpose. The highway draws every section name upper-cased, and folding the case
    here rather than in the renderer keeps a pure function of the chart out of the per-frame path,
    where it was allocating and transforming a fresh string for every visible section every frame.
    Only the 3D board reads this view, so the case fold cannot leak into the 2D ruler, which shows
    the authored name.
    */
    std::string name;

    /*!
    \brief Compares two section views by their stored fields.
    \param lhs Left-hand section view.
    \param rhs Right-hand section view.
    \return True when both views store equal values.
    */
    friend bool operator==(const HighwaySectionViewState& lhs, const HighwaySectionViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.name == rhs.name;
    }
};

/*!
\brief The 3D highway's frame content: the shared chart scene plus the board-only structure.

Built once per chart and shared immutably by the game highway and the editor 3D preview:
positions are resolved through the tempo map at projection time so rendering never queries
musical positions per frame. The camera and every drawer are pure functions of this state plus
per-frame time.
*/
struct HighwayViewState
{
    /*! \brief Display-mapping flags the renderer applies; the projection never reads them. */
    HighwayDisplayOptions options{};

    /*!
    \brief The chart scene both surfaces share (\ref makeChartViewState).

    Composed rather than restated so the two surfaces cannot drift on a shared chart fact. Its
    strings are CHART strings and its string count the tuning's: the displayed-lane padding
    \ref HighwayDisplayOptions::minimum_string_count asks for is resolved by the renderer per
    frame (\ref displayedLane), exactly as the tab lane resolves it in its geometry, which is what
    keeps the shared string-color palette anchored the same way on both surfaces.
    */
    ChartViewState chart;

    /*!
    \brief The fretting hand's light.

    Its track is an arrival per placement (\ref ChartViewState::fret_hand_positions): what the
    board's window motion reads — the window edges, the morph dim, the camera's framing — through
    the one resolver both hands share (\ref highwayHandWindowAt). The chart's placements stay the
    authority for what a placement IS (the 2D lane draws them, the board labels them). It is lit
    where the chart proves the hand holds something (\ref makeFretHandLight).
    */
    HighwayHandLight fret_hand;

    /*!
    \brief The picking hand's light (\ref makePickHandLight): the tap onsets' paths as one track,
    lit around each right-hand strike through the same merge the fretting hand's evidence takes.
    */
    HighwayHandLight pick_hand;

    /*!
    \brief Tapping-hand onsets in ascending order, derived from the notes' picking-hand-at-the-neck
    attacks — taps AND pick slides, per \ref rightHandOnset.

    Right-hand presentation is derived, never authored (the right-hand-tap-lighting plan): these
    feed the tapped chord boxes and the strike pops, and carry no user-editable data.
    */
    std::vector<HighwayTapOnsetViewState> tap_onsets;

    /*!
    \brief Onset groups in ascending order, classified for chord-box and repeat treatment.

    Derived once per chart revision by \ref makeHighwayChordGroups; the renderer clamps these to
    its visible range instead of rebuilding and reclassifying them every frame.
    */
    std::vector<HighwayChordGroupViewState> chord_groups;

    /*!
    \brief Each note's index into \ref chord_groups, sized and ordered like
    \ref ChartViewState::notes.
    */
    std::vector<std::size_t> note_group;

    /*! \brief Every beat of the song grid in ascending order, downbeats marked. */
    std::vector<HighwayBeatViewState> beats;

    /*! \brief Section labels in ascending order. */
    std::vector<HighwaySectionViewState> sections;

    /*!
    \brief Camera framing-zone start times in ascending order; each zone runs to the next start.

    Derived structure for the camera's framing window only — deliberately not musical phrases
    (the notation has none) and carrying no notation meaning. The projection groups measures the
    way a standard automatic phrase generator does: runs of measures containing note onsets
    split into fixed-size groups aligned to downbeats, runs of empty measures collapse into a
    single zone however long, and section starts force a new zone. The camera frames the current
    zone plus the next one, so its framing target steps only at these boundaries — the resting
    cadence that defines the camera's feel.

    Non-empty whenever there is anything to frame, and the camera depends on that: it has a
    single scan path, so an empty list reads as one unbounded zone and would frame the entire
    timeline at once. The invariant holds because zones derive from measure downbeats and any
    chart yields at least beat 0, while an arrangement with no chart fills neither these nor the
    \ref chart scene. Keep it that way — a state carrying content but no zone starts is not a
    supported shape, and nothing diagnoses it.
    */
    std::vector<double> camera_zone_starts;

    /*!
    \brief Compares two view states by their stored fields.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const HighwayViewState& lhs, const HighwayViewState& rhs) = default;
};

/*!
\brief Groups simultaneous notes and classifies each group's chord-box treatment.

Pure over the seconds-resolved streams, so the board's fussiest display rules live where tests can
reach them instead of inside the GPU path.

A BOX MARKS SIMULTANEITY (LAW IV): any two-or-more-string strike wears one, inside and outside spans
alike, and it is THE STANDARD box either way. That is the whole of whether a group is boxed at all —
the derivation is not asked, because "these were struck together" is a fact about the strike and
about nothing else. A single note stays boxless, and so does a fretted note under a simultaneous
right-hand onset: the tapping hand is not the strumming hand, and a tapped dyad gets its own box
from the tap onsets.

FULL OR REPEAT is the consecutiveness law: **an onset wears a repeat box iff it is identical to the
IMMEDIATELY PRECEDING onset, within the same span, with no onset of any kind between.** The onsets
ARE the groups in order, so "nothing between" needs no test — the immediately preceding onset is
simply the group before this one. Identity is the SAME STRUCK STRINGS at the SAME FRETS and nothing
more: the PROFILE is free, so a plain chord's first dead chug is an X'd REPEAT box wearing its own
mark rather than a re-head, which is what the repeat box already carries its own emphasis and mute
marks for. Everything else is a full box. Silence re-heads, because a rest is its own span boundary
and a span boundary breaks the run. A partial strike after a full chord is a different onset —
different notes — so it wears its own full box, and only an identical partial after THAT partial
repeats.

WHY THE SPAN STILL SCOPES IT, when the comparison is one onset against the one before it: two
identical chords with a genuine gap between them are two statements, and the derivation is what
knows that. The span boundary is the only thing that separates them, since the onsets themselves
compare equal.

WHAT THIS DELIBERATELY DOES NOT DO is ask the derivation whether a slot sounds its covering span's
shape WHOLE. Such a comparison would exist to keep a partial restrike from claiming a full
restatement, and the identity law above refuses that outright, because a repeat only ever follows
an IDENTICAL onset. Nor does anything here walk the note stream BACKWARD looking for a run to
anchor a chain on (an F10-style "singles and chugs don't break the chain" rule) — and no chain
state survives at all: the run's head is simply the onset whose predecessor differs.

EVERY QUESTION HERE IS ASKED OF THE FRETTING HAND'S MEMBERS ALONE: the count, the identity's places,
the mute and emphasis unanimities, and the capability gate's scans. A right-hand onset is the other
hand, so it is no part of the strike a box speaks for. Two figures a mixed reading gets wrong: a tap
over two identical chugs would make them different onsets and re-head the run, and a group of taps
alone would compare identical to its neighbour and draw a headless repeat box for a strum nobody
played. With the identity reading fretting content only, a REPLACED note — a chord one of whose
members becomes a tap — is a shrunk fretting set, which is a different onset and wears its own full
box, exactly as the exact string-set comparison says.

THE DISPLAY-CAPABILITY GATE is untouched otherwise, and it is the one thing here that is about
drawing rather than about the music: a repeat box has no heads, so it can only stand in for a strum
whose whole
statement it can draw itself — the mute profiles it wears a mark for, composed with the emphasis it
carries. Anything else falls back to the full box, which keeps its heads and therefore keeps every
mark on them. With the profile out of the identity the gate is what a mixed profile now meets, so it
is asked per group exactly as the tails a group happens to present are.

The take-over cap is resolved over the whole song, which is what makes it stable: each group's
span-hold display ends at the next note-showing strum wherever that strum is, not merely within
whatever window a renderer happens to be drawing.

\param notes Seconds-resolved notes sorted by start time.
\param shapes Hand-shape spans in ascending order.
\return Groups in ascending onset order, plus each note's group index.
*/
[[nodiscard]] inline HighwayChordGrouping makeHighwayChordGroups(
    const std::vector<NoteViewState>& notes, const std::vector<ShapeViewState>& shapes)
{
    HighwayChordGrouping grouping;
    grouping.note_group.assign(notes.size(), 0);

    // Sorted (string, stop) pairs — where each member's head SOUNDS — for the repeat identity
    // below. Scratch for the classification only: no consumer reads them once the treatment is
    // decided, so they are not carried on the view.
    std::vector<std::vector<std::pair<int, ChartStop>>> group_stops;

    for (std::size_t index = 0; index < notes.size();)
    {
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - notes[index].start_seconds) <
                   g_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayChordGroupViewState group{
            .start_seconds = notes[index].start_seconds,
            .first = index,
            .count = group_end - index,
            .fretting_hand_count = 0,
            .emphasis = NoteEmphasis::Normal,
            .all_palm_muted = true,
            .all_dead = true,
            .box_treatment = HighwayChordBoxTreatment::None,
            .arpeggio_mark = false,
            .hold_cap_seconds = std::numeric_limits<double>::infinity(),
        };
        std::vector<std::pair<int, ChartStop>> stops;
        stops.reserve(group.count);
        // Quiet is the unanimous claim, so it starts true and any non-ghost member clears it;
        // loud is the existential one and starts false. Both fold in the same pass below, as do
        // the two mute unanimities, which are unanimous claims of the same shape.
        bool all_ghosted = true;
        for (std::size_t member = index; member < group_end; ++member)
        {
            const NoteViewState& note = notes[member];
            // Every note needs a group index, whatever else it contributes.
            grouping.note_group[member] = grouping.groups.size();
            // THE STRUM IS THE FRETTING HAND'S, and this is the one place that is decided. A
            // right-hand onset is the OTHER hand's. It is no part of the strike a box speaks for,
            // so it does not count toward it, fold into its unanimities, carry its dynamics, or
            // state a fret its identity compares — and it is not scanned by the capability gate
            // below, which is the same question asked about the same members.
            //
            // Letting a tap in the group put its own fret into the repeat identity and its own
            // sustain into the gate is the failure the right-hand half prevents: two identical
            // chugs with a tap over them would read as DIFFERENT onsets and re-head, while a group
            // of taps alone would compare identical to the next and draw a headless repeat box for
            // a strum that never happened. The identity reads FRETTING CONTENT, so a shrunk
            // fretting set is simply a different onset and wears its own full box.
            if (rightHandOnset(note.attack))
            {
                continue;
            }
            ++group.fretting_hand_count;
            if (isAccented(note.emphasis))
            {
                group.emphasis = NoteEmphasis::Accent;
            }
            all_ghosted = all_ghosted && isGhosted(note.emphasis);
            group.all_palm_muted = group.all_palm_muted && note.palm_mute;
            group.all_dead = group.all_dead && note.dead;
            // WHERE THE HEADS SOUND, not the stored fret and not the grip: the box the identity
            // gates draws NO heads (the renderer skips every member of a repeat group), so two
            // onsets may only compare identical when the heads they replace are. Reading `fret`
            // made a node-12 chord compare identical to an open chord on the same strings, so
            // the open one following it drew a HEADLESS repeat box for a strum that never
            // repeated; reading the GRIP would leave the artificial half of the same defect live,
            // since a fret-5 head damped at node 17 is drawn twelve frets from the stop the hand
            // presses. The chart's own place, not the board-clamped one (\ref highwayDrawnStop):
            // the cap is a display limit and must not merge two onsets the board can tell apart.
            stops.emplace_back(
                note.string, soundingStopAt(note.harmonic_node, note.attack, note.fret, note.fret));
        }
        // Loud wins a mixed strum, matching the note-level tie-break: one struck accent makes the
        // strum accented, where a lone ghost among normal notes does not make it quiet.
        if (all_ghosted && group.emphasis != NoteEmphasis::Accent)
        {
            group.emphasis = NoteEmphasis::Ghost;
        }
        std::ranges::sort(stops);
        group_stops.push_back(std::move(stops));
        grouping.groups.push_back(group);
        index = group_end;
    }

    // THE REPEAT IDENTITY, in one place: the same struck strings at the same stops. The PROFILE is
    // deliberately absent — the profile is free, so a plain chord's first dead chug repeats wearing
    // its own X rather than re-heading, and the capability gate below is what catches a profile no
    // box can draw. The sorted (string, stop) pairs are the whole comparison, which is also why
    // they are built once per group above instead of being re-derived here.
    const auto same_onset = [&group_stops](const std::size_t lhs, const std::size_t rhs) {
        return group_stops[lhs] == group_stops[rhs];
    };
    // ONE forward cursor over the spans, replacing the backward walk over the notes. Both streams
    // ascend, so the span covering a group can only ever move forward. No chain state rides along:
    // the run's head is the onset whose predecessor differs, which the comparison above answers on
    // the spot.
    std::size_t next_shape = 0;
    std::size_t covering = shapes.size();
    // Which span the PREVIOUS onset lay in — the whole of "within the same span", and empty where
    // that onset lay in none. Updated for EVERY group, boxed or not, because "no onset of any kind
    // between" counts them all: a single note, a tap or a held slot between two identical chords
    // breaks the run exactly as a different chord would.
    std::optional<std::size_t> previous_span;
    for (std::size_t group_index = 0; group_index < grouping.groups.size(); ++group_index)
    {
        HighwayChordGroupViewState& group = grouping.groups[group_index];
        // Shapes ascend by start: consume every span standing at this onset, keeping the LAST. That
        // is the same span \ref common::core::SpanCoverage names by keeping the furthest-reaching
        // one, because spans never overlap — pinned by "Chart shape derivation never overlaps two
        // spans", so neither rule has to be widened to match. Tolerance because the first strum of
        // a run usually sits exactly ON the span start and a rounding epsilon below it would leave
        // the span unconsumed here — the classic cause of a repeat chord flickering to notes.
        while (next_shape < shapes.size() &&
               !(shapes[next_shape].start_seconds > group.start_seconds + g_onset_match_epsilon))
        {
            covering = next_shape;
            ++next_shape;
        }
        // The span this onset lies in, if any. A chord onset at (or within rounding of) the span's
        // end is still under it — a strict comparison here once dropped a handshape's last strum
        // from repeat treatment.
        const std::optional<std::size_t> lies_in =
            covering < shapes.size() && !(group.start_seconds > shapes[covering].drawn_end_seconds +
                                                                    g_onset_match_epsilon)
                ? std::optional<std::size_t>{covering}
                : std::nullopt;
        // Recorded before any of the early exits below, so an unboxed onset still breaks a run.
        const std::optional<std::size_t> previous = std::exchange(previous_span, lies_in);
        // The OTHER box producer (\ref HighwayChordGroupViewState::arpeggio_mark), answered here
        // because here is where the covering span is already in hand. An opening mark always falls
        // on an onset — a span an event states opens at its own slot, and a landing-opened one
        // defers to a sounding — so every mark that draws has a group to be published on, and the
        // question needs no walk of its own. Recorded before the early exits too, since a lone note
        // under a bracket is exactly the case a reader gets wrong.
        if (lies_in.has_value())
        {
            const ShapeViewState& span = shapes[*lies_in];
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<double>& mark = span.bracket_seconds;
            group.arpeggio_mark = span.arpeggio && mark.has_value() &&
                                  std::abs(*mark - group.start_seconds) < g_onset_match_epsilon;
        }
        // Two or more fretting-hand members is what makes the group a STRUM, and only a strum is
        // simultaneous. Taps are the other hand: a fretted note under a simultaneous right-hand
        // onset is a single note, and a tapped dyad gets the tapped box from the tap onsets.
        if (group.fretting_hand_count < 2)
        {
            continue;
        }
        // A BOX MARKS SIMULTANEITY, so every strum wears one over its own struck strings.
        group.box_treatment = HighwayChordBoxTreatment::Full;
        // THE CONSECUTIVENESS LAW: identical to the onset immediately before it, both inside the
        // SAME span. Two onsets in no span at all are two statements the derivation never joined,
        // so they never repeat however alike they look.
        if (group_index == 0 || !lies_in.has_value() || lies_in != previous ||
            !same_onset(group_index, group_index - 1))
        {
            continue;
        }
        bool has_tails = false;
        bool any_marks = false;
        for (std::size_t member = group.first; member < group.first + group.count; ++member)
        {
            const NoteViewState& note = notes[member];
            // The same members the strum is made of (above), for the same reason: the gate asks
            // whether a box can carry this STRUM's whole statement, and a tap's sustain is no part
            // of that statement. A tap draws its own head and its own tail whatever the strum below
            // it does, so letting one force a full box put heads back on a chug run for a sound the
            // other hand made.
            if (rightHandOnset(note.attack))
            {
                continue;
            }
            has_tails = has_tails || note.ink_end_seconds > note.start_seconds ||
                        !note.vibrato.empty() || note.tremolo || !note.bend.empty();
            // What is DRAWN, not what is stored: inside the connection family the mark is the
            // note's RESOLVED motion, so a claim nothing justifies carries no mark and must not
            // hold the repeat box off — it is pixel-identical to the plain pick beside it. Every
            // other attack draws a mark of its own.
            const bool attack_marks =
                !legatoClaimable(note.attack) || note.legato != LegatoMotion::Unjustified;
            any_marks = any_marks || note.harmonic_node.has_value() || attack_marks ||
                        isMuted(note.palm_mute, note.dead);
        }
        // THE DISPLAY-CAPABILITY GATE, and the whole of it. A repeat box draws no heads, so it may
        // only stand in for a strum whose entire statement the box itself can carry: the mute
        // profile it wears a mark for, and the emphasis it already carries. A strum presenting a
        // TAIL is out on the same grounds — a box is drawn at an instant and has nowhere to put a
        // sustain — and so is any other mark a head would have shown. Those fall back to the full
        // box, which keeps its heads and therefore loses nothing.
        if (has_tails)
        {
            continue;
        }
        // A dead chug wears its own X on the box, so the marks scan does not speak for it: every
        // dead note reads as marked, and the one mark it has is the one the box draws. The palm
        // short-circuit beside it is the same shape and is deliberately left as it stands.
        if (!group.all_dead && any_marks && !group.all_palm_muted)
        {
            continue;
        }
        group.box_treatment = HighwayChordBoxTreatment::Repeat;
    }

    // Span-hold take-over: a span-held strum's heads stay pinned at the hit line until the next
    // strum that shows its notes arrives to re-pin the identical heads there, so the newcomer
    // owns the hold display from its onset and the two never stack. Repeat boxes, dead chugs,
    // and single notes continue the hold rather than taking it over.
    double next_shown_onset = std::numeric_limits<double>::infinity();
    for (HighwayChordGroupViewState& group : grouping.groups | std::views::reverse)
    {
        group.hold_cap_seconds = next_shown_onset;
        if (group.count >= 2 && group.box_treatment != HighwayChordBoxTreatment::Repeat &&
            !group.all_dead)
        {
            next_shown_onset = group.start_seconds;
        }
    }

    return grouping;
}

/*!
\brief One unbroken run of natural-harmonic notes at a single node: the span where the fretting
finger stands on that node.

Derived once per chart revision by \ref makeHighwayNodeSeries. The first note of a series is the one
that STATES the node (repeats inside an unbroken run stay unlabeled, and a chord of naturals at one
node is one statement), and the span also suppresses the dotted-fret downbeat numbers on the node's
own fret — two numbers in one slot muddy each other, and the node's is the one with information.
*/
struct HighwayNodeSeries
{
    /*! \brief Integer fret slot the series claims (\ref fretFor of the establishing note). */
    int fret{0};

    /*! \brief The node the run stands on, in fractional-fret units. */
    double node{0.0};

    /*! \brief Onset of the establishing note. */
    double begin_seconds{0.0};

    /*! \brief Onset of the run's last repeat; equals \ref begin_seconds for a lone natural. */
    double end_seconds{0.0};
};

/*!
\brief Derives the natural-harmonic node series from the note stream.

A new node under the fretting finger establishes a series; a repeat of the same node in an
unbroken run of naturals extends it; any fretting-hand note that is NOT a natural breaks the
run, because the hand left the node. Picking-hand onsets are invisible to the series, exactly
as they are to posture derivation.

\param notes Seconds-resolved notes sorted by start time.

\return Series in ascending begin order. Series never overlap (one established node at a time)
        and their end times are likewise non-decreasing, so consumers can binary-search them by
        time.
*/
[[nodiscard]] inline std::vector<HighwayNodeSeries> makeHighwayNodeSeries(
    const std::vector<NoteViewState>& notes)
{
    std::vector<HighwayNodeSeries> series;
    std::optional<double> established_node;
    for (const NoteViewState& note : notes)
    {
        if (rightHandOnset(note.attack))
        {
            continue;
        }
        if (!frettingFingerOnNode(note.fret, note.harmonic_node, note.attack) ||
            !note.harmonic_node.has_value())
        {
            established_node.reset();
            continue;
        }
        if (established_node == note.harmonic_node)
        {
            series.back().end_seconds = note.start_seconds;
            continue;
        }
        established_node = note.harmonic_node;
        series.push_back(
            HighwayNodeSeries{
                .fret = fretFor(note),
                .node = *note.harmonic_node,
                .begin_seconds = note.start_seconds,
                .end_seconds = note.start_seconds,
            });
    }
    return series;
}

} // namespace rock_hero::common::core
