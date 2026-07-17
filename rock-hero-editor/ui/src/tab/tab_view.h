/*!
\file tab_view.h
\brief JUCE component that renders the 2D tablature lane over the arrangement waveform.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief String count whose lane spacing every displayed count matches.

Six lanes across the default waveform row is the reference density. The hosting row is sized in
proportion to the string count at this density (see TrackViewport), so a four-string bass shrinks
the row to fit and an eight-string display grows it, both keeping identical per-lane spacing
instead of compressing the lanes or leaving empty margins.
*/
inline constexpr int g_tab_reference_string_count{6};

/*!
\brief Returns the number of string lanes the tablature lane should draw.

The chart's own string count is the floor: a user minimum only ever adds empty lanes below the
chart's strings and can never hide notes. A chart-less arrangement draws no lanes at all.

\param chart_string_count String count declared by the displayed chart, or zero without a chart.
\param minimum_displayed_strings User minimum lane count; zero means match the chart.
\return Number of lanes to draw, or zero when there is no chart to draw.
*/
[[nodiscard]] int tabDisplayedStringCount(
    int chart_string_count, int minimum_displayed_strings) noexcept;

/*!
\brief Returns the base display color for one string lane.

The six highest lanes take Charter's default six string colors — red, yellow, blue, orange,
green, purple from the sixth-highest lane upward — so a four-string bass keeps red through
orange and a standard guitar keeps the familiar six. Lanes below that window continue with the
shipped extended tier going down — teal for the 7th, Charter's near-white gray for the 8th —
cycling defensively for even lower lanes; further colors arrive with the string-cap raise. The
palette data and Charter's fixed derivation multipliers live in rock-hero-common/ui
(string_color_palette.h), shared with the game highway; this is a thin JUCE-converting wrapper.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\return Base lane color the tablature style derives its surfaces from.
*/
[[nodiscard]] juce::Colour tabStringColor(int displayed_string, int displayed_string_count);

/*!
\brief Returns the hand-shape mark color shared by the tab lane and the ruler's name chips.

The Charter hand-shape base brightened so the narrow span rails and the chord/arpeggio name
chips read clearly against dark chrome. The tablature lane (rails) and the timeline ruler
(name chips derived from the same tab projection) must agree on it so a chip visually belongs
to the rails below it.

\param arpeggio True for arpeggio spans (purple); false for chord spans (blue).
\return Opaque mark color.
*/
[[nodiscard]] juce::Colour tabShapeMarkColor(bool arpeggio);

/*!
\brief Returns the vertical center of one string lane inside the tablature bounds.

Lanes stack in standard tablature orientation: the highest-pitched string sits in the top lane
and the lowest in the bottom lane, evenly filling the bounds. The host sizes those bounds in
proportion to the string count (see TrackViewport and g_tab_reference_string_count), so the even
division yields the reference per-lane spacing at every count.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param bounds Full tablature lane bounds.
\return Vertical lane center in the bounds' coordinate space.
*/
[[nodiscard]] float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept;

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
\brief Renders the chart tablature over the arrangement waveform lane in Charter's visual style.

The view draws string lines with Charter's modern-theme note presentation — layered circular
note heads (diamonds for harmonics) with fret numbers, bordered sustain tails, technique icons,
slide and bend lines with label chips, and hand-shape spans — from the controller's
seconds-resolved tab projection, mapping time to pixels with the same visible-timeline
convention as the waveform beneath it, then the chart-editing overlays (selection rings, the
and the in-flight marquee) above the notation. While a chart is displayed the
lane owns its pointer events, converting them to lane-local chart pointer intents; the
controller decides what a press means (select, seek, or marquee), so empty-lane clicks still
seek. Without a chart the lane is pointer-transparent as before.
*/
class TabView final : public juce::Component
{
public:
    /*! \brief One pointer-intent sink receiving every phase of a lane gesture. */
    using PointerEventCallback =
        std::function<void(core::ChartPointerPhase, const core::ChartPointerEvent&)>;

