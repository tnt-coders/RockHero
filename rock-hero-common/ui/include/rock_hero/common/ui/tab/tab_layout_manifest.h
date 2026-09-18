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
\brief Pixel layout of one rendered note's HEAD, matching the paint core's glyph geometry.

**HEADS ARE TARGETS; TAILS ARE TESTIMONY**. A note is addressed at the one column where it happens
— its onset — and a tail says how long the string rings, which is evidence and not a handle. A
click in the lane moves the caret to the slot under the pointer, exactly as a click in empty lane
does, rather than selecting a note whose onset is somewhere else entirely ("that selection is not
under the caret").

The rule is UNIFORM: a VISIBLE tail does not select either, not only ink a covering span's
furniture owns. That is what lets this manifest publish head rectangles alone — a tail rectangle
would be a target nothing may resolve against, and it would not bound what the lane draws: it spans
the whole presented ring while a member under a span's ink draws no ribbon at all.

Hit testing resolves pointer positions against these rectangles instead of duplicating glyph
geometry: the values derive from the same TabLaneGeometry the paint core draws with, so clicks
and pixels can never drift apart. The head rectangle bounds the layered head shape (Charter
draws heads one pixel larger than the note height so they get a center pixel on the string
line).
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
};

/*!
\brief Computes the pixel layout of one note's head under the given lane geometry.

The rectangle is the DRAWN, clickable extent of the note's HEAD, which is the whole of what a note
is addressed by: heads are targets, tails are testimony.

\param geometry Lane geometry the paint core draws with.
\param note Seconds-resolved note to lay out; its onset and string place the head.
\return Per-note head layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabNoteLayout tabNoteLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note) noexcept;

/*! \brief Pixel layout of one held stop's satellite digit, outboard of its posture bracket. */
struct TabHeldStopLayout
{
    /*! \brief Horizontal centre of the digit column. */
    float center_x{};

    /*! \brief Vertical lane center of the note's string: the digit's centre row. */
    float center_y{};

    /*! \brief Bounding rectangle of the satellite slot: its drawn extent, and its clickable one. */
    TabLayoutRect box{};
};

/*!
\brief Computes the pixel layout of one note's held-stop satellite, when it draws one.

The second stop a note states (\ref common::core::NoteViewState::held) prints in its own column
outboard of the head's own bracket columns, because the head's centre is already carrying what
that head SOUNDS. That column is its independent target: clicking it addresses the held stop
where clicking the head addresses the sounding fret. THREE populations wear one: the planted finger
under a plain tap or a scrape, whose own fret is the picking hand's; the PRESSED stop a tapped
harmonic states as its own fret, the fretting hand's under a node the picking hand only touches;
and the stop a pull-off PLANTS beneath a FRETTING-hand onset (THE PLANT'S FACE), which the bracket
then prints nothing of on that string, so exactly one ink states it either way.

Both facts are the whole test, and neither can be inferred from the other: the stop itself says the
note states one, and the resolved mark says whether its digit is SHOWN and where. A stop whose face
waits for the reveal (\ref common::core::StopMarkFace::Revealed) lays out to nothing until
`revealed` says its note's truth is on show — the same per-note pick that swaps the note to its real
ring, asked here through \ref common::core::stopMarkShown so the drawn digit and the clickable one
can never part.

The vertical extent is the bracket's own, so the two halves of a bracketed mark present the same
target height; it lies inside the digit's drawn box, which is a full head tall, so nothing undrawn
becomes clickable. The column sits outboard of the closing bar, so it never overlaps the bars or a
digit centred between them.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note to lay out.
\param revealed True when this note's whole truth is on show, which is what a reveal-only face
       waits for. Deliberately not defaulted: a surface with no reveal answers false, and it says
       so, rather than a forgotten argument quietly deciding a mark is absent.
\return The satellite's layout, or nothing when the note states no held stop or none is shown.
*/
[[nodiscard]] std::optional<TabHeldStopLayout> tabHeldStopLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    bool revealed) noexcept;

/*! \brief Pixel layout of one keyframe's mark: a linked head along a note's tail, or the
release's falls-away chip at its end. */
struct TabKeyframeLayout
{
    /*! \brief Horizontal position of the keyframe's instant: the mark's center column. */
    float center_x{};

    /*! \brief Vertical center of the mark: the note's string line for a head, the chip's line
    for a release. */
    float center_y{};

    /*! \brief Rendered head extent — the note head's own size. */
    float head_size{};

    /*! \brief True when the mark is the release's chip rather than a head, so a host tracing the
    mark traces a box and not the note's head silhouette. */
    bool chip{false};

    /*! \brief Bounding rectangle of the mark — its drawn extent, and its clickable one. */
    TabLayoutRect head{};
};

/*!
\brief Computes the pixel layout of one keyframe's mark under the given lane geometry.

The keyframe marks the lane already draws are what the editor hit-tests, so this reads the same
instant and the same head size the paint core draws with. A release (\ref
common::core::KeyframeViewState::release) has no head: the slide line ends in its falls-away chip,
above the tail when the last leg rises and below it when it falls, so its box is the chip's ground
— the fret-text height plus the chip's padding, wide enough for two digits — on the chip's own
line. A keyframe the lane draws NO mark for — one at the presented tail's end that is not the
release, where the re-picked landing draws its own head — is still laid out here; asking whether
a mark exists there is the caller's job, exactly as the paint core asks before drawing.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the keyframe belongs to; its string places the head.
\param keyframe One of the note's \ref common::core::NoteViewState::slides entries.
\return Per-keyframe layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe) noexcept;

} // namespace rock_hero::common::ui
