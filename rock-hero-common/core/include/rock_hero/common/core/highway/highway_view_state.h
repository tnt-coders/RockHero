/*!
\file highway_view_state.h
\brief Seconds-resolved, camera-agnostic frame content for the 3D note highway.
*/

#pragma once

#include <algorithm>
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

    /*! \brief Harmonic timbre applied to the note. */
    NoteHarmonic harmonic{NoteHarmonic::None};

    /*!
    \brief Fractional touch position for harmonics sounding between frets.

    Carries the chart's exact node point (the 3.2 / 2.7 / 5.8 family) so the highway can place
    the harmonic head at the true touch position instead of the fret middle. Only meaningful
    when harmonic is set.
    */
    std::optional<double> touch{};

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

    Derived at projection time: a placement landing exactly on a pitched slide waypoint ramps
    over that glide segment so the window travels with the slide, and every other placement
    morphs over the minimum-sustain-distance margin (shortened when placements crowd closer
    than the ramp).
    */
    double ramp_seconds{0.0};

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

    /*! \brief Fret-hand position markers in ascending order. */
    std::vector<HighwayFhpView> fret_hand_positions;

    /*! \brief Every beat of the song grid in ascending order, downbeats marked. */
    std::vector<HighwayBeatView> beats;

    /*! \brief Section labels in ascending order. */
    std::vector<HighwaySectionView> sections;

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

} // namespace rock_hero::common::core
