/*!
\file highway_view_state.h
\brief Seconds-resolved, camera-agnostic frame content for the 3D note highway.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Display-mapping flags applied as pure math the renderer never sees. */
struct HighwayDisplayOptions
{
    /*! \brief True to reflect the fret axis for left-handed display. */
    bool mirrored{false};

    /*! \brief True to stack the lowest-pitched string on top instead of the bottom. */
    bool invert_string_order{false};

    /*!
    \brief Minimum number of string lanes to display, padding the chart's own string count.

    The editor mirrors the 2D tab's "show at least N strings" setting so the 3D preview shows the
    same lanes; when this exceeds the chart's string count the extra empty lanes appear and every
    note/posture string index is shifted into the padded lane range (see makeHighwayViewState).
    Zero (the game default) leaves the chart's string count untouched.
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

/*! \brief One bend curve point resolved to an absolute timeline second. */
struct HighwayBendPointView
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
        const HighwayBendPointView& lhs, const HighwayBendPointView& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on a
        // floating member, which is why every float-bearing view here is spelled out. Exact
        // equality is intended; the ordering query expresses it warning-free with identical
        // semantics (NaN compares unequal either way).
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*! \brief One slide waypoint resolved to an absolute timeline second. */
struct HighwaySlideView
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this waypoint. */
    int fret{0};

    /*!
    \brief True when this waypoint is unpitched travel rather than a pitched arrival.

    NOT "the glide trails off here" — see \ref TabSlideView::unpitched, which carries the same rule
    for the 2D lane and drifted into the same wrong wording. A pick slide's every waypoint carries
    this, because a scrape's whole path is unpitched travel.
    */
    bool unpitched{false};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(
        const HighwaySlideView& lhs, const HighwaySlideView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.unpitched == rhs.unpitched;
    }
};

/*! \brief One sounding note resolved to timeline seconds for highway rendering. */
struct HighwayNoteView
{
    /*! \brief Absolute onset position. */
    double start_seconds{0.0};

    /*! \brief Absolute end of the sustain; equals start_seconds when there is no sustain. */
    double end_seconds{0.0};

    /*!
    \brief One-based displayed string lane, counted from the lowest-pitched lane.

    Equals the chart string when no display padding applies (HighwayDisplayOptions::
    minimum_string_count of zero, the game default); when the projection pads to a larger
    displayed lane count it is the chart string shifted into the padded range.
    */
    int string{1};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*!
    \brief What this note's connection claim resolves to (\ref resolveLegato).

    The chart stores a claim and never a direction, so the drawn hammer-on or pull-off cell can only
    come from here. `Unjustified` is not a state of its own on the board: a claim nothing justifies
    draws exactly what the plain pick beside it draws, which is why no cue exists and why every
    attack outside \ref legatoClaimable leaves this at its default.
    */
    LegatoMotion legato{LegatoMotion::Unjustified};

    /*! \brief Muting applied to the note. */
    NoteMute mute{NoteMute::None};

    /*!
    \brief Harmonic node in fret units, and the assertion that this note is a harmonic.

    Mirrors `ChartNote::harmonic_node`, carrying the chart's exact node point (the 3.2 / 2.7 /
    5.8 family) so the highway places the harmonic head at the true node instead of the fret
    middle. Every harmonic carries one, a pinch included (rule-enforced) — but a pinch's node
    lies off the neck where the thumb grazes, so ask `nodeIsOnNeck` before anchoring to it; a
    pinch draws at its fret.
    */
    std::optional<double> harmonic_node{};

    /*! \brief True when the note is played with vibrato. */
    bool vibrato{false};

    /*! \brief True when the note is tremolo picked. */
    bool tremolo{false};

    /*! \brief True when the note is accented. */
    bool accent{false};

    /*! \brief Bend curve points in ascending time order; empty when not bent. */
    std::vector<HighwayBendPointView> bend;

    /*! \brief Slide waypoints in ascending time order; empty when the note does not slide. */
    std::vector<HighwaySlideView> slides;

    /*!
    \brief Compares two note views by their stored fields.
    \param lhs Left-hand note view.
    \param rhs Right-hand note view.
    \return True when both views store equal values.
    */
    friend bool operator==(const HighwayNoteView& lhs, const HighwayNoteView& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.string == rhs.string &&
               lhs.fret == rhs.fret && lhs.attack == rhs.attack && lhs.legato == rhs.legato &&
               lhs.mute == rhs.mute && lhs.harmonic_node == rhs.harmonic_node &&
               lhs.vibrato == rhs.vibrato && lhs.tremolo == rhs.tremolo &&
               lhs.accent == rhs.accent && lhs.bend == rhs.bend && lhs.slides == rhs.slides;
    }
};

