/*!
\file tab_layout_manifest.h
\brief Framework-free per-note layout queries matching the shared notation paint core.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>

namespace rock_hero::common::ui
{

/*!
\brief Answers whether one event, by its index, is REVEALED, for a host that reveals.

THE ONE pick a reveal makes, asked per note by the paint core and the hit tester and per span by
the furniture pass. A revealed note is drawn to its ring's end (\ref common::core::drawnEndSeconds),
so every keyframe it stores shows at its true instant, and its reveal-only marks come in with it
(\ref common::core::stopMarkShown); a revealed span's furniture runs to its musical close
(\ref common::core::ShapeViewState::close_seconds) instead of the extent rule 12a trimmed. A host
derives the answer from a predicate of its own; the cores are told the answer and never the reason,
so the drawn picture and the reachable one cannot part.

An empty accessor is the ordinary case and reveals nothing (\ref tabRevealed), which is the whole
answer for a surface with no reveal at all: the game's tab strips.
*/
using TabRevealed = std::function<bool(std::size_t index)>;

/*!
\brief Reads a reveal answer for one index, an empty accessor revealing nothing.
\param revealed The host's answer, possibly empty.
\param index The event's index.
\return True when the host reveals that event.
*/
[[nodiscard]] inline bool tabRevealed(const TabRevealed& revealed, const std::size_t index)
{
    return revealed && revealed(index);
}

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
\brief Layout of one floating chip: the box a click on it lands in, and the column it stands on.

A chip has two widths, the click box's (the widest text it can print, since the headless hit tester
measures no text) and the painted plate's (the text it does print). Both are placed from the ANCHOR
by the one chip rule (\ref chipLeftEdge), so the anchor is kept beside the box: once the ring's
limit pushes the box back, the box's own centre no longer says where a narrower plate stands.
*/
struct TabChipLayout
{
    /*! \brief The click target, placed by \ref leftEdge at the widest text's width. */
    TabLayoutRect box{};

    /*! \brief The column the chip centres on unless its ring's limit pushes it back. */
    float anchor_x{};

    /*! \brief The ring's chip limit (\ref ringChipLimit), carried so no reader derives it again. */
    std::optional<float> limit{};

    /*!
    \brief Where a width of this chip starts: the one chip rule (\ref chipLeftEdge) applied to this
    chip's anchor and limit, for the click box and the painted plate alike.
    \param width The width being placed.
    \return The left edge.
    */
    [[nodiscard]] float leftEdge(float width) const noexcept
    {
        return chipLeftEdge(anchor_x, width, limit);
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
the whole inked ring while a member under a span's ink draws no ribbon at all.

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

    /*!
    \brief The chip printing the onset's own bend above the head (\ref tabBendPointChip), or
    nothing where the note's curve does not open at its onset or the lane prints no text.

    The note's SECOND FACE, on the rule every chip follows: a chip is a face of what owns it, so a
    click on it reaches the note and a selection of the note rings it.
    */
    std::optional<TabChipLayout> bend_chip{};
};

/*!
\brief The head-sized square at a slot — THE one statement of where a head stands, whatever stands
there or will: the note head, the caret riding a slot, a popup anchored on an instant along a ring.

\param geometry Lane geometry the notation was painted with.
\param seconds The slot's instant.
\param chart_string One-based chart string of the slot.
\return The square, centred on the instant and the string line.
*/
[[nodiscard]] TabLayoutRect tabSlotHeadSquare(
    const TabLaneGeometry& geometry, double seconds, int chart_string) noexcept;

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
under a plain tap or a scrape, whose own fret is the picking hand's; the PRESSED stop of a harmonic
whose head prints its node instead (\ref common::core::harmonicOverPressedStop) — the artificial one
and the tapped one alike, the fretting hand's stop under a node the picking hand only touches; and
the stop a pull-off PLANTS beneath a note the picking hand does NOT stop the string for (THE PLANT'S
FACE), which the bracket then prints nothing of on that string, so exactly one ink states it either
way.

Both facts are the whole test, and neither can be inferred from the other: the stop itself says the
note states one, and the resolved mark says whether its digit is SHOWN and where. A stop whose face
waits for the reveal (\ref common::core::StopMarkFace::Revealed) lays out to nothing until
`revealed` says its note's truth is on show — the same per-note pick that draws the note to its
ring end, asked here through \ref common::core::stopMarkShown so the drawn digit and the clickable
one can never part.

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

/*! \brief What a keyframe's mark is, so a host tracing it traces the right outline. */
enum class TabKeyframeShape : std::uint8_t
{
    /*! \brief A linked head: the note's own head silhouette on its string line. */
    Head,

    /*! \brief A chip: the slide-out's, or the destination chip at the crop — a box. */
    Chip,

    /*! \brief A point riding the bend curve: its dot there — a disc. */
    Dot,
};

/*! \brief Pixel layout of one keyframe's mark: a linked head along a note's tail, a chip, or a
point's dot on the bend curve. */
struct TabKeyframeLayout
{
    /*! \brief The mark's centre column: its box's centre. */
    float center_x{};

    /*! \brief Vertical center of the mark: the note's string line for a head, the chip's line
    for a chip, the curve's height for a dot. */
    float center_y{};

    /*! \brief Rendered head extent — the note head's own size. */
    float head_size{};

