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
\brief Answers whether one hand-posture span, by its index, is REVEALED, for a host that reveals.

The furniture pass's pick: a revealed span's furniture runs to its musical close
(\ref common::core::ShapeViewState::close_seconds) instead of the extent rule 12a trimmed. A host
derives the answer from a predicate of its own; the core is told the answer and never the reason.
A note's reveal is part of its presence instead (\ref TabNotePresence).

An empty accessor is the ordinary case and reveals nothing (\ref tabSpanRevealed), which is the
whole answer for a surface with no reveal at all: the game's tab strips.
*/
using TabSpanRevealed = std::function<bool(std::size_t index)>;

/*!
\brief How a host presents one note right now: how far its reveal has run, and whether it has
stepped back behind the note the charter is editing.

THE EDITOR'S FOCUS, handed to the paint core, its overlays and the hit tester as one answer per
note, so the picture and the reachable marks agree. A revealed note is drawn to its ring's end
(\ref common::core::drawnEndSeconds), every keyframe it stores at its true instant and its
reveal-only marks with it (\ref common::core::stopMarkShown). A host easing its presence hands the
painter the eased amounts, so a revealed tail GROWS from its crop and every mark riding it travels
with it, and a head stepping back fades as it goes; what a press reaches reads the settled answer,
the state the ease is heading for. A surface without an editor (the game's tab strips) presents
every note plainly (\ref tabPresence).
*/
struct TabNotePresence
{
    /*!
    \brief How far the note's reveal has run: 0 crops it at its ink end, 1 draws it to its ring's
    end, and a value between draws it that far along the stretch the reveal adds (\ref
    drawnExtentSeconds).
    */
    float reveal{};

    /*!
    \brief How far the note has STEPPED BACK, 0 in front to 1 behind: its head is the one a ring in
    the editor's focus ends on, so the note is drawn faint and beneath that ring, and the ring being
    edited reads whole over it.
    */
    float recede{};

    /*!
    \brief Whether the note's reveal has begun: its reveal-only marks are in, and its tail no longer
    fades at the crop, the extent alone easing.
    \return True while \ref reveal is above 0.
    */
    [[nodiscard]] bool revealing() const noexcept
    {
        return reveal > 0.0f;
    }

    /*!
    \brief Whether the note has begun stepping back: it is drawn as one faint group beneath every
    note in front, and answers a press after them.
    \return True while \ref recede is above 0.
    */
    [[nodiscard]] bool receded() const noexcept
    {
        return recede > 0.0f;
    }
};

/*! \brief A host's per-note presence, by projection index (\ref TabNotePresence). */
using TabPresence = std::function<TabNotePresence(std::size_t index)>;

/*!
\brief Reads one note's presence, an empty accessor presenting every note plainly: cropped and in
front.
\param presence The host's answers, possibly empty.
\param index The note's index.
\return The note's presence.
*/
[[nodiscard]] inline TabNotePresence tabPresence(
    const TabPresence& presence, const std::size_t index)
{
    return presence ? presence(index) : TabNotePresence{};
}

/*!
\brief Whether the head a note's ring ends on stands in front, which a ring ending anywhere else
answers trivially: that head's own presence is the one statement, so while it steps back the marks
at the ring's end draw in truth over it (\ref tabKeyframeLayout).
\param presence The host's answers, possibly empty.
\param note The note whose ring's end is asked about.
\return False only while the head it ends on has stepped back.
*/
[[nodiscard]] inline bool tabEndHeadInFront(
    const TabPresence& presence, const common::core::NoteViewState& note)
{
    const std::optional<std::size_t>& end_head = note.end_head;
    return !end_head.has_value() || !tabPresence(presence, *end_head).receded();
}

/*!
\brief The extent a note is drawn to at a reveal amount: its ink end at 0, its ring's end at 1
(\ref common::core::drawnEndSeconds, exactly), and the stretch between in proportion.
\param note The note.
\param amount How far its reveal has run (\ref TabNotePresence::reveal).
\return The drawn extent in seconds.
*/
[[nodiscard]] inline double drawnExtentSeconds(
    const common::core::NoteViewState& note, const float amount) noexcept
{
    const double cropped = common::core::drawnEndSeconds(note, false);
    const double revealed = common::core::drawnEndSeconds(note, true);
    if (amount <= 0.0f)
    {
        return cropped;
    }
    if (amount >= 1.0f)
    {
        return revealed;
    }
    return cropped + ((revealed - cropped) * static_cast<double>(amount));
}

/*!
\brief Reads a span's reveal answer, an empty accessor revealing nothing.
\param revealed The host's answer, possibly empty.
\param index The span's index.
\return True when the host reveals that span.
*/
[[nodiscard]] inline bool tabSpanRevealed(const TabSpanRevealed& revealed, const std::size_t index)
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
    \brief Box of the chip printing the onset's own bend above the head (\ref tabBendPointChipBox),
    or nothing where the note's curve does not open at its onset or the lane prints no text.

    The note's SECOND FACE, on the rule every chip follows: a chip is a face of what owns it, so a
    click on it reaches the note and a selection of the note rings it.
    */
    std::optional<TabLayoutRect> bend_chip{};
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

    /*! \brief Bounding rectangle of the satellite slot: its drawn extent. */
    TabLayoutRect box{};

    /*!
    \brief The digit's own cell inside the slot, where the painter prints it: the column between
    the slot's gaps, as tall as a chip's plate on the string line.
    */
    TabLayoutRect digit{};
};