/*!
\brief Where a note sounds on the DRAWN 3D board, in fret units.

\ref soundingPositionAt answers the chart question and is deliberately unbounded by the board: a
string has harmonic nodes past its last fret, so a node runs to \ref g_max_harmonic_node, which is
48 fret units. The drawn board lays out \ref g_highway_fret_count frets and has nowhere to put a
position past the last one, so this holds a node at the board's edge.

The cap is a DISPLAY limit and nothing else. The chart still carries the exact node, validation
still accepts it, and the 2D lane still prints it as a number, so the two surfaces deliberately
disagree about a node past the board: 2D names it, 3D draws the note at the last fret. That is a
decided asymmetry rather than a latent bug, and the decision it is pending is tracked — see
docs/plans/roadmap/57-positions-past-the-drawn-board.md, whose first question is a corpus
measurement that may close it by shrinking the domain to the board instead.

Every 3D consumer must ask this rather than \ref soundingPositionAt, or the board and the camera
frame different places — which is exactly how a third-partial artificial harmonic came to be framed
at its stop while drawn at its node, entirely off screen.

NOT clamped here: a plain fret. Chart validation accepts frets to \ref g_max_fret, which is 30, so a
fret-25-and-up note already draws past a 24-fret board with or without a harmonic. That is the same
board-versus-domain mismatch and wants the same single decision, so it is left visible rather than
quietly clamped to a fret the chart did not ask for.

\param note Note whose sounding place is wanted.
\param fret_at_point Stop being labeled — the onset fret, or a slide waypoint's fret.
\return Where to draw, with a node held inside the board.
*/
[[nodiscard]] inline SoundingPosition highwayDrawnSoundingPosition(
    const HighwayNoteView& note, int fret_at_point)
{
    SoundingPosition sounding =
        soundingPositionAt(note.harmonic_node, note.attack, note.fret, fret_at_point);
    if (sounding.at_node)
    {
        sounding.position = std::min(sounding.position, static_cast<double>(g_highway_fret_count));
    }
    return sounding;
}

/*! \brief What the hand holds on one string under a shape span (fingering-panel data). */
struct HighwayShapeStringView
{
    /*! \brief One-based displayed string lane (padded like HighwayNoteView::string). */
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
        const HighwayShapeStringView& lhs, const HighwayShapeStringView& rhs) noexcept = default;
};

/*! \brief One hand-posture span resolved to timeline seconds for highway rendering. */
struct HighwayShapeView
{
    /*! \brief Absolute start of the span. */
    double start_seconds{0.0};

    /*! \brief Absolute end of the span. */
    double end_seconds{0.0};

    /*! \brief Chord template display name; may be empty for unnamed shapes. */
    std::string name;

    /*!
    \brief True when the span's notes arrive sequentially (arpeggio treatment) rather than
    together (chord box). Derived at projection time from the notes under the span start.
    */
    bool arpeggio{false};

    /*!
    \brief Posture entries from the shape's chord template, lowest string first; empty when the
    template is unknown. Drives the fingering panel and the arpeggio brackets.
    */
    std::vector<HighwayShapeStringView> strings;

    /*!
    \brief Compares two shape views by their stored fields.
    \param lhs Left-hand shape view.
    \param rhs Right-hand shape view.
    \return True when both views store equal values.
    */
    friend bool operator==(const HighwayShapeView& lhs, const HighwayShapeView& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.name == rhs.name &&
               lhs.arpeggio == rhs.arpeggio && lhs.strings == rhs.strings;
    }
};

/*! \brief One station along a tapping-hand light path: the tapped fret extent at an instant. */
struct HighwayTapLightStation
{
    /*! \brief Absolute position of this station. */
    double seconds{0.0};

    /*! \brief Lowest tapped fret at this instant; fractional mid-glide. */
    double fret_low{0.0};

    /*! \brief Highest tapped fret at this instant; fractional mid-glide. */
    double fret_high{0.0};

    /*!
    \brief True when the glide arriving at this station is unpitched pick travel.

    A scrape's waypoints move the picking hand with the unpitched slide ease, so the light
    renderer sweeps toward this station with that profile; tapped pitched glides keep the
    pitched ease. An onset station never arrives from a glide, so its flag is never read.
    */
    bool unpitched{false};

    /*!
    \brief Compares two stations by their stored fields.
    \param lhs Left-hand station.
    \param rhs Right-hand station.
    \return True when both stations store equal values.

    Exact field equality: stations are compared against values the projection produced, so is_eq
    keeps GCC's -Wfloat-equal satisfied that the exactness is intended. Callers checking an eased
    mid-glide station compare with a tolerance instead.
    */
    friend constexpr bool operator==(
        const HighwayTapLightStation& lhs, const HighwayTapLightStation& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.fret_low <=> rhs.fret_low) &&
               std::is_eq(lhs.fret_high <=> rhs.fret_high) && lhs.unpitched == rhs.unpitched;
    }
};

