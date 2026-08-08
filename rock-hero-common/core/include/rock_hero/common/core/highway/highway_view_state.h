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
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart.h>
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
        const HighwayBendPointView& lhs, const HighwayBendPointView& rhs) noexcept = default;
};

/*! \brief One slide waypoint resolved to an absolute timeline second. */
struct HighwaySlideView
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this waypoint. */
    int fret{0};

    /*! \brief True when the glide trails off unpitched. */
    bool unpitched{false};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(
        const HighwaySlideView& lhs, const HighwaySlideView& rhs) noexcept = default;
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

    /*! \brief Muting applied to the note. */
    NoteMute mute{NoteMute::None};

    /*!
    \brief Harmonic node in fret units, and the assertion that this note is a harmonic.

    Mirrors `ChartNote::harmonic_node`, carrying the chart's exact node point (the 3.2 / 2.7 / 5.8 family)
    so the highway places the harmonic head at the true node instead of the fret middle. Ask
    `nodeIsOnNeck` before anchoring to it and `isHarmonic` to ask whether the note is
    one at all — a pinch is a harmonic whose node may be unrecorded, and is drawn at its fret
    because the thumb grazes over the body.
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
    friend bool operator==(const HighwayNoteView& lhs, const HighwayNoteView& rhs) = default;
};

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
    friend bool operator==(const HighwayShapeView& lhs, const HighwayShapeView& rhs) = default;
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
    friend constexpr bool operator==(
        const HighwayFhpView& lhs, const HighwayFhpView& rhs) noexcept = default;
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
        const HighwayBeatView& lhs, const HighwayBeatView& rhs) noexcept = default;
};

/*! \brief One section label resolved to a timeline second. */
struct HighwaySectionView
{
    /*! \brief Absolute position the section starts at. */
    double seconds{0.0};

    /*! \brief Free-form section name, such as "verse" or "chorus". */
    std::string name;

    /*!
    \brief Compares two section views by their stored fields.
    \param lhs Left-hand section view.
    \param rhs Right-hand section view.
    \return True when both views store equal values.
    */
    friend bool operator==(const HighwaySectionView& lhs, const HighwaySectionView& rhs) = default;
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
    /*! \brief Number of string lanes the chart uses; zero means no chart is loaded. */
    int string_count{0};

    /*! \brief Display-mapping flags the projection was built with. */
    HighwayDisplayOptions options{};

    /*! \brief Sounding notes in ascending onset order. */
    std::vector<HighwayNoteView> notes;

    /*! \brief Hand-posture spans in ascending start order. */
    std::vector<HighwayShapeView> shapes;

