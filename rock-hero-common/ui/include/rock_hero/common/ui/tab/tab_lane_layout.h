/*!
\file tab_lane_layout.h
\brief Framework-free tablature lane geometry shared by the editor lane and the game strips.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <vector>

namespace rock_hero::common::ui
{

/*!
\brief Optional style variants of the shared notation renderer.

The single home for presentation variants; it carries only the scale knob. The defaults
reproduce the editor tab lane's behavior exactly, so a default-constructed style draws every
consumer the way the editor lane does.
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

The bracket is the only mark that states a fret nothing struck — the stop a hand holds under a
right-hand onset, printed at its span's own instant. The satellite column beside it is sized from
these numbers too (\ref TabLaneGeometry::bracketGeometry).
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
have to land on exactly the same edges — the fill that draws the bars, the string-line gap that
clears them, and the satellite digit that sits outboard of the closing bar — and an edge that missed
its bar by half a pixel would look like a rendering bug rather than a spelling one.
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
two-hand tapping case, and the ordinary case for any right-hand onset carrying a held stop. Two
slots make that conflict unrepresentable instead of arbitrated: the head's centre carries what
SOUNDS and this carries what the fretting hand HOLDS.

The width is derived from the lane's own text scale rather than measured from the digits it will
carry, and that is what makes the slot a GEOMETRY fact instead of a font one. Two things fall out
of it: every satellite in the lane is the same column, so a stack of them closes on one straight
right wall without anyone scanning a span for its widest value; and the framework-free layout
manifest can bound the slot exactly, so the painter and anything measuring the lane agree on it.
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
    the layout manifest places the satellite column beside the bracket — a displaced digit
    outboard of the closing bar — from these numbers, and a second copy of them there would be the
    drift the manifest exists to prevent.

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
            .serif = static_cast<int>(std::lround(size / 8.0f)) + bar,
        };
    }

    /*!
    \brief The pixel columns and rows one posture bracket occupies about a mark's centre.

    The bracket's own numbers snapped to the grid it is drawn on, so every pass that touches a
    bracket lands on the same edges (\ref TabBracketColumns). The half-width is the bar's own centre
    line offset — the bars straddle the clearance radius — which is the same measure the layout
    manifest bounds the mark with.

    \param center_x Mark's centre column: the instant the bracket prints at, which is not in
           general its span's start.
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
            .width = static_cast<int>(std::lround(fretTextHeight() * digit_pair)),
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
    \brief Returns the timeline seconds one horizontal pixel spans: the inverse of \ref x's scale.
    \return Seconds per pixel.
    */
    [[nodiscard]] double secondsPerPixel() const noexcept;

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

/*!
\brief Vertical span of a sustain tail's INTERIOR: the band between its edge rails, symmetric
about the string line like the envelope itself.

The one definition of where a technique mark may live — the sine and the bend polyline COMPRESS
their swing to fit it, the slide diagonals anchor their endpoints on it, the technique clip holds
every mark inside it, and the side chip's ground fills exactly it — so a mark meets the tail's edge
scaled, never cut.
*/
struct TailInterior
{
    /*! \brief Top of the interior (the top rail's inner edge). */
    float top;

    /*! \brief Bottom of the interior (the bottom rail's inner edge). */
    float bottom;
};

/*!
\brief Returns the interior of the sustain tail around one lane center: its envelope less the rails.
\param geometry Lane geometry supplying the tail height and rail thickness.
\param center_y Vertical lane center the tail straddles.
\return Tail interior in the bounds' coordinate space.
*/
[[nodiscard]] TailInterior tailInterior(const TabLaneGeometry& geometry, float center_y) noexcept;

/*!
\brief Charter's white technique-line stroke, shared by the slide diagonals and the bend polyline.

One constant because the interior anchoring assumes it: both drawers inset their endpoints by half
of THIS stroke, so a divergence would push one of them back onto the rails.
*/
inline constexpr float g_technique_line_thickness = 2.0f;

/*!
\brief The dot radius of a point riding the bend curve, as a fraction of the tail height: the
automation lanes' point mark, scaled to the tail it rides.
*/
inline constexpr float g_bend_dot_radius_tails = 0.25f;

/*!
\brief Where the bend curve stands at an amount: THE one curve height, read by the painter that
draws the polyline and the manifest that places a curve point's dot (\ref bendCurveYAt).

The height is how far the string physically travels (\ref common::core::bendTravel, the law the 3D
lift draws too) as a share of the travel three whole steps take — compressed into the tail's
interior with the stroke included, like the vibrato sine, so the polyline meets the rails scaled
instead of being cut by the technique clip: rest sits on the interior's floor, three whole steps
on its ceiling, and an amount past them clamps there. A half step therefore rises about a third of
the way, the first of the travel being the longest, exactly as the fretting hand feels it.

\param geometry Lane geometry supplying the tail's interior.
\param center_y The note's string line.
\param semitones Bend amount in semitones.
\return The curve's center on the vertical axis at that amount.
*/
[[nodiscard]] float bendCurveY(
    const TabLaneGeometry& geometry, float center_y, double semitones) noexcept;