/*! \brief One tapping-hand onset (a lone tap or a tapped chord) derived from the notes. */
struct HighwayTapOnsetView
{
    /*! \brief Absolute onset position shared by the simultaneous taps. */
    double seconds{0.0};

    /*! \brief Lowest tapped fret at the onset. */
    int fret_low{0};

    /*! \brief Highest tapped fret at the onset. */
    int fret_high{0};

    /*! \brief Number of simultaneous taps; two or more render the tapped chord box. */
    int count{0};

    /*!
    \brief Light path from the onset through any pitched glides to the fingers' release.

    The first station sits at the onset with the onset extent; later stations land on the taps'
    pitched slide waypoints (the light morphs with the glide) — or, for a scrape, on every
    waypoint of the pick's travel, flagged unpitched — and on the hold end (sustained contact
    keeps the light on through the sustain). Unpitched trail-offs contribute nothing —
    pressure is already releasing, so the light decays from the last pitched station instead.
    Never empty; a sustainless tap has exactly one station.
    */
    std::vector<HighwayTapLightStation> path;

    /*!
    \brief Duration of the light's rise ending at \ref seconds.

    Derived at projection time with the fret-hand placements' own arrival rule rather than a
    fixed wall-clock rise: the minimum-sustain-distance margin at the onset's meter, shortened
    when the previous tap onset's release crowds closer than the margin so envelopes never reach
    backward through an earlier hold.
    */
    double ramp_seconds{0.0};

    /*!
    \brief Compares two tap-onset views by their stored fields.
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.

    Exact second equality for the same reason as the station's: these are compared against values
    the projection produced. The scalars are tested before the path so an unequal onset rejects
    without walking the station vector.
    */
    friend bool operator==(const HighwayTapOnsetView& lhs, const HighwayTapOnsetView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret_low == rhs.fret_low &&
               lhs.fret_high == rhs.fret_high && lhs.count == rhs.count &&
               std::is_eq(lhs.ramp_seconds <=> rhs.ramp_seconds) && lhs.path == rhs.path;
    }
};

/*!
\brief One onset group: the simultaneous notes of a strum, classified for display.

Membership decides the rolling flip and the shadow, two or more fretting-hand members earn the
plain chord box, and \ref box_only marks the repeat-chord treatment. Derived from the chart once
per revision by \ref makeHighwayChordGroups: the repeat rules look BACKWARD through the whole
note stream, so deriving them inside the renderer's visible window both re-ran them every frame
and could not see past the window's edge.
*/
struct HighwayChordGroupView
{
    /*! \brief Absolute onset second shared by the group's members. */
    double start_seconds{0.0};

    /*! \brief Index of the group's first note in the state's note stream. */
    std::size_t first{0};

    /*! \brief Number of simultaneous notes at the onset. */
    std::size_t count{0};

    /*!
    \brief Members struck by the fretting hand: only these decide the PLAIN chord box.

    Taps and pick slides are the other hand — a fretted note under a simultaneous right-hand
    onset is a single note, and a tapped dyad gets the tapped box from the tap onsets.
    */
    std::size_t fretting_hand_count{0};

    /*! \brief True when any member is accented. */
    bool any_accent{false};

    /*! \brief The mute every member shares, or None when the members disagree. */
    NoteMute common_mute{NoteMute::None};

    /*!
    \brief True when every member is fully muted.

    A dead chug restating the preceding chord hides behind the repeat box, and muted runs never
    break another chord's repeat chain.
    */
    bool all_full_muted{false};

    /*!
    \brief Repeat-chord treatment (Charter's visibility rules): the strum renders as a
           half-height box with its mute cross and NO note heads.
    */
    bool box_only{false};

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
        const HighwayChordGroupView& lhs, const HighwayChordGroupView& rhs) noexcept
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) && lhs.first == rhs.first &&
               lhs.count == rhs.count && lhs.fretting_hand_count == rhs.fretting_hand_count &&
               lhs.any_accent == rhs.any_accent && lhs.common_mute == rhs.common_mute &&
               lhs.all_full_muted == rhs.all_full_muted && lhs.box_only == rhs.box_only &&
               std::is_eq(lhs.hold_cap_seconds <=> rhs.hold_cap_seconds);
    }
};

/*! \brief The derived onset grouping: the groups, and each note's index into them. */
struct HighwayChordGrouping
{
    /*! \brief Onset groups in ascending time order. */
    std::vector<HighwayChordGroupView> groups;

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

/*! \brief One fret-hand position marker resolved to a timeline second. */
struct HighwayFhpView
{
    /*! \brief Absolute position the hand arrives at this placement. */
    double seconds{0.0};

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand. */
    int width{4};

