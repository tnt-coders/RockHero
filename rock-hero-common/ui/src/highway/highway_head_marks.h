/*!
\file highway_head_marks.h
\brief What a highway note head draws: its technique marks in order, its connection cell, its base.

Decisions the 3D draw path makes for a note head, kept out of the draw pass for two reasons. Each
is asked from more than one place — the open-string head path and the fretted one — and nothing
inside the renderer's draw pass is reachable from a test, so out here they gain a witness.

One authority per decision keeps the two paths from restating a rule and answering differently;
\ref highwayHeadMarks applies that one level up, to the whole marker LIST and its order.
*/

#pragma once

#include "highway/highway_atlas.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/highway/highway_view_state.h>

namespace rock_hero::common::ui
{

/*!
\brief What a head does with the shared legato atlas cell.

The hammer-on and the pull-off are ONE cell drawn two ways, so the pair can never drift apart in
weight or border the way two separately authored cells would.
*/
enum class HighwayLegatoCell : std::uint8_t
{
    /*! \brief Nothing drawn. */
    None,

    /*! \brief The cell as authored: a hammer-on. */
    Upright,

    /*! \brief The cell flipped vertically: a pull-off. */
    Flipped
};

/*!
\brief Maps a note's RESOLVED connection motion onto the cell its head draws.

Driven by the resolution, never by the stored attack — the chart records a claim and the direction
is read back from the predecessor — so a claim nothing justifies draws neither cell, exactly like
the plain pick it plays as.

Total over the motion, and shared by the open-string and fretted head paths. An open path drawing
the pull-off alone — on the argument that a hammer-on needs a fret to strike — holds for a resolved
claim but not for a `LeftTap`, which resolves to the hammer motion unconditionally and is legal on
an open string carrying a harmonic node, so it would silently drop a mark the 2D lane draws for the
same note. One authority removes the class.

\param motion The note's resolved connection motion (\ref common::core::LegatoMotion).
\return The cell treatment, or `None` when the head draws no connection mark.
*/
[[nodiscard]] constexpr HighwayLegatoCell highwayLegatoCell(
    common::core::LegatoMotion motion) noexcept
{
    switch (motion)
    {
        case common::core::LegatoMotion::Hammer:
        {
            return HighwayLegatoCell::Upright;
        }
        case common::core::LegatoMotion::Pull:
        {
            return HighwayLegatoCell::Flipped;
        }
        case common::core::LegatoMotion::Unjustified:
        {
            break;
        }
    }
    return HighwayLegatoCell::None;
}

/*!
\brief True when a head sits ON its harmonic node and takes the diamond base cell.

A node head lands between fret wires wherever the overtone lives, so the family rectangle reads
as a misaligned ordinary note there; the diamond base has no edge to disagree with a wire. Asks the
board's own placement rule (\ref highwayDrawnStop, the one every 3D consumer must ask) rather than
restating its condition, so the base shape can never disagree with where the head is actually
drawn. Takes precedence over \ref highwayTechHead: the base SHAPE tracks where the head sits, and
the technique markers still stack over it.

\param note Projected note whose head is being drawn.
\return True when the diamond node base applies.
*/
[[nodiscard]] inline bool highwayNodeHead(const common::core::NoteViewState& note)
{
    return common::core::highwayDrawnStop(note, note.fret).node.has_value();
}

/*!
\brief True when a head takes the darker technique base cell instead of the standard one.

Charter's base-cell selection: a head wearing a left-hand technique marker, and a scrape — whose
travel is unpitched noise, so it takes the base a dead note takes and lets its pick mark sit
on that rather than on an X. A node head is not among them: it wears its own diamond base
(\ref highwayNodeHead), which outranks this darkening.

Asks \ref highwayLegatoCell rather than testing the motion again, so the base can never darken for
a claim that draws no mark (or stay light under one that does).

Keyed on the dead flag ALONE, never on the pair: the base says what the note sounds like, and a
note that is also palm muted sounds dead (`ChartNote::dead`), so it takes the dead base and its
palm marker stacks over that. A palm mute on its own leaves the head light — it is still pitched.

\param note Projected note whose head is being drawn.
\return True when the technique base cell applies.
*/
[[nodiscard]] constexpr bool highwayTechHead(const common::core::NoteViewState& note) noexcept
{
    return note.dead || highwayLegatoCell(note.legato) != HighwayLegatoCell::None ||
           common::core::isScrape(note.attack);
}

/*!
\brief True when the board calls a note a harmonic and can point at its node on the neck.

Two things on this board turn on that one fact and neither may answer it for itself: the head
wears the harmonic cell because of it (\ref highwayHeadMarks below), and the note's fret-span line
on the floor runs from the stop to the node rather than spanning its fret slot because of it
(`harmonicMarkFootprint` in the renderer), node-centred where the stop IS the node. A head and a
floor mark disagreeing about which notes are harmonics is precisely the two-spellings defect, so
they read one predicate.

Both halves are the shared chart authorities rather than named attacks. WHETHER the note is a
harmonic is \ref common::core::isHarmonic, the claim the 2D diamond also reads, which is where a
scrape's latent node is refused. Whether the board can POINT at it is
\ref common::core::nodeIsOnNeck: a pinch's node is over the body where the thumb grazes, so neither
the neck's fret axis nor a floor light has anywhere to put it, and it wears its own cell instead.

\param note Projected note being drawn.
\return True when the note is a harmonic the board points at.
*/
[[nodiscard]] constexpr bool highwayHarmonicMark(const common::core::NoteViewState& note) noexcept
{
    return common::core::isHarmonic(note.harmonic_node, note.attack) &&
           common::core::nodeIsOnNeck(note.attack);
}

/*!
\brief One technique mark stacked on a note head.
*/
struct HighwayHeadMark
{
    /*! \brief Atlas cell this mark draws. */
    int cell{};

