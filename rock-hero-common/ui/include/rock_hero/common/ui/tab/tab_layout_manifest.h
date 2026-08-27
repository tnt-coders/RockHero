/*!
\file tab_layout_manifest.h
\brief Framework-free per-note layout queries matching the shared notation paint core.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>

namespace rock_hero::common::ui
{

/*! \brief Axis-aligned rectangle in the lane bounds' pixel space. */
struct TabLayoutRect
{
    /*! \brief Left edge. */
    float x{};

    /*! \brief Top edge. */
    float y{};

    /*! \brief Width; zero means the rectangle is empty. */
    float width{};

    /*! \brief Height. */
    float height{};

    /*!
    \brief Returns true when a point lies inside the rectangle (right/bottom exclusive).
    \param point_x Horizontal probe position.
    \param point_y Vertical probe position.
    \return True when the point is inside.
    */
    [[nodiscard]] constexpr bool contains(float point_x, float point_y) const noexcept
    {
        return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
    }
};

/*!
\brief Pixel layout of one rendered note, matching the paint core's glyph geometry.

Hit testing resolves pointer positions against these rectangles instead of duplicating glyph
geometry: the values derive from the same TabLaneGeometry the paint core draws with, so clicks
and pixels can never drift apart. The head rectangle bounds the layered head shape (Charter
draws heads one pixel larger than the note height so they get a center pixel on the string
line); the tail rectangle spans the sustain bar between the onset and the note's presented end
(NoteViewState::end_seconds), and is empty for notes the lane draws no tail for — which is every
note presenting none, including a chugged member of a span-held strum.
*/
struct TabNoteLayout
{
    /*! \brief Horizontal onset position: the head center and glyph anchor column. */
    float onset_x{};

    /*! \brief Vertical lane center: the head center and glyph anchor row. */
    float center_y{};

    /*! \brief Rendered head extent (note height plus the center pixel). */
    float head_size{};

    /*! \brief Bounding rectangle of the layered head shape. */
    TabLayoutRect head{};

    /*! \brief Bounding rectangle of the sustain tail; empty when the lane draws no tail. */
    TabLayoutRect tail{};
};

/*!
\brief Computes the pixel layout of one note under the given lane geometry.

The rectangles are the DRAWN, clickable extent of the note: the note carries the only stop either
the layout or the paint core reads, so every drawn ribbon is hit-testable and nothing undrawn is.

\param geometry Lane geometry the paint core draws with.
\param note Seconds-resolved note to lay out; its presented end is where the tail rectangle stops.
\return Per-note layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept;

/*! \brief Pixel layout of one silently-held stop's posture bracket. */
struct TabSilentHoldLayout
{
    /*! \brief Horizontal position of the bracket's centre column: the span's start. */
    float center_x{};

    /*! \brief Vertical lane center of the hold's string: the bracket's centre row. */
    float center_y{};

    /*! \brief Bounding rectangle of the bracket pair — its drawn extent, and its clickable one. */
    TabLayoutRect box{};
};

/*!
\brief Computes the pixel layout of one silent hold's posture bracket, when it draws one.

A \ref common::core::NoteAttack::None note has no head of its own: the arpeggio bracket printing
its stop at the span start IS its face, which is why this reads the note's resolved bracket instant
(\ref common::core::NoteViewState::bracket_seconds) rather than the slot it was authored at. A hold
that resolved to no posture draws nothing anywhere, so it lays out to nothing here and is therefore
unclickable by construction — the same rule that keeps an undrawn waypoint head off the hit list,
stated once instead of guarded twice. So does any note that is not a silent hold, whose face is its
own head and whose layout is \ref tabNoteLayout's.

The box spans the bracket's two bars, not the fret digit outboard of the closing bar: the digit's
slot is decided against the head sounding at the span start and against the widest digit of the
span, both of which need a font the framework-free geometry does not have. The bars are drawn for
every posture string unconditionally, so bounding them is what keeps the clickable extent exactly
the always-drawn one.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note to lay out.
\return The bracket's layout, or nothing when the note is not a silent hold or joined no span.
*/
[[nodiscard]] std::optional<TabSilentHoldLayout> tabSilentHoldLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept;

/*! \brief Pixel layout of one linked waypoint head along a note's tail. */
struct TabWaypointLayout
{
    /*! \brief Horizontal position of the waypoint's instant: the head center column. */
    float center_x{};

    /*! \brief Vertical lane center of the note the waypoint rides. */
    float center_y{};

    /*! \brief Rendered head extent — the note head's own size. */
    float head_size{};

    /*! \brief Bounding rectangle of the linked head shape — its drawn extent, and its clickable
    one. */
    TabLayoutRect head{};
};

/*!
\brief Computes the pixel layout of one linked waypoint head under the given lane geometry.

The waypoint marks the lane already draws are what the editor hit-tests, so this reads the same
instant and the same head size the paint core draws with. A waypoint the lane draws NO head for —
one at the presented tail's end, where the re-picked landing draws its own — is still laid out
here; asking whether a head exists there is \ref common::core::linkedWaypoint's job, and the
caller does that before treating this box as clickable, exactly as the paint core does before
drawing.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the waypoint belongs to; its string places the head.
\param waypoint One of the note's \ref common::core::NoteViewState::slides entries.
\return Per-waypoint layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabWaypointLayout tabWaypointLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::SlideViewState& waypoint) noexcept;

} // namespace rock_hero::common::ui