    /*!
    \brief Duration of the eased approach ending at \ref seconds; zero arrives instantly.

    Derived at projection time: a placement landing exactly on a slide waypoint — pitched glide or
    unpitched trail-off end alike — ramps over that glide's own segment so the window travels with
    the drawn rail, and every other placement morphs over the minimum-sustain-distance margin
    (shortened when placements crowd closer than the ramp).
    */
    double ramp_seconds{0.0};

    /*!
    \brief True when \ref ramp_seconds spans an UNPITCHED glide, so the window eases with the
    unpitched release curve instead of the pitched one.

    The window follows whatever the rail draws, and the two families are different functions of
    progress (\ref highwaySlideEaseWeight). Easing every move with the pitched curve left the
    window and the rail sharing only their endpoints. Note the consequence: the unpitched curve
    arrives at full travel with nonzero slope, so the window stops abruptly at the release — which
    is exactly what the drawn rail does at the same instant.
    */
    bool unpitched_ramp{false};

    /*!
    \brief Compares two fret-hand position views by their stored fields.
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.
    */
    friend constexpr bool operator==(const HighwayFhpView& lhs, const HighwayFhpView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.width == rhs.width && std::is_eq(lhs.ramp_seconds <=> rhs.ramp_seconds) &&
               lhs.unpitched_ramp == rhs.unpitched_ramp;
    }
};

/*! \brief One beat bar on the board, resolved to a timeline second. */
struct HighwayBeatView
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
        const HighwayBeatView& lhs, const HighwayBeatView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               lhs.measure_downbeat == rhs.measure_downbeat;
    }
};

/*! \brief One section label resolved to a timeline second. */
struct HighwaySectionView
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
    friend bool operator==(const HighwaySectionView& lhs, const HighwaySectionView& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.name == rhs.name;
    }
};

/*!
\brief Seconds-resolved chart content for one arrangement's 3D highway.

Built once per chart and shared immutably by the game highway and the editor 3D preview:
positions are resolved through the tempo map at projection time so rendering never queries
musical positions per frame. The camera and every drawer are pure functions of this state plus
per-frame time.
*/
struct HighwayViewState
{
    /*!
    \brief Number of string lanes DISPLAYED; zero means no chart is loaded.

    Not the chart's own string count whenever display padding applies: this is
    \ref displayedStringCount over the chart's tuning and
    \ref HighwayDisplayOptions::minimum_string_count, so it can exceed the tuning's size and must
    NOT be used to index the tuning. \ref HighwayNoteView::string is shifted into this same padded
    range, which is what keeps the shared string-color palette anchored as the 2D lane anchors it.
    */
    int string_count{0};

    /*!
    \brief Capo fret from the chart tuning; 0 means no capo.

    Carried so the board can draw the capo and its dead zone: the chart stores absolute frets
    with 0 meaning the capo'd open string, so without this the neck below the capo looks like
    ordinary playable board.
    */
    int capo{0};

    /*! \brief Display-mapping flags the projection was built with. */
    HighwayDisplayOptions options{};

    /*! \brief Sounding notes in ascending onset order. */
    std::vector<HighwayNoteView> notes;

    /*! \brief Hand-posture spans in ascending start order. */
    std::vector<HighwayShapeView> shapes;

    /*!
    \brief Per-note display hold end in seconds, one entry per \ref notes entry.

    The note's own sustain end, except that a sustainless member of a two-or-more onset group under
    a covering hand-shape span is held for the span — a strum's heads stay pinned at the hit line
    while the posture is held, instead of vanishing the instant they are struck. A fully dead group
    is choked rather than held and keeps its own end.

    Resolved here from \ref chartEffectiveSustains, the ONE authority for that rule, rather than
    recomputed in seconds. It used to be computed twice, once in beats for the chart rules and once
    in seconds for the board, and both copies carried the same defect — a long span shadowed by a
    short one that started inside it silently lost its hold — and were fixed separately. That is the
    whole argument for resolving the beats answer instead of restating it.

    Feeds the visible-range prefix maximum, so a span-held strum stays in range for as long as it is
    drawn.
    */
    std::vector<double> display_hold_ends;

    /*!
    \brief Tapping-hand onsets in ascending order, derived from the notes' picking-hand-at-the-neck
    attacks — taps AND pick slides, per \ref rightHandOnset.

    Right-hand presentation is derived, never authored (the right-hand-tap-lighting plan): these
    feed the per-tap light envelopes and the tapped chord boxes, and carry no user-editable data.
    */
    std::vector<HighwayTapOnsetView> tap_onsets;

    /*!
    \brief Onset groups in ascending order, classified for chord-box and repeat treatment.

    Derived once per chart revision by \ref makeHighwayChordGroups; the renderer clamps these to
    its visible range instead of rebuilding and reclassifying them every frame.
    */
    std::vector<HighwayChordGroupView> chord_groups;

