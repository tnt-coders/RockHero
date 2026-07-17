/*!
\file tab_lane_layout.h
\brief Framework-free tablature lane geometry shared by the editor lane and the game strips.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

/*!
\brief Optional style variants of the shared notation renderer.

The single home for future presentation variants; today it carries only the scale knob. The
defaults reproduce the editor tab lane's shipped behavior exactly, so a default-constructed
style keeps every existing consumer byte-identical.
*/
struct TabLaneStyle
{
    /*!
    \brief Ceiling on the rendered note-head height in pixels.

    Charter's default noteHeight: lanes big enough to fit it render at exactly Charter's scale,
    and smaller lanes shrink notes proportionally (laneHeight = 1.5 x noteHeight).
    */
    float max_note_height{25.0f};
};

/*!
\brief Returns the number of string lanes the tablature should draw.

The chart's own string count is the floor: a user minimum only ever adds empty lanes below the
chart's strings and can never hide notes. A chart-less display draws no lanes at all.

\param chart_string_count String count declared by the displayed chart, or zero without a chart.
\param minimum_displayed_strings User minimum lane count; zero means match the chart.
\return Number of lanes to draw, or zero when there is no chart to draw.
*/
[[nodiscard]] int tabDisplayedStringCount(
    int chart_string_count, int minimum_displayed_strings) noexcept;

/*!
\brief Returns the vertical center of one string lane inside the lane bounds.

Lanes stack in standard tablature orientation: the highest-pitched string sits in the top lane
and the lowest in the bottom lane, evenly filling the bounds. Hosts size those bounds in
proportion to the string count, so the even division yields the reference per-lane spacing at
every count.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param bounds_y Top of the tablature lane bounds.
\param bounds_height Height of the tablature lane bounds.
\return Vertical lane center in the bounds' coordinate space.
*/
[[nodiscard]] float tabLaneCenterY(
    int displayed_string, int displayed_string_count, float bounds_y, float bounds_height) noexcept;

/*!
\brief Layout facts shared by every glyph of one rendered tablature lane.

Sizes follow Charter's DrawerUtils: the lane height fixes the note height (laneHeight = 1.5 x
noteHeight) and everything else derives from the note height with Charter's ratios. The struct
is framework-free so headless consumers (layout manifest queries, hit testing) share the exact
geometry the paint core draws with.
*/
struct TabLaneGeometry
{
    /*! \brief Timeline range represented by the lane width. */
    common::core::TimeRange visible_timeline{};

    /*! \brief Left edge of the lane bounds. */
    float bounds_x{};

    /*! \brief Top of the lane bounds. */
    float bounds_y{};

    /*! \brief Width of the lane bounds. */
    float bounds_width{};

    /*! \brief Height of the lane bounds. */
    float bounds_height{};

    /*! \brief Number of displayed string lanes. */
    int displayed_count{};

    /*! \brief Extra empty lanes displayed below the chart's strings. */
    int extra_lanes{};

    /*! \brief Height of one string lane. */
    float lane_height{};

    /*! \brief Rendered note-head height. */
    float note_height{};

    /*! \brief Sustain tail height; odd so the tail centers on the string line. */
    float tail_height{};

    /*! \brief Sustain tail border thickness. */
    float tail_edge_size{};

    /*! \brief Tremolo gem inset size. */
    float tremolo_size{};

    /*! \brief Style ceiling the note height was derived under (visibility slack derives here). */
    float max_note_height{};

    /*! \brief True when notes are large enough to carry readable fret numbers. */
    bool draw_text{};

    /*!
    \brief Maps a timeline time onto the lane's horizontal axis.
    \param seconds Timeline time to map.
    \return Horizontal pixel position in the bounds' coordinate space.
    */
    [[nodiscard]] float x(double seconds) const noexcept;

    /*!
    \brief Vertical lane center for a chart string, accounting for extra lanes below the chart.
    \param chart_string One-based chart string, 1 = the chart's lowest string.
    \return Vertical lane center in the bounds' coordinate space.
    */
    [[nodiscard]] float laneY(int chart_string) const noexcept;
};

/*!
\brief Derives the lane geometry for one displayed tablature lane.

\param bounds_x Left edge of the lane bounds.
\param bounds_y Top of the lane bounds.
\param bounds_width Width of the lane bounds; must be positive.
\param bounds_height Height of the lane bounds; must be positive.
\param visible_timeline Timeline range represented by the width; must have positive duration.
\param displayed_count Number of displayed lanes; must be positive.
\param chart_string_count String count declared by the displayed chart.
\param style Optional style variants; the default reproduces the editor lane exactly.
\return Geometry every glyph of the lane derives from.
*/
[[nodiscard]] TabLaneGeometry makeTabLaneGeometry(
    float bounds_x, float bounds_y, float bounds_width, float bounds_height,
    common::core::TimeRange visible_timeline, int displayed_count, int chart_string_count,
    TabLaneStyle style = {});

/*! \brief Vertical span of a sustain tail around the string line. */
struct TailSpan
{
    /*! \brief Top of the tail span. */
    float top;

    /*! \brief Bottom of the tail span. */
    float bottom;
};

/*!
\brief Returns the vertical sustain-tail span around one lane center (Charter's tail top/bottom).
\param geometry Lane geometry supplying the tail height.
\param center_y Vertical lane center the tail straddles.
\return Tail span in the bounds' coordinate space.
*/
[[nodiscard]] TailSpan tailSpan(const TabLaneGeometry& geometry, float center_y) noexcept;

/*!
\brief Returns the note index range that can intersect a visible time span.

Notes are sorted by start time but sustains overlap freely, so the lower bound comes from a
prefix-maximum table of sustain ends: the first note whose running maximum end reaches the span
can be the earliest visible one, and every note starting past the span's end is invisible. The
range is a tight superset — callers still intersect each note individually because an early
short note inside the range may end before the span begins.

\param notes Seconds-resolved notes sorted by start time.
\param prefix_max_end_seconds Running maximum of note end times, one entry per note.
\param span_start_seconds Visible span start.
\param span_end_seconds Visible span end.
\return Half-open [first, last) index range of candidate notes.
*/
[[nodiscard]] std::pair<std::size_t, std::size_t> tabVisibleNoteRange(
    const std::vector<common::core::TabNoteView>& notes,
    const std::vector<double>& prefix_max_end_seconds, double span_start_seconds,
    double span_end_seconds) noexcept;

/*!
\brief Builds the running maximum of note end times for tabVisibleNoteRange.
\param notes Seconds-resolved notes sorted by start time.
\return Prefix-maximum table aligned with the note order.
*/
[[nodiscard]] std::vector<double> tabPrefixMaxEndSeconds(
    const std::vector<common::core::TabNoteView>& notes);

} // namespace rock_hero::common::ui