    /*! \brief Creates an empty tablature lane. */
    TabView();

    /*!
    \brief Installs the sink that receives the lane's chart pointer intents.
    \param on_pointer_event Callback invoked for every gesture phase; empty disables forwarding.
    */
    void setPointerEventCallback(PointerEventCallback on_pointer_event);

    /*! \brief Sustain-adjust sink for Alt+wheel: direction is +1/-1, fine follows Ctrl. */
    using SustainWheelCallback = std::function<void(int direction, bool fine)>;

    /*!
    \brief Installs the sink that receives Alt+wheel sustain adjustments over the lane.
    \param on_sustain_wheel Callback receiving wheel direction and the precision flag.
    */
    void setSustainWheelCallback(SustainWheelCallback on_sustain_wheel);

    /*!
    \brief Adjusts the selection's sustain on Alt+wheel; other wheel events pass through.
    \param event Mouse event delivered by JUCE.
    \param wheel Wheel movement details.
    */
    void mouseWheelMove(
        const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    /*!
    \brief Applies the chart-editing overlay state (selection, marquee).
    \param edit Overlay state resolved against the same projection instance as setState's tab.
    */
    void setEditState(core::ChartEditViewState edit);

    /*!
    \brief Reports whether the lane wants the pointer at a lane-local position.

    The cursor overlay's pass-through predicate queries this: with a chart displayed the lane
    claims its whole band (the controller still turns empty clicks into seeks), and without one
    it stays transparent so the overlay's click-to-seek is untouched.

    \param local_point Position in this component's coordinates.
    \return True when the lane should receive the pointer event.
    */
    [[nodiscard]] bool wantsPointerAt(juce::Point<int> local_point) const;

    /*!
    \brief Claims pointer events exactly where wantsPointerAt does.
    \param x Pointer x position in local coordinates.
    \param y Pointer y position in local coordinates.
    \return True when the lane should receive the pointer event.
    */
    bool hitTest(int x, int y) override;

    /*!
    \brief Forwards a press as a chart pointer Down intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards held-button movement as a chart pointer Drag intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards the release as a chart pointer Up intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Stores the visible timeline range used to map note times to pixels.
    \param visible_timeline Timeline range represented by the component width.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Applies the current tab projection and lane-count preference.

    The projection is compared by pointer identity: the controller rebuilds it only when the
    displayed arrangement changes, so identical pointers mean identical content.

    \param tab Seconds-resolved tab projection, or null when the arrangement has no chart.
    \param minimum_displayed_strings User minimum lane count; zero means match the chart.
    */
    void setState(
        std::shared_ptr<const common::core::TabViewState> tab, int minimum_displayed_strings);

    /*!
    \brief Draws the visible notes and sustains onto the lane.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

private:
    // Rebuilds the prefix-maximum sustain-end table after the projection changes.
    void rebuildVisibilityIndex();

    // Builds the chart pointer event for a mouse event using the currently painted geometry.
    [[nodiscard]] core::ChartPointerEvent makePointerEvent(const juce::MouseEvent& event) const;

    // Seconds-resolved tab projection shared with the controller; null without a chart.
    std::shared_ptr<const common::core::TabViewState> m_tab{};

    // Chart-editing overlay state (selection indices, marquee) pushed by the editor.
    core::ChartEditViewState m_edit{};

    // Sink receiving the lane's chart pointer intents; empty disables pointer forwarding.
    PointerEventCallback m_on_pointer_event{};

    // Sink receiving Alt+wheel sustain adjustments; empty passes wheel events through.
    SustainWheelCallback m_on_sustain_wheel{};

    // User minimum lane count; zero means match the chart's string count.
    int m_minimum_displayed_strings{0};

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};

    // Running maximum of note end times, aligned with the projection's note order.
    std::vector<double> m_prefix_max_end_seconds{};
};

} // namespace rock_hero::editor::ui
