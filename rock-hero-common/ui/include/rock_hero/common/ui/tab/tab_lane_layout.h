/*!
\file tab_lane_layout.h
\brief Framework-free tablature lane geometry shared by the editor lane and the game strips.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>

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
\brief Size of one arpeggio posture bracket pair — the "[ fret ]" mark at its span's own opening
instant.

The bracket is the only mark that states a fret nothing struck, which makes it the mark an
authored silently-held stop wears; the editor therefore both draws it and hit-tests it, and these
are the numbers both of those read (\ref TabLaneGeometry::bracketGeometry).
*/
struct TabBracketGeometry
{
    /*! \brief Distance from the bracket's centre column to the outer face of each bar's clearance
    around the head. */
    float radius{};

    /*! \brief Half the bars' drawn height, measured from the lane's string line. */
    float half_height{};

    /*! \brief Bar width in whole pixels. */
    int bar{};

    /*! \brief Length of the serif capping each bar's top and bottom, in whole pixels. */
    int serif{};
};

/*!
\brief One posture bracket's DRAWN pixel columns and rows at a mark's centre.

Whole pixels, because the bracket draws as pixel-snapped rectangles: the marks stay perfectly square
instead of antialiasing into fuzz. It is a struct rather than four expressions because three passes
now have to land on exactly the same edges — the fill that draws the bars, the string-line gap that
clears them, and the outline the editor traces to show one selected — and an edge that missed its
bar by half a pixel would look like a rendering bug rather than a spelling one.
*/
struct TabBracketColumns
{
    /*! \brief Left edge of the opening bar. */
    int bar_left{};

    /*! \brief Right edge of the closing bar. */
    int bar_right{};

    /*! \brief Top of both bars. */
    int top{};

    /*! \brief Bottom of both bars. */
    int bottom{};
};

/*!
\brief The posture SATELLITE slot: the outboard digit column beside a bracket's closing bar.

Where a posture fret prints when the head at the MARK'S OWN INSTANT sounds a different one — the
two-hand tapping case, and now the ordinary case for any right-hand onset carrying a held stop. Two
slots make that conflict unrepresentable instead of arbitrated: the head's centre carries what
SOUNDS and this carries what the fretting hand HOLDS.

The width is derived from the lane's own text scale rather than measured from the digits it will
carry, and that is what makes the slot a GEOMETRY fact instead of a font one. Two things fall out
of it: every satellite in the lane is the same column, so a stack of them closes on one straight
right wall without anyone scanning a span for its widest value; and the framework-free layout
manifest can bound the slot exactly, which is what lets the editor hit-test the satellite as its
own target under the rule that every drawn mark is clickable and nothing undrawn is.
*/
struct TabSatelliteSlot
{
    /*! \brief Clear pixels between the bracket's closing bar and the digit column, and past it. */
    int gap{};

    /*! \brief Width of the digit column; the digit centres inside it. */
    int width{};