/*!
\brief The satellite slot and digit cell beside a bracket closing at `mark_x` on the line at
`center_y` — THE one statement of that geometry, which the painter prints inside.

\param geometry Lane geometry.
\param mark_x The instant's x the bracket is centred on.
\param center_y The string line's y.
\return The slot's layout.
*/
[[nodiscard]] TabHeldStopLayout tabSatelliteLayoutAt(
    const TabLaneGeometry& geometry, float mark_x, float center_y) noexcept;

/*!
\brief Computes the pixel layout of one note's held-stop satellite, when it draws one.

The second stop a note states (\ref common::core::NoteViewState::stop_mark) prints in its own
column outboard of the head's own bracket columns, because the head's centre is already carrying
what that head SOUNDS. It is display-only: every held stop is derived. THREE populations wear one:
the stop under a plain tap or a scrape (its plant, else the covering grip), whose own fret is the
picking hand's; the PRESSED stop of a harmonic whose head prints its node instead
(\ref common::core::harmonicOverPressedStop) — the artificial one and the tapped one alike; and the
stop a pull-off PLANTS beneath a note the picking hand does NOT stop the string for (THE PLANT'S
FACE), which the bracket then prints nothing of on that string, so exactly one ink states it either
way.

The resolved mark says whether its digit is SHOWN and where. A stop whose face waits for the reveal
(\ref common::core::StopMarkFace::Revealed) lays out to nothing until `revealed` says its note's
truth is on show — the same per-note pick that draws the note to its ring end, asked here through
\ref common::core::stopMarkShown.

The vertical extent is the bracket's own, so the two halves of a bracketed mark present the same
height. The column sits outboard of the closing bar, so it never overlaps the bars or a digit
centred between them.

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
    would reach into the square of the head its ring ends on while that head stands in front (\ref
    nextHeadLeftEdge), whose chip is its only face there; past the extent only the destination chip
    a cut slide leg wears at the crop is drawn, and it names this keyframe and so reaches it.
    */
    bool mark_drawn{};

    /*!
    \brief Box of the chip printing the bend this keyframe states (\ref tabBendPointChipBox), or
    nothing where the lane draws none: the keyframe states no bend, the lane prints no text, or its
    point stands past the extent without wearing the destination chip.

    The keyframe's SECOND FACE: a click on it reaches the keyframe as a click on the mark does, and
    a selection rings both.
    */
    std::optional<TabLayoutRect> bend_chip{};
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
a note whose ink stops at its onset, or a lane that prints no text, draws none at all. The painted
chip centres on the box, which is wide enough for any label it prints.

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
\brief Computes the box of the chip printing one bend point's amount, or nothing where the lane
draws none.

THE ONE statement of whether a bend chip is drawn and where it stands, read by the paint core that
draws every bend chip and, through \ref tabNoteLayout and \ref tabKeyframeLayout, by the hit tester
that bounds a click on one. A point within the drawn extent wears its chip centred on its own
column: the truth, wherever it falls. The first point PAST the extent wears the DESTINATION chip
centred at the crop, where its cut leg stops (\ref tabBendLeg), naming the amount the leg is heading
for, but only where the leg changes the amount and the note draws a tail at all; later points wear
nothing. So as a reveal runs the extent on, the destination chip rides the leg's end to its point.
The chip sits half a tail above the curve at the amount it prints, or above the head where its
column wears one: the onset's, or the point's own keyframe head (a linked stop, a resting keyframe),
whose digit a chip on the string line would cover. The box is as wide as the widest amount a chip
can print, so it bounds the painted chip, which centres on it, whatever the amount.

\param geometry Lane geometry the notation was painted with.
\param note Seconds-resolved note the chip belongs to.
\param point Index of the point in the note's \ref common::core::NoteViewState::bend.
\param drawn_end The extent the note is drawn to (\ref drawnExtentSeconds).
\return The chip's box in the lane bounds' pixel space, or nothing where no chip is drawn.
*/
[[nodiscard]] std::optional<TabLayoutRect> tabBendPointChipBox(
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
\param drawn_end The extent the note is drawn to (\ref drawnExtentSeconds).
\param end_head_in_front False where the head the note's ring ends on has stepped back for it
       (\ref tabEndHeadInFront), so a dot at the ring's end draws in truth over it.
\return Per-keyframe layout in the lane bounds' pixel space.
*/
[[nodiscard]] TabKeyframeLayout tabKeyframeLayout(
    const TabLaneGeometry& geometry, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe, double drawn_end, bool end_head_in_front);

} // namespace rock_hero::common::ui
