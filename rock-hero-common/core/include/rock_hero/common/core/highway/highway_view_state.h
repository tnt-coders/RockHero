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
#include <rock_hero/common/core/chart/chart_view_state.h>
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

A plain fret is never clamped here, and since 2026-08-20 never needs to be: \ref g_max_fret is the
drawn board's own 24, and \ref g_highway_fret_count derives from it, so a fret past the board is no
longer representable. Only a NODE can still lie past the last fret (a bridge-side harmonic), which
is why this function exists at all.

\param note Note whose sounding place is wanted.
\param fret_at_point Stop being labeled — the onset fret, or a slide waypoint's fret.
\return Where to draw, with a node held inside the board.
*/
[[nodiscard]] inline SoundingPosition highwayDrawnSoundingPosition(
    const NoteViewState& note, int fret_at_point)
{
    SoundingPosition sounding =
        soundingPositionAt(note.harmonic_node, note.attack, note.fret, fret_at_point);
    if (sounding.at_node)
    {
        sounding.position = std::min(sounding.position, static_cast<double>(g_highway_fret_count));
    }
    return sounding;
}

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
struct HighwayTapOnsetViewState
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
    friend bool operator==(
        const HighwayTapOnsetViewState& lhs, const HighwayTapOnsetViewState& rhs) noexcept
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
struct HighwayChordGroupViewState
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

    /*!
    \brief The strum's own emphasis, for the box that STANDS IN for its heads.

    A repeat box draws no note heads (\ref box_only), so the box is the only surface left to
    carry the group's dynamics; every other box draws over heads that state their own, and
    restating it there would be the same claim in two places.

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

    A dead chug restating the preceding chord hides behind the repeat box, and dead runs never
    break another chord's repeat chain. Unanimous like \ref all_palm_muted and independent of it.
    */
    bool all_dead{false};

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
        const HighwayChordGroupViewState& lhs, const HighwayChordGroupViewState& rhs) noexcept
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) && lhs.first == rhs.first &&
               lhs.count == rhs.count && lhs.fretting_hand_count == rhs.fretting_hand_count &&
               lhs.emphasis == rhs.emphasis && lhs.all_palm_muted == rhs.all_palm_muted &&
               lhs.all_dead == rhs.all_dead && lhs.box_only == rhs.box_only &&
               std::is_eq(lhs.hold_cap_seconds <=> rhs.hold_cap_seconds);
    }
};