/*!
\brief Where the DRAWN bend curve runs at an instant along a note: the one height a point riding the
curve is placed at, read by the manifest that lays its dot out.

The lane draws the curve as straight legs between its points' heights (\ref bendCurveY) and flat
past the last one, so this is that polyline's own height — exactly a point's at its instant, and
between two points where the drawn leg crosses — never a height the curve is interpolated to by
some other law. A note whose curve is empty never leaves rest.

\param geometry Lane geometry supplying the tail's interior.
\param center_y The note's string line.
\param curve The note's bend points (\ref common::core::NoteViewState::bend), in time order.
\param seconds The instant, on or after the onset.
\return The curve's center on the vertical axis at that instant.
*/
[[nodiscard]] float bendCurveYAt(
    const TabLaneGeometry& geometry, float center_y,
    const std::vector<common::core::BendPointViewState>& curve, double seconds) noexcept;

/*!
\brief Whether a note draws any tail at all within the extent it is drawn to.

No tail, no tail marks: a note whose ink stops at its onset wears no leg and no destination chip,
which would sit on its head.

\param note The note.
\param drawn_end The extent the note is drawn to (\ref common::core::drawnEndSeconds).
\return True when the extent runs past the onset.
*/
[[nodiscard]] bool tailInked(const common::core::NoteViewState& note, double drawn_end) noexcept;

/*!
\brief How far along a leg toward a stop past the drawn extent the extent falls, so the leg is
drawn on its true path and cut there: a slide or bend written to land on the next head slopes
toward it and simply ends. A leg of no width is complete.
\param from_x Column the leg leaves from.
\param to_x Column of the stop it heads for.
\param end_x Column of the drawn extent.
\return The fraction of the leg drawn, in [0, 1].
*/
[[nodiscard]] float cutLegProgress(float from_x, float to_x, float end_x) noexcept;

/*! \brief One leg of the drawn bend curve, in the lane bounds' pixel space. */
struct TabBendLeg
{
    /*! \brief Column the leg leaves from. */
    float from_x{};

    /*! \brief Height the leg leaves from. */
    float from_y{};

    /*! \brief Column the leg ends at: its point's, or the drawn extent where the leg is cut. */
    float to_x{};

    /*! \brief Height the leg ends at, on its true path. */
    float to_y{};

    /*! \brief True where the extent cuts the leg short of its point: the last ink. */
    bool cut{};
};

/*!
\brief Lays out one leg of the drawn bend curve: the leg INTO a point, or past the last point the
held run to the drawn extent.

THE ONE statement of the curve's polyline, read by the paint core that strokes it and by the layout
manifest that places the chip where a leg ends (\ref tabBendPointChipBox), so a destination chip at
the crop stands exactly where the cut leg stops. The first leg leaves the onset at rest; every later
one leaves a pixel past the point before it, which opens a hairline between consecutive legs. A leg
toward a point past the extent is drawn on its true path as far as the extent (\ref cutLegProgress)
and is the last ink.

\param geometry Lane geometry the notation is painted with.
\param note The bent note; its curve is \ref common::core::NoteViewState::bend.
\param point Index of the point the leg runs into, or the curve's size for the held run after the
       last point.
\param drawn_end The extent the note is drawn to (\ref common::core::drawnEndSeconds).
\return The leg.
*/
[[nodiscard]] TabBendLeg tabBendLeg(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note, std::size_t point,
    double drawn_end) noexcept;

/*!
\brief Where a chip stating a stop on the tail sits: just outside the sustain envelope, above it
when the leg into that stop rises and below it when the leg falls.

THE ONE CHIP BAND, stated here for the painter that draws the chip and the manifest that bounds its
click, so the box and the mark can never land on opposite sides of the envelope.

\param geometry Lane geometry supplying the tail height and the note height the lift is measured in.
\param center_y The note's string line.
\param upward True where the leg into the stop rises.
\return The chip's center on the vertical axis.
*/
[[nodiscard]] float slideOutChipY(
    const TabLaneGeometry& geometry, float center_y, bool upward) noexcept;

/*!
\brief The left edge of the head a ring ends on, where it ends on the next head of its own string
(\ref common::core::NoteViewState::end_head), or nothing.

THE COLUMN THE HEAD OWNS, stated once: a bend point's dot that would reach into it draws none, its
chip being its face (\ref TabKeyframeLayout::mark_drawn), because a white dot on the head's white
digit reads as a different digit. Chips are not held out: every chip stands centred on its true
column, and where an ending ring's chip meets the head's own pre-bend chip the head's, painted
later, reads on top until the ending point is selected. Geometric rather than keyed on the ring's
last instant, so what the head covers depends on the zoom, as the head's own width does: zoomed
in, a point just before the end draws its dot clear of the head at its true instant.

\param geometry Lane geometry supplying the time mapping and the head size.
\param note The ring.
\return The head's left edge, or nothing where the ring does not end on a head.
*/
[[nodiscard]] std::optional<float> nextHeadLeftEdge(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept;

} // namespace rock_hero::common::ui