    /*!
    \brief Tapping-hand onsets in ascending order, derived from the notes' Tap attacks.

    Right-hand presentation is derived, never authored (the right-hand-tap-lighting plan): these
    feed the per-tap light envelopes and the tapped chord boxes, and carry no user-editable data.
    */
    std::vector<HighwayTapOnsetView> tap_onsets;

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
\brief Builds the running maximum of note sustain ends, one entry per note.

Companion table for highwayVisibleNoteRange: notes are sorted by onset but sustains overlap
freely, so the visible-range lower bound needs the prefix maximum of end times.

\param notes Seconds-resolved notes sorted by start time.
\return Non-decreasing prefix maximum of end_seconds, sized like notes.
*/
[[nodiscard]] inline std::vector<double> makeHighwaySustainPrefixMax(
    const std::vector<HighwayNoteView>& notes)
{
    std::vector<double> prefix_max;
    prefix_max.reserve(notes.size());
    double running = 0.0;
    for (const HighwayNoteView& note : notes)
    {
        running = prefix_max.empty() ? note.end_seconds : std::max(running, note.end_seconds);
        prefix_max.push_back(running);
    }
    return prefix_max;
}

/*!
\brief Builds the running maximum of per-note end times supplied directly.

Overload for callers whose display end differs from the raw sustain end — the highway renderer
feeds the hold ends from highwayDisplayHoldEnds so span-held strums stay in the visible range
while their heads pin at the hit line.

\param end_seconds Per-note end times, ordered like the note list they describe.
\return Non-decreasing prefix maximum of the entries, sized like the input.
*/
[[nodiscard]] inline std::vector<double> makeHighwaySustainPrefixMax(
    const std::vector<double>& end_seconds)
{
    std::vector<double> prefix_max;
    prefix_max.reserve(end_seconds.size());
    double running = 0.0;
    for (const double end : end_seconds)
    {
        running = prefix_max.empty() ? end : std::max(running, end);
        prefix_max.push_back(running);
    }
    return prefix_max;
}

/*!
\brief Returns the note index range that can intersect a visible time span.

Sorted starts bound the range's end; the non-decreasing prefix maximum of sustain ends bounds its
start, because every note before the first index whose running maximum reaches the span ends
strictly before the span. The range is a tight superset — callers still intersect each note
individually because an early short note inside the range may end before the span begins.

\param notes Seconds-resolved notes sorted by start time.
\param prefix_max_end_seconds Running maximum of note end times from makeHighwaySustainPrefixMax.
\param span_start_seconds Visible span start.
\param span_end_seconds Visible span end.
\return Half-open [first, last) index range of candidate notes.
*/
[[nodiscard]] inline std::pair<std::size_t, std::size_t> highwayVisibleNoteRange(
    const std::vector<HighwayNoteView>& notes, const std::vector<double>& prefix_max_end_seconds,
    double span_start_seconds, double span_end_seconds) noexcept
{
    const auto begin_it = std::ranges::lower_bound(prefix_max_end_seconds, span_start_seconds);
    const auto end_it = std::ranges::upper_bound(
        notes, span_end_seconds, std::ranges::less{}, [](const HighwayNoteView& note) {
            return note.start_seconds;
        });

    const auto first =
        static_cast<std::size_t>(std::distance(prefix_max_end_seconds.begin(), begin_it));
    const auto last = static_cast<std::size_t>(std::distance(notes.begin(), end_it));
    return {std::min(first, last), last};
}

/*!
\brief Tolerance for matching an onset to another onset or a shape-span boundary.

Two events at the same musical grid position resolve through the tempo map on different code
paths (a forward cursor for note onsets, the plain resolver for shape boundaries), so they can
land a rounding epsilon apart; without this tolerance a chord sitting exactly on a hand shape's
start or end would intermittently fall outside the span.
*/
inline constexpr double g_highway_onset_match_epsilon = 1.0e-4;

/*!
\brief Derives the picking-hand onsets: one entry per onset group with taps or pick slides.

Right-hand presentation is derived, never authored: each entry carries the fret extent and
count of the right-hand notes struck together at that onset — feeding, for two or more
simultaneous taps, the tapped chord box — plus the light path the envelope follows: from the
onset through the hand's travel (a tap's pitched glides, or a scrape's whole waypoint path —
the light rides the slide either way) to the release: the sustain end for held contact and for
scrapes (the pick leaves at the path's end), or the last pitched station when an unpitched
trail-off is already releasing pressure. Fretting-hand notes sharing the onset contribute
nothing. Fret-zero notes are skipped, so a malformed chart can never place a light off the
board.

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
        double previous_fret = note.fret;
        for (const HighwaySlideView& waypoint : note.slides)
        {
            if ((waypoint.unpitched && !scrape) || waypoint.fret <= 0)
            {
                continue;
            }
            if (seconds <= waypoint.seconds)
            {
                const double span = waypoint.seconds - previous_seconds;
                const double weight =
                    span > 0.0 ? std::clamp((seconds - previous_seconds) / span, 0.0, 1.0) : 1.0;
                return previous_fret + ((waypoint.fret - previous_fret) * weight);
            }
            previous_seconds = waypoint.seconds;
            previous_fret = waypoint.fret;
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
            if (!rightHandOnset(note.attack) || note.fret <= 0)
            {
                continue;
            }
            view.fret_low = view.count == 0 ? note.fret : std::min(view.fret_low, note.fret);
            view.fret_high = std::max(view.fret_high, note.fret);
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
\brief Resolves each note's display hold end: the sustain end, span-extended for chord strums.

A strum under a hand-shape span is held for the whole span even when its notes carry no
sustain — the span (and the repeat boxes restating the chord on the highway) tells the player
how long to keep the shape fretted. Each sustainless note in a same-onset group of two or more
covered by a span therefore holds to the span's end. Groups whose notes are all fully muted
stay unextended (a dead chug is choked, not held), as do single notes and notes with explicit
sustains, whose drawn tails already state their hold. Coverage is positional only — a partial
strum under the span holds like a full restatement, and no posture matching applies.

\param notes Seconds-resolved notes sorted by start time.
\param shapes Seconds-resolved hand-posture spans sorted by start time.
\return Per-note hold end in seconds, sized like notes; never before the note's own end.
*/
[[nodiscard]] inline std::vector<double> highwayDisplayHoldEnds(
    const std::vector<HighwayNoteView>& notes, const std::vector<HighwayShapeView>& shapes)
{
    std::vector<double> hold_ends;
    hold_ends.reserve(notes.size());
    for (const HighwayNoteView& note : notes)
    {
        hold_ends.push_back(note.end_seconds);
    }
    // Both streams ascend, so one shape cursor tracks the latest span starting at or before
    // each onset group.
    std::size_t next_shape = 0;
    const HighwayShapeView* covering = nullptr;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double onset = notes[index].start_seconds;
        std::size_t group_end = index + 1;
        bool all_full_muted = notes[index].mute == NoteMute::Full;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - onset) < g_highway_onset_match_epsilon)
        {
            all_full_muted = all_full_muted && notes[group_end].mute == NoteMute::Full;
            ++group_end;
        }
        while (next_shape < shapes.size() &&
               shapes[next_shape].start_seconds <= onset + g_highway_onset_match_epsilon)
        {
            covering = &shapes[next_shape];
            ++next_shape;
        }
        if (group_end - index >= 2 && !all_full_muted && covering != nullptr &&
            onset <= covering->end_seconds + g_highway_onset_match_epsilon)
        {
            for (std::size_t member = index; member < group_end; ++member)
            {
                if (notes[member].end_seconds <= notes[member].start_seconds)
                {
                    hold_ends[member] = std::max(hold_ends[member], covering->end_seconds);
                }
            }
        }
        index = group_end;
    }
    return hold_ends;
}

} // namespace rock_hero::common::core