    /*! \brief True when the mark turns with the head's rolling flip rather than staying upright. */
    bool rides_roll{};

    /*! \brief True when the cell draws mirrored vertically (the connection cell's pull-off). */
    bool flipped{};
};

/*!
\brief The technique marks a head wears, in draw order: front is lowest, back is on top.

Fixed capacity because the maximum is provable rather than guessed, and the render path may not
allocate per note: a head stacks at most ONE hand mark (tap, slap and pop share the `attack` slot,
so they are mutually exclusive), a connection cell, a harmonic (the node cell or the pinch cell —
a pinch draws on this rung, not the hand rung), the palm mark, and the deadening X. Five, exactly.
*/
struct HighwayHeadMarkStack
{
    /*! \brief Hand, connection, harmonic, palm, deadening — the provable maximum. */
    static constexpr std::size_t g_capacity = 5;

    /*! \brief Marks in draw order; only the first \ref count entries are populated. */
    std::array<HighwayHeadMark, g_capacity> marks{};

    /*! \brief How many of \ref marks this head actually wears. */
    std::size_t count{};

    /*! \brief Range begin, so a caller can draw the stack with a plain range-for. */
    [[nodiscard]] const HighwayHeadMark* begin() const noexcept
    {
        return marks.data();
    }

    /*! \brief Range end at \ref count, not at capacity. */
    [[nodiscard]] const HighwayHeadMark* end() const noexcept
    {
        return marks.data() + count;
    }
};

/*!
\brief Builds the ordered technique-mark stack for one head, lowest mark first.

THE draw order for note-head technique marks, asked by every head the highway draws — the open
string's overlay and the fretted head alike. One authority rather than a hand-written list per
path: two lists are both "the marker order", written twice and free to answer differently about
where the connection cell and the harmonic sit in it — this project's recurring defect rather than
a matter of taste.

Below the mutes the order follows how much of the note's identity each mark overrides: a hand
mark says how the string was struck, a connection says whether it was struck at all, and a
harmonic says the pitch is not the fretted one. Each claim swallows the one before it, so each
draws over the one before it.

The mutes cap the stack: palm mute, then the deadening X on top. A mute is stated over whatever
the hand did, which is how the 2D lane paints it too (its mute X lands after the pinch bar), so
both surfaces state one stack; drawn under the pinch cell, a palm mute would be hidden by the
squeal mark. The X stays topmost because a broken X reads as a different mark entirely.

A scrape is not a rank in that ladder but a category of one, and that is a chart rule rather than a
layering preference: `chart_rules.cpp` validates a pick-slide note against `savedChartNote(note) ==
note`, a fixpoint that strips every mute, node, vibrato, tremolo and bend, while its attack slot is
already spent on `PickSlide`. Nothing can stack with it, so it returns before the ladder starts.

Rotation travels with each mark instead of being re-derived at the call site (\ref
HighwayHeadMark::rides_roll), because the ladder interleaves the two kinds: the harmonic rides the
head's flip and the connection cell below it does not. The open-string bar has no flip, so it
ignores the flag and draws every mark upright.

\param note Projected note whose head is being drawn.
\return The marks in draw order; empty when the head wears none.
*/
[[nodiscard]] inline HighwayHeadMarkStack highwayHeadMarks(const common::core::NoteViewState& note)
{
    HighwayHeadMarkStack stack;
    const auto add = [&stack](const int cell, const bool rides_roll, const bool flipped = false) {
        stack.marks.at(stack.count) =
            HighwayHeadMark{.cell = cell, .rides_roll = rides_roll, .flipped = flipped};
        ++stack.count;
    };

    if (common::core::isScrape(note.attack))
    {
        // The pick mark seats concentric on the head and covers its whole footprint, so it needs
        // nothing under it and admits nothing over it.
        add(g_head_cell_pick_slide, false);
        return stack;
    }

    if (note.attack == common::core::NoteAttack::Tap)
    {
        add(g_head_cell_tap, true);
    }
    else if (note.attack == common::core::NoteAttack::Slap)
    {
        add(g_head_cell_slap, true);
    }
    else if (note.attack == common::core::NoteAttack::Pop)
    {
        add(g_head_cell_pop, true);
    }

    if (const HighwayLegatoCell legato_cell = highwayLegatoCell(note.legato);
        legato_cell != HighwayLegatoCell::None)
    {
        add(g_head_cell_legato, false, legato_cell == HighwayLegatoCell::Flipped);
    }

    // One rung for one claim: a harmonic wears a harmonic cell, and which one says whether the
    // board can point at its node or the thumb is grazing it over the body.
    if (common::core::isHarmonic(note.harmonic_node, note.attack))
    {
        add(highwayHarmonicMark(note) ? g_head_cell_harmonic : g_head_cell_pinch_harmonic, true);
    }

    if (note.palm_mute)
    {
        add(g_head_cell_palm_mute, true);
    }

    if (note.dead)
    {
        add(g_head_cell_full_mute, false);
    }

    return stack;
}

} // namespace rock_hero::common::ui