    /*! \brief Each note's index into \ref chord_groups, sized and ordered like \ref notes. */
    std::vector<std::size_t> note_group;

    /*! \brief Fret-hand position markers in ascending order. */
    std::vector<HighwayFhpView> fret_hand_positions;

    /*! \brief Every beat of the song grid in ascending order, downbeats marked. */
    std::vector<HighwayBeatView> beats;

    /*! \brief Section labels in ascending order. */
    std::vector<HighwaySectionView> sections;

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
    chart yields at least beat 0, while an arrangement with no chart fills neither these nor
    \ref notes and \ref fret_hand_positions. Keep it that way — a state carrying content but no
    zone starts is not a supported shape, and nothing diagnoses it.
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
\brief Tolerance for matching an onset to another onset or a shape-span boundary.

A true rounding tolerance and nothing more: a nanosecond is four orders above the double rounding
error at song scale and six orders below the finest grid the editor offers (a 1/128 note is 15 ms at
120 BPM), so it can only ever absorb arithmetic noise, never join two musically distinct events.

It was 1e-4 s, on the stated grounds that a note onset and a shape boundary resolve through
different tempo-map paths and so land a rounding epsilon apart. They do not: the forward cursor is
documented as returning bit-identical results and computes the same expression against the same
anchor span as the plain resolver, so equal grid positions resolve to equal seconds. The oversized
value was the sole reason the display's simultaneity rule could group notes at DISTINCT musical
positions that the chart-side rule (chartEffectiveSustains) refuses — a divergence
`grid_arithmetic.h` recorded as deliberate. With the tolerance honest, the two rules agree.
*/
inline constexpr double g_highway_onset_match_epsilon = 1.0e-9;

/*!
\brief Derives the picking-hand onsets: one entry per onset group with taps or pick slides.

Right-hand presentation is derived, never authored: each entry carries the fret extent and
count of the right-hand notes struck together at that onset — feeding, for two or more
simultaneous taps, the tapped chord box — plus the light path the envelope follows: from the
onset through the hand's travel (a tap's pitched glides, or a scrape's whole waypoint path —
the light rides the slide either way) to the release: the sustain end for held contact and for
scrapes (the pick leaves at the path's end), or the last pitched station when an unpitched
trail-off is already releasing pressure. Fretting-hand notes sharing the onset contribute
nothing. Notes are judged on where they SOUND, not on `fret`: an open-string tap harmonic strikes
its node, and reading `fret` instead dropped the light from a note the rules explicitly allow.
A sounding place at or below the nut is skipped, and one past the last fret is held at the board's
edge by \ref highwayDrawnSoundingPosition, so a malformed chart cannot place a light off the board
at either end.

Each onset also carries a light-rise ramp, derived with the fret-hand placements' own arrival
rule: the caller supplies each note's margin-based rise duration (the minimum-sustain-distance
margin at the note's meter, resolved to seconds — zero for non-tap notes), the onset takes the
widest member's, and crowding clamps the rise so it never reaches backward past the previous
tap onset's release.

\param notes Seconds-resolved notes sorted by start time.
\param note_rise_seconds Per-note margin rise duration in seconds, sized and ordered like notes.
\return Tap onsets in ascending time order, each with at least one path station.
*/
[[nodiscard]] inline std::vector<HighwayTapOnsetView> makeHighwayTapOnsets(
    const std::vector<HighwayNoteView>& notes, const std::vector<double>& note_rise_seconds)
{
    // A member's hand position at an instant: its own fret before any glide, linear between
    // its path waypoints, and the last station afterwards. A trail-off's unpitched terminal is
    // a release and never moves the light; a scrape's unpitched waypoints ARE the hand's
    // travel.
    const auto member_fret_at = [](const HighwayNoteView& note, const double seconds) {
        const bool scrape = note.attack == NoteAttack::PickSlide;
        double previous_seconds = note.start_seconds;
        // Where the note SOUNDS, not its stop: a tap harmonic strikes its node, and on an open
        // string that node is the only position it has. Waypoints ride the same rule, since a node
        // travels with the stop it rides. The DRAWN position, so a station chain cannot walk off
        // the board while the head it belongs to is held at the edge.
        double previous_fret = highwayDrawnSoundingPosition(note, note.fret).position;
        for (const HighwaySlideView& waypoint : note.slides)
        {
            if ((waypoint.unpitched && !scrape) || waypoint.fret <= 0)
            {
                continue;
            }
            // The station is the waypoint's DRAWN sounding position, exactly like the seed above:
            // a node rides the stop it glides with, so a tapped harmonic's light walks the node
            // path, not the stop path underneath it. Identity for a node-less note.
            const double waypoint_position =
                highwayDrawnSoundingPosition(note, waypoint.fret).position;
            if (seconds <= waypoint.seconds)
            {
                const double span = waypoint.seconds - previous_seconds;
                const double weight =
                    span > 0.0 ? std::clamp((seconds - previous_seconds) / span, 0.0, 1.0) : 1.0;
                return previous_fret + ((waypoint_position - previous_fret) * weight);
            }
            previous_seconds = waypoint.seconds;
            previous_fret = waypoint_position;
        }
        return previous_fret;
    };
    // When the member's hand leaves: the last pitched waypoint when an unpitched trail-off
    // follows (the release is already underway), otherwise the sustain end — which for a
    // scrape is the path's end, where the pick lifts.
    const auto member_release_at = [](const HighwayNoteView& note) {
        if (note.attack != NoteAttack::PickSlide && !note.slides.empty() &&
            note.slides.back().unpitched)
        {
            double last_pitched = note.start_seconds;
            for (const HighwaySlideView& waypoint : note.slides)
            {
                if (!waypoint.unpitched && waypoint.fret > 0)
                {
                    last_pitched = waypoint.seconds;
                }
            }
            return last_pitched;
        }
        return std::max(note.end_seconds, note.start_seconds);
    };

    std::vector<HighwayTapOnsetView> onsets;
    std::vector<const HighwayNoteView*> taps;
    std::vector<double> station_times;
    std::vector<double> scrape_times;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double onset = notes[index].start_seconds;
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - onset) < g_highway_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayTapOnsetView view{.seconds = onset, .path = {}};
        taps.clear();
        for (std::size_t member = index; member < group_end; ++member)
        {
            const HighwayNoteView& note = notes[member];
            // Judged on where the note SOUNDS, so an open-string tap HARMONIC lights its node. The
            // guard exists to keep a malformed chart from putting a light off the board, and the
            // sounding position is what has to be on the board — reading `fret` instead dropped the
            // light from a note the rules explicitly allow, since E4 accepts a tap that strikes a
            // node in place of a fret. Asking for the DRAWN position closes the other end of that
            // guard: the zero test below catches a light below the nut, and the board cap catches
            // one past the last fret, which a node legally can be.
            const SoundingPosition sounding = highwayDrawnSoundingPosition(note, note.fret);
            // The integer fret CONTAINING the sounding place, since the light spans fret slots: a
            // node at 12.0 lies in fret 12, one at 2.669 in fret 3.
            const int sounding_fret =
                sounding.at_node ? static_cast<int>(std::ceil(sounding.position)) : note.fret;
            if (!rightHandOnset(note.attack) || sounding_fret <= 0)
            {
                continue;
            }
            view.fret_low =
                view.count == 0 ? sounding_fret : std::min(view.fret_low, sounding_fret);
            view.fret_high = std::max(view.fret_high, sounding_fret);
            ++view.count;
            view.ramp_seconds = std::max(
                view.ramp_seconds,
                member < note_rise_seconds.size() ? note_rise_seconds[member] : 0.0);
            taps.push_back(&note);
        }
        if (!taps.empty())
        {
            // Path stations: the onset, every pitched waypoint, and the hold end, deduplicated;
            // the extent at each station spans every member's fret at that instant.
            double hold_end = onset;
            station_times.clear();
            scrape_times.clear();
            station_times.push_back(onset);
            for (const HighwayNoteView* const tap : taps)
            {
                hold_end = std::max(hold_end, member_release_at(*tap));
                for (const HighwaySlideView& waypoint : tap->slides)
                {
                    if ((!waypoint.unpitched || tap->attack == NoteAttack::PickSlide) &&
                        waypoint.fret > 0)
                    {
                        station_times.push_back(waypoint.seconds);
                        if (tap->attack == NoteAttack::PickSlide)
                        {
                            scrape_times.push_back(waypoint.seconds);
                        }
                    }
                }
            }
            station_times.push_back(hold_end);
            std::ranges::sort(station_times);
            for (const double seconds : station_times)
            {
                if (!view.path.empty() &&
                    seconds - view.path.back().seconds < g_highway_onset_match_epsilon)
                {
                    continue;
                }
                const double first_fret = member_fret_at(*taps.front(), seconds);
                HighwayTapLightStation station{
                    .seconds = seconds,
                    .fret_low = first_fret,
                    .fret_high = first_fret,
                    .unpitched = std::ranges::any_of(scrape_times, [&](const double time) {
                        return std::abs(time - seconds) < g_highway_onset_match_epsilon;
                    }),
                };
                for (std::size_t tap = 1; tap < taps.size(); ++tap)
                {
                    const double fret = member_fret_at(*taps[tap], seconds);
                    station.fret_low = std::min(station.fret_low, fret);
                    station.fret_high = std::max(station.fret_high, fret);
                }
                view.path.push_back(station);
            }
            // Crowding clamp, mirroring the fret-hand ramps: the rise never reaches backward
            // past the previous tap onset's release, so a dense run keeps its per-tap dips.
            if (!onsets.empty())
            {
                view.ramp_seconds = std::clamp(
                    view.ramp_seconds,
                    0.0,
                    std::max(0.0, onset - onsets.back().path.back().seconds));
            }
            onsets.push_back(std::move(view));
        }
        index = group_end;
    }
    return onsets;
}