    /*!
    \brief The whole mark's horizontal extent: the gap, the column, and the gap past it.

    What every consumer actually wants — the painter gapping the lane line, the layout bounding the
    click target, the caret drawing its armed square — so the composition is spelled here once
    instead of in each of them, where the three could drift by a pixel and nobody would notice.

    \return The extent in whole pixels.
    */
    [[nodiscard]] constexpr int extent() const noexcept
    {
        return gap + width + gap;
    }
};

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

    /*! \brief Left edge of the lane bounds; \ref x measures horizontal positions from it. */
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

    /*!
    \brief Rendered note-head extent: one pixel more than the note height, the odd size that
           centers the head's box on the string line the way the odd tail height centers the tail.

    The one authority for the head's drawn extent — \ref TabNoteLayout::head_size reports this
    value, and every head-sized element (outline, mute icon, harmonic diamond, bracket) draws
    from it.

    \return Head extent in pixels.
    */
    [[nodiscard]] float headSize() const noexcept
    {
        return note_height + 1.0f;
    }

    /*!
    \brief Size of one arpeggio posture bracket pair, in this lane's pixels.

    The bracket hugs a note head's ring, so every value derives from \ref headSize and the bracket
    tracks the heads at each lane size. It lives on the geometry rather than in the painter because
    the bracket is now a HIT TARGET as well as a mark — selecting a held stop means clicking the
    bracket that states its stop — and a second copy of these numbers in the layout manifest would
    be the drift the manifest exists to prevent.

    \return The bracket pair's radius, half height, bar width and serif length.
    */
    [[nodiscard]] TabBracketGeometry bracketGeometry() const noexcept
    {
        const float size = headSize();
        const float border = size / 15.0f > 1.0f ? size / 15.0f : 1.0f;
        constexpr int bar = 2;
        return TabBracketGeometry{
            .radius = size / 2.0f + border,
            .half_height = size / 2.0f - border - static_cast<float>(bar),
            .bar = bar,
            .serif = static_cast<int>(size / 8.0f + 0.5f) + bar,
        };
    }

    /*!
    \brief The pixel columns and rows one posture bracket occupies about a mark's centre.

    The bracket's own numbers snapped to the grid it is drawn on, so every pass that touches a
    bracket lands on the same edges (\ref TabBracketColumns). The half-width is the bar's own centre
    line offset — the bars straddle the clearance radius — which is the same measure the layout
    manifest bounds the mark with.

    \param center_x Mark's centre column: the instant the bracket prints at, which since [D2]'s
           amendment 2 is not in general its span's start.
    \param center_y Lane centre of the bracket's string.

    \return The bars' outer columns and their shared top and bottom.
    */
    [[nodiscard]] TabBracketColumns bracketColumnsAt(
        const float center_x, const float center_y) const noexcept
    {
        const TabBracketGeometry bracket = bracketGeometry();
        const float half_width = bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
        // floor(value + 0.5) is juce::roundToInt's own rounding, spelled arithmetically so this
        // geometry stays framework-free for the headless consumers that share it.
        const auto snap = [](const float value) {
            return static_cast<int>(std::floor(value + 0.5f));
        };
        return TabBracketColumns{
            .bar_left = snap(center_x - half_width),
            .bar_right = snap(center_x + half_width),
            .top = snap(center_y - bracket.half_height),
            .bottom = snap(center_y + bracket.half_height),
        };
    }

    /*!
    \brief Height of the fret-number text, in pixels — the one authority for this lane's digits.

    The paint call builds its bold fret font at exactly this height (\ref TabLaneMetrics), and the
    framework-free geometry sizes the satellite slot from it, so the column a digit is drawn in and
    the column a click lands in derive from one number instead of two that happen to agree. The
    floor keeps small lanes legible; \ref draw_text is the separate question of whether digits are
    drawn at all.

    \return Fret-number text height in pixels.
    */
    [[nodiscard]] float fretTextHeight() const noexcept
    {
        constexpr float minimum = 8.0f;
        const float derived = note_height / 2.0f;
        return derived > minimum ? derived : minimum;
    }

    /*!
    \brief The outboard posture digit column beside a bracket, in this lane's pixels.

    The width holds two bold digits of \ref fretTextHeight with room to spare — the widest fret the
    board can state is two digits, and a bold digit's advance runs about six tenths of its text
    height, so 1.4 heights clears the pair on every platform's metrics and leaves the centred digit
    a little air inside its own ground. Deliberately NOT measured from the values in a span: a
    measured column would make the drawn slot a font fact the framework-free hit test could not
    reproduce, which is the drift the layout manifest exists to prevent.

    \return The gap around the column and the column's own width.
    */
    [[nodiscard]] TabSatelliteSlot satelliteSlot() const noexcept
    {
        // One pixel binds the digit to its bracket by proximity without letting the glyph's
        // antialiasing merge into the bar the way touching it does.
        constexpr int gap = 1;
        constexpr float digit_pair = 1.4f;
        return TabSatelliteSlot{
            .gap = gap,
            .width = static_cast<int>(fretTextHeight() * digit_pair + 0.5f),
        };
    }

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

/*! \brief Vertical span of a sustain tail around the string line: the whole drawn envelope. */
struct TailSpan
{
    /*! \brief Top of the tail envelope (the top rail's outer edge). */
    float top;

    /*! \brief Bottom of the tail envelope (the bottom rail's outer edge). */
    float bottom;
};

/*!
\brief Returns the vertical sustain-tail envelope around one lane center, edge rails included.

Symmetric about the lane center by construction, so every consumer — the ribbon, the tremolo
band's midline, the hit-test rectangle — centers on the string line without its own balancing
arithmetic.

\param geometry Lane geometry supplying the tail height.
\param center_y Vertical lane center the tail straddles.
\return Tail envelope in the bounds' coordinate space.
*/
[[nodiscard]] TailSpan tailSpan(const TabLaneGeometry& geometry, float center_y) noexcept;

} // namespace rock_hero::common::ui