    /*! \brief What the mark is (\ref TabKeyframeShape). */
    TabKeyframeShape shape{TabKeyframeShape::Head};

    /*!
    \brief Bounding rectangle of the mark — its drawn extent, and where the lane draws it (\ref
    mark_drawn) its clickable one. A dot's is the disc a selection ring traces, half a head across,
    since the dot itself is too small a target to find.
    */
    TabLayoutRect box{};

    /*!
    \brief True where the lane draws the mark, the one answer the paint core, the hit tester and the
    selection overlay read. A mark within the drawn extent is drawn, but for a bend point's dot that
    would reach into the column of the head its ring ends on (\ref nextHeadLeftEdge), whose chip is
    its only face there; past the extent only the destination chip a cut slide leg wears at the crop
    is drawn, and it names this keyframe and so reaches it.
    */
    bool mark_drawn{};

    /*!
    \brief The chip the mark IS, for a \ref TabKeyframeShape::Chip mark (a slide-out's chip, or the
    destination chip at the crop), whose click box is \ref box; nothing for a head or a dot.
    */
    std::optional<TabChipLayout> mark_chip{};

    /*!
    \brief The chip printing the bend this keyframe states (\ref tabBendPointChip), or
    nothing where the lane draws none: the keyframe states no bend, the lane prints no text, or its
    point stands past the extent without wearing the destination chip.

    The keyframe's SECOND FACE: a click on it reaches the keyframe as a click on the mark does, and
    a selection rings both.
    */
    std::optional<TabChipLayout> bend_chip{};
};

/*!
\brief Computes the pixel layout of one gesture stop's mark under the given lane geometry, for the
extent the note is drawn to.

THE ONE statement of where a stop's mark stands, read by the paint core that draws it and, through
\ref tabKeyframeLayout, by the hit tester that bounds a click on it. A stop within the drawn extent
(\ref common::core::instantDrawn) stands at its instant: a linked one wears the note's own head
there, and a slide-out (\ref common::core::SlideStopViewState::slide_out) has no head — the slide
line ends in its chip, above the tail when the last leg rises and below it when it falls, so its
box is the chip's ground: the fret-text height plus the chip's padding, wide enough for two digits,
on the chip's own line. A stop PAST the extent is the DESTINATION CHIP at the crop: the same chip,
naming where the leg the ink ends on is heading, standing at the drawn extent rather than at the
stop's own instant.

Whether the chip is DRAWN is \ref TabKeyframeLayout::mark_drawn: a level leg past the extent draws
no destination chip, a shift slide's arrival draws none beside the head that shows its landing, and
a note whose ink stops at its onset, or a lane that prints no text, draws none at all. The box is
placed by the one chip rule (\ref chipLeftEdge).

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the stop belongs to; its string places the head.
\param stop Index of the stop in the note's \ref common::core::NoteViewState::slides.
\param drawn_end The extent the note is drawn to (\ref common::core::drawnEndSeconds).
\return Per-stop layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabKeyframeLayout tabSlideStopLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note, std::size_t stop,
    double drawn_end) noexcept;

/*!
\brief Lays out the chip printing one bend point's amount, or nothing where the lane draws none.

THE ONE statement of whether a bend chip is drawn and where it stands, read by the paint core that
draws every bend chip and, through \ref tabNoteLayout and \ref tabKeyframeLayout, by the hit tester
that bounds a click on one. A point within the drawn extent wears its chip in its own column. The
first point PAST the extent wears the DESTINATION chip at the crop, where its cut leg stops (\ref
tabBendLeg), naming the amount the leg is heading for, but only where the leg changes the amount
and the note draws a tail at all; later points wear nothing. The chip sits half a tail above the
curve at the amount it prints, or above the head where its column is the onset's, placed by the one
chip rule (\ref chipLeftEdge): a chip reaching past its ring's limit stops short of the head the
ring ends on, and several such chips stack. The box is as wide as the widest amount a chip can
print, so it bounds the painted chip whatever the amount.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the chip belongs to.
\param point Index of the point in the note's \ref common::core::NoteViewState::bend.
\param drawn_end The extent the note is drawn to (\ref common::core::drawnEndSeconds).
\return The chip's layout in the lane bounds' pixel space, or nothing where no chip is drawn.
*/
[[nodiscard]] std::optional<TabChipLayout> tabBendPointChip(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note, std::size_t point,
    double drawn_end) noexcept;

/*!
\brief Computes the pixel layout of one keyframe's mark under the given lane geometry, for the
extent the note is drawn to.

THE ONE statement of where any keyframe's mark stands, whatever it states (\ref
common::core::KeyframeMark): a keyframe stating a position wears its stop's mark (\ref
tabSlideStopLayout); one changing the vibrato wears a linked head on the string line at its
instant; any other rides the bend curve, its dot where the drawn curve runs (\ref bendCurveYAt).
A keyframe stating a bend also carries the chip printing it (\ref TabKeyframeLayout::bend_chip).
Read by the paint core, the hit tester and the host's selection overlays alike, so a mark and its
target cannot part.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the keyframe belongs to; its string places the mark.
\param keyframe One of the note's \ref common::core::NoteViewState::keyframes entries.
\param drawn_end The extent the note is drawn to (\ref common::core::drawnEndSeconds).
\return Per-keyframe layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe, double drawn_end);

} // namespace rock_hero::common::ui