/*!
\brief Groups simultaneous notes and classifies each group's chord-box and repeat treatment.

Pure over the seconds-resolved streams, so the fussiest display rules on the board — the repeat
chain, the dead-chug restatement, the span-hold take-over — live where tests can reach them
instead of inside the GPU path. The classification (Charter's chord visibility rules): a strum
shows only the half-height repeat box when it repeats the covering hand shape's own posture
within the shape span — single notes and dead chugs between strums do not break the chain, a
fully-muted strum never shows notes, and a sustained or technique-bearing strum always does. The
take-over cap is resolved over the whole song, which is what makes it stable: each group's
span-hold display ends at the next note-showing strum wherever that strum is, not merely within
whatever window a renderer happens to be drawing.

\param notes Seconds-resolved notes sorted by start time.
\param shapes Hand-shape spans in ascending order.
\return Groups in ascending onset order, plus each note's group index.
*/
[[nodiscard]] inline HighwayChordGrouping makeHighwayChordGroups(
    const std::vector<HighwayNoteView>& notes, const std::vector<HighwayShapeView>& shapes)
{
    HighwayChordGrouping grouping;
    grouping.note_group.assign(notes.size(), 0);

    // Sorted (string, fret) pairs for matching a strum against a shape's posture. Scratch for the
    // classification only — no consumer reads them once box_only is decided, so they are not
    // carried on the view.
    std::vector<std::vector<std::pair<int, int>>> group_frets;

    for (std::size_t index = 0; index < notes.size();)
    {
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - notes[index].start_seconds) <
                   g_highway_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayChordGroupView group{
            .start_seconds = notes[index].start_seconds,
            .first = index,
            .count = group_end - index,
            .fretting_hand_count = 0,
            .any_accent = false,
            .common_mute = notes[index].mute,
            .all_full_muted = true,
            .box_only = false,
            .hold_cap_seconds = std::numeric_limits<double>::infinity(),
        };
        std::vector<std::pair<int, int>> frets;
        frets.reserve(group.count);
        for (std::size_t member = index; member < group_end; ++member)
        {
            const HighwayNoteView& note = notes[member];
            if (!rightHandOnset(note.attack))
            {
                ++group.fretting_hand_count;
            }
            group.any_accent = group.any_accent || note.accent;
            if (note.mute != group.common_mute)
            {
                group.common_mute = NoteMute::None;
            }
            group.all_full_muted = group.all_full_muted && note.mute == NoteMute::Full;
            frets.emplace_back(note.string, note.fret);
            grouping.note_group[member] = grouping.groups.size();
        }
        std::ranges::sort(frets);
        group_frets.push_back(std::move(frets));
        grouping.groups.push_back(group);
        index = group_end;
    }

    const auto posture_matches = [](const HighwayShapeView& shape,
                                    const std::vector<std::pair<int, int>>& frets) {
        if (shape.strings.empty() || shape.strings.size() != frets.size())
        {
            return false;
        }
        for (std::size_t entry = 0; entry < frets.size(); ++entry)
        {
            // Posture entries ascend by string (projection order), like the sorted pairs.
            if (shape.strings[entry].string != frets[entry].first ||
                shape.strings[entry].fret != frets[entry].second)
            {
                return false;
            }
        }
        return true;
    };
    for (std::size_t group_index = 0; group_index < grouping.groups.size(); ++group_index)
    {
        HighwayChordGroupView& group = grouping.groups[group_index];
        if (group.count < 2)
        {
            continue;
        }
        bool has_tails = false;
        bool all_palm_muted = true;
        bool any_marks = false;
        for (std::size_t member = group.first; member < group.first + group.count; ++member)
        {
            const HighwayNoteView& note = notes[member];
            has_tails = has_tails || note.end_seconds > note.start_seconds || note.vibrato ||
                        note.tremolo || !note.bend.empty() || !note.slides.empty();
            all_palm_muted = all_palm_muted && note.mute == NoteMute::Palm;
            // What is DRAWN, not what is stored: inside the connection family the mark is the
            // note's RESOLVED motion, so a claim nothing justifies carries no mark and must not
            // hold the repeat box off — it is pixel-identical to the plain pick beside it. Every
            // other attack draws a mark of its own.
            const bool attack_marks =
                !legatoClaimable(note.attack) || note.legato != LegatoMotion::Unjustified;
            any_marks = any_marks || note.harmonic_node.has_value() || attack_marks ||
                        note.mute != NoteMute::None;
        }
        if (has_tails)
        {
            continue;
        }
        if (group.all_full_muted)
        {
            // A dead chug earns the X repeat box only when it restates the nearest preceding
            // chord's posture (muted or not); with fresh frets it displays its notes and their
            // mute crosses like any chord (Charter blanks every dead chug).
            std::size_t cursor = group.first;
            while (cursor > 0)
            {
                const double onset = notes[cursor - 1].start_seconds;
                std::size_t run_begin = cursor - 1;
                while (run_begin > 0 && std::abs(notes[run_begin - 1].start_seconds - onset) <
                                            g_highway_onset_match_epsilon)
                {
                    --run_begin;
                }
                const std::size_t run_count = cursor - run_begin;
                if (run_count >= 2)
                {
                    std::vector<std::pair<int, int>> run_frets;
                    run_frets.reserve(run_count);
                    for (std::size_t member = run_begin; member < cursor; ++member)
                    {
                        run_frets.emplace_back(notes[member].string, notes[member].fret);
                    }
                    std::ranges::sort(run_frets);
                    group.box_only = run_frets == group_frets[group_index];
                    break;
                }
                cursor = run_begin;
            }
            continue;
        }
        // Marked chords always show their notes — unless every note is palm muted, where
        // Charter's mute short-circuit applies the repeat rule anyway.
        if (any_marks && !all_palm_muted)
        {
            continue;
        }
        const HighwayShapeView* shape = nullptr;
        for (const HighwayShapeView& candidate : shapes)
        {
            // Tolerance so a shape starting on the same grid position as the chord (resolved a
            // rounding epsilon later) is still selected rather than skipped.
            if (candidate.start_seconds > group.start_seconds + g_highway_onset_match_epsilon)
            {
                break;
            }
            shape = &candidate;
        }
        // A chord onset at (or within rounding of) the shape's end is still under the span — a
        // strict comparison here once dropped the handshape's last strum from repeat treatment.
        if (shape == nullptr ||
            group.start_seconds > shape->end_seconds + g_highway_onset_match_epsilon ||
            !posture_matches(*shape, group_frets[group_index]))
        {
            continue;
        }
        // Walk the note stream backward for the run that anchors the repeat chain. A predecessor
        // far behind the playhead must still anchor it, which is why this could never be derived
        // from a visible window alone.
        std::size_t cursor = group.first;
        while (cursor > 0)
        {
            const double onset = notes[cursor - 1].start_seconds;
            // Tolerance at the span start: the first strum of a repeat chain usually sits exactly
            // on the shape start, and a rounding epsilon below it would break the walk before it
            // finds the anchoring run — the classic cause of a repeat chord flickering to notes.
            if (onset < shape->start_seconds - g_highway_onset_match_epsilon)
            {
                break;
            }
            std::size_t run_begin = cursor - 1;
            while (run_begin > 0 && std::abs(notes[run_begin - 1].start_seconds - onset) <
                                        g_highway_onset_match_epsilon)
            {
                --run_begin;
            }
            const std::size_t run_count = cursor - run_begin;
            if (run_count >= 2)
            {
                bool run_all_full_muted = true;
                std::vector<std::pair<int, int>> run_frets;
                run_frets.reserve(run_count);
                for (std::size_t member = run_begin; member < cursor; ++member)
                {
                    run_all_full_muted = run_all_full_muted && notes[member].mute == NoteMute::Full;
                    run_frets.emplace_back(notes[member].string, notes[member].fret);
                }
                if (!run_all_full_muted)
                {
                    std::ranges::sort(run_frets);
                    group.box_only = posture_matches(*shape, run_frets);
                    break;
                }
            }
            cursor = run_begin;
        }
    }

    // Span-hold take-over: a span-held strum's heads stay pinned at the hit line until the next
    // strum that shows its notes arrives to re-pin the identical heads there, so the newcomer
    // owns the hold display from its onset and the two never stack. Box-only repeats, dead
    // chugs, and single notes continue the hold rather than taking it over, exactly as they
    // never break a repeat chain.
    double next_shown_onset = std::numeric_limits<double>::infinity();
    for (HighwayChordGroupView& group : grouping.groups | std::views::reverse)
    {
        group.hold_cap_seconds = next_shown_onset;
        if (group.count >= 2 && !group.box_only && !group.all_full_muted)
        {
            next_shown_onset = group.start_seconds;
        }
    }

    return grouping;
}

} // namespace rock_hero::common::core