/*!
\brief Whether an onset group is a struck CHORD — the one rule the chord box is drawn by.

Two or more fretting-hand members. Stated here beside the count it reads rather than at the sites
that ask, because the answer is not the box's alone any more: the arpeggio bracket looks for the
struck group and the strike glow lights a boxed cluster's window edges instead of its fret lines.
A mark that deferred to the box — "the box already states this strum's held duration, so do not
repeat it" — would have to ask this too rather than filter on a count of its own, or the two could
disagree about the same strum.

\param fretting_hand_count Members struck by the fretting hand
       (\ref HighwayChordGroupViewState::fretting_hand_count).
\return True when the group draws a plain chord box.
*/
[[nodiscard]] constexpr bool highwayChordBoxApplies(std::size_t fretting_hand_count) noexcept
{
    return fretting_hand_count >= 2;
}

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
    \brief Tapping-hand onsets in ascending order, derived from the notes' picking-hand-at-the-neck
    attacks — taps AND pick slides, per \ref rightHandOnset.

    Right-hand presentation is derived, never authored (the right-hand-tap-lighting plan): these
    feed the per-tap light envelopes and the tapped chord boxes, and carry no user-editable data.
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
[[nodiscard]] inline std::vector<HighwayTapOnsetViewState> makeHighwayTapOnsets(
    const std::vector<NoteViewState>& notes, const std::vector<double>& note_rise_seconds)
{
    // A member's hand position at an instant: its own fret before any glide, linear between
    // its path stops, and the last station afterwards. A trail-off's unpitched terminal is
    // a release and never moves the light; a scrape's unpitched stops ARE the hand's
    // travel.
    const auto member_fret_at = [](const NoteViewState& note, const double seconds) {
        const bool scrape = isScrape(note.attack);
        double previous_seconds = note.start_seconds;
        // Where the note SOUNDS, not its stop: a tap harmonic strikes its node, and on an open
        // string that node is the only position it has. Waypoints ride the same rule, since a node
        // travels with the stop it rides. The DRAWN position, so a station chain cannot walk off
        // the board while the head it belongs to is held at the edge.
        double previous_fret = highwayDrawnSoundingPosition(note, note.fret).position;
        for (std::size_t index = 0; index < glideStopCount(note); ++index)
        {
            const GlideStop stop = glideStopAt(note, index);
            if ((stop.unpitched && !scrape) || stop.fret <= 0)
            {
                continue;
            }
            // The station is the stop's DRAWN sounding position, exactly like the seed above:
            // a node rides the stop it glides with, so a tapped harmonic's light walks the node
            // path, not the stop path underneath it. Identity for a node-less note.
            const double stop_position = highwayDrawnSoundingPosition(note, stop.fret).position;
            if (seconds <= stop.seconds)
            {
                const double span = stop.seconds - previous_seconds;
                const double weight =
                    span > 0.0 ? std::clamp((seconds - previous_seconds) / span, 0.0, 1.0) : 1.0;
                return previous_fret + ((stop_position - previous_fret) * weight);
            }
            previous_seconds = stop.seconds;
            previous_fret = stop_position;
        }
        return previous_fret;
    };
    // When the member's hand leaves: the last pitched waypoint when an unpitched trail-off
    // follows (the release is already underway), otherwise the sustain end — which for a
    // scrape is the path's end, where the pick lifts.
    const auto member_release_at = [](const NoteViewState& note) {
        if (!isScrape(note.attack) && note.slide_out.has_value())
        {
            double last_pitched = note.start_seconds;
            for (const SlideViewState& waypoint : note.slides)
            {
                if (waypoint.fret > 0)
                {
                    last_pitched = waypoint.seconds;
                }
            }
            return last_pitched;
        }
        return std::max(note.end_seconds, note.start_seconds);
    };

    std::vector<HighwayTapOnsetViewState> onsets;
    std::vector<const NoteViewState*> taps;
    std::vector<double> station_times;
    std::vector<double> scrape_times;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double onset = notes[index].start_seconds;
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - onset) < g_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayTapOnsetViewState view{.seconds = onset, .path = {}};
        taps.clear();
        for (std::size_t member = index; member < group_end; ++member)
        {
            const NoteViewState& note = notes[member];
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
            for (const NoteViewState* const tap : taps)
            {
                hold_end = std::max(hold_end, member_release_at(*tap));
                for (std::size_t stop_index = 0; stop_index < glideStopCount(*tap); ++stop_index)
                {
                    const GlideStop stop = glideStopAt(*tap, stop_index);
                    if ((!stop.unpitched || isScrape(tap->attack)) && stop.fret > 0)
                    {
                        station_times.push_back(stop.seconds);
                        if (isScrape(tap->attack))
                        {
                            scrape_times.push_back(stop.seconds);
                        }
                    }
                }
            }
            station_times.push_back(hold_end);
            std::ranges::sort(station_times);
            for (const double seconds : station_times)
            {
                if (!view.path.empty() &&
                    seconds - view.path.back().seconds < g_onset_match_epsilon)
                {
                    continue;
                }
                const double first_fret = member_fret_at(*taps.front(), seconds);
                HighwayTapLightStation station{
                    .seconds = seconds,
                    .fret_low = first_fret,
                    .fret_high = first_fret,
                    .unpitched = std::ranges::any_of(scrape_times, [&](const double time) {
                        return std::abs(time - seconds) < g_onset_match_epsilon;
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
fully dead strum never shows notes, and a sustained or technique-bearing strum always does. The
take-over cap is resolved over the whole song, which is what makes it stable: each group's
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

    // Sorted (string, fret) pairs for matching a strum against a shape's posture. Scratch for the
    // classification only — no consumer reads them once box_only is decided, so they are not
    // carried on the view.
    std::vector<std::vector<std::pair<int, int>>> group_frets;

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
            .box_only = false,
            .hold_cap_seconds = std::numeric_limits<double>::infinity(),
        };
        std::vector<std::pair<int, int>> frets;
        frets.reserve(group.count);
        // Quiet is the unanimous claim, so it starts true and any non-ghost member clears it;
        // loud is the existential one and starts false. Both fold in the same pass below, as do
        // the two mute unanimities, which are unanimous claims of the same shape.
        bool all_ghosted = true;
        for (std::size_t member = index; member < group_end; ++member)
        {
            const NoteViewState& note = notes[member];
            // A silently-held stop is not part of the STRUM: it counts toward no chord box, has no
            // dynamics or mute state to fold into the group's unanimities, and states no fret the
            // repeat rule could match a posture against. It keeps its group index — every note
            // needs one — and contributes nothing else.
            if (silentHold(note.attack))
            {
                grouping.note_group[member] = grouping.groups.size();
                continue;
            }
            if (!rightHandOnset(note.attack))
            {
                ++group.fretting_hand_count;
            }
            if (isAccented(note.emphasis))
            {
                group.emphasis = NoteEmphasis::Accent;
            }
            all_ghosted = all_ghosted && isGhosted(note.emphasis);
            group.all_palm_muted = group.all_palm_muted && note.palm_mute;
            group.all_dead = group.all_dead && note.dead;
            frets.emplace_back(note.string, note.fret);
            grouping.note_group[member] = grouping.groups.size();
        }
        // Loud wins a mixed strum, matching the note-level tie-break: one struck accent makes the
        // strum accented, where a lone ghost among normal notes does not make it quiet.
        if (all_ghosted && group.emphasis != NoteEmphasis::Accent)
        {
            group.emphasis = NoteEmphasis::Ghost;
        }
        std::ranges::sort(frets);
        group_frets.push_back(std::move(frets));
        grouping.groups.push_back(group);
        index = group_end;
    }

    const auto posture_matches = [](const ShapeViewState& shape,
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
        HighwayChordGroupViewState& group = grouping.groups[group_index];
        if (group.count < 2)
        {
            continue;
        }
        bool has_tails = false;
        bool any_marks = false;
        for (std::size_t member = group.first; member < group.first + group.count; ++member)
        {
            const NoteViewState& note = notes[member];
            has_tails = has_tails || note.end_seconds > note.start_seconds ||
                        !note.vibrato.empty() || note.tremolo || !note.bend.empty() ||
                        glideStopCount(note) > 0;
            // What is DRAWN, not what is stored: inside the connection family the mark is the
            // note's RESOLVED motion, so a claim nothing justifies carries no mark and must not
            // hold the repeat box off — it is pixel-identical to the plain pick beside it. Every
            // other attack draws a mark of its own.
            const bool attack_marks =
                !legatoClaimable(note.attack) || note.legato != LegatoMotion::Unjustified;
            any_marks = any_marks || note.harmonic_node.has_value() || attack_marks ||
                        isMuted(note.palm_mute, note.dead);
        }
        if (has_tails)
        {
            continue;
        }
        if (group.all_dead)
        {
            // A dead chug earns the X repeat box only when it restates the nearest preceding
            // chord's posture (muted or not); with fresh frets it displays its notes and their
            // mute crosses like any chord (Charter blanks every dead chug).
            std::size_t cursor = group.first;
            while (cursor > 0)
            {
                const double onset = notes[cursor - 1].start_seconds;
                std::size_t run_begin = cursor - 1;
                while (run_begin > 0 &&
                       std::abs(notes[run_begin - 1].start_seconds - onset) < g_onset_match_epsilon)
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
        if (any_marks && !group.all_palm_muted)
        {
            continue;
        }
        const ShapeViewState* shape = nullptr;
        for (const ShapeViewState& candidate : shapes)
        {
            // Tolerance so a shape starting on the same grid position as the chord (resolved a
            // rounding epsilon later) is still selected rather than skipped.
            if (candidate.start_seconds > group.start_seconds + g_onset_match_epsilon)
            {
                break;
            }
            shape = &candidate;
        }
        // A chord onset at (or within rounding of) the shape's end is still under the span — a
        // strict comparison here once dropped the handshape's last strum from repeat treatment.
        if (shape == nullptr || group.start_seconds > shape->end_seconds + g_onset_match_epsilon ||
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
            if (onset < shape->start_seconds - g_onset_match_epsilon)
            {
                break;
            }
            std::size_t run_begin = cursor - 1;
            while (run_begin > 0 &&
                   std::abs(notes[run_begin - 1].start_seconds - onset) < g_onset_match_epsilon)
            {
                --run_begin;
            }
            const std::size_t run_count = cursor - run_begin;
            if (run_count >= 2)
            {
                bool run_all_dead = true;
                std::vector<std::pair<int, int>> run_frets;
                run_frets.reserve(run_count);
                for (std::size_t member = run_begin; member < cursor; ++member)
                {
                    run_all_dead = run_all_dead && notes[member].dead;
                    run_frets.emplace_back(notes[member].string, notes[member].fret);
                }
                if (!run_all_dead)
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
    for (HighwayChordGroupViewState& group : grouping.groups | std::views::reverse)
    {
        group.hold_cap_seconds = next_shown_onset;
        if (group.count >= 2 && !group.box_only && !group.all_dead)
        {
            next_shown_onset = group.start_seconds;
        }
    }

    return grouping;
}

/*!
\brief One unbroken run of natural-harmonic notes at a single node: the span where the fretting
finger stands on that node.

Derived once per chart revision by \ref makeHighwayNodeSeries. The first note of a series is the
one that STATES the node (user rule 2026-08-15: repeats inside an unbroken run stay unlabeled,
and a chord of naturals at one node is one statement), and the span also suppresses the
dotted-fret downbeat numbers on the node's own fret — two numbers in one slot muddy each other,
and the node's is the one with information.
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
