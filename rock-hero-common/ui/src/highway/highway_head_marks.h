/*!
\file highway_head_marks.h
\brief What a highway note head draws: its technique marks in order, its connection cell, its base.

Decisions the 3D draw path made inline, hoisted out for two reasons. They were each read in more
than one place — the connection cell in the open-string branch and again in the fretted one, where
the open branch restated a SUBSET of the rule — and nothing inside the renderer's draw pass is
reachable from a test, so inline they had no witness at all.

\ref highwayHeadMarks is the same class caught one level up: the two branches did not merely
restate the connection rule, they hand-wrote the whole marker LIST twice and came to different
answers about its order.
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
weight or border the way separately drawn art did.
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

Total over the motion, and shared by the open-string and fretted head paths. The open path used to
draw the pull-off alone, on the argument that a hammer-on needs a fret to strike; that holds for a
resolved claim but not for a `LeftTap`, which resolves to the hammer motion unconditionally and is
legal on an open string carrying a harmonic node — so the open path silently dropped a mark the 2D
lane drew for the same note. One authority removes the class.

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
on that rather than on an X. A node head is no longer among them: it wears its own diamond base
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
on the floor is drawn NODE-centred instead of slot-wide because of it (`harmonicMarkFootprint` in
the renderer). A head and a floor mark disagreeing about which notes are harmonics is precisely
the two-spellings defect, so they read one predicate.

Both exclusions are the shared chart authorities rather than named attacks. A PINCH is out
because \ref common::core::nodeIsOnNeck is: its node is over the body where the thumb grazes, so
neither the neck's fret axis nor a floor light has anywhere to put it, and it wears its own cell
instead. A SCRAPE is out because a pick slide's node is the in-memory latent its attack toggle
preserves rather than a touch anybody makes — `chart.h` records the two separate failures that
reading it as one produced.

\param note Projected note being drawn.
\return True when the note is a harmonic the board points at.
*/
[[nodiscard]] inline bool highwayHarmonicMark(const common::core::NoteViewState& note) noexcept
{
    return note.harmonic_node.has_value() && common::core::nodeIsOnNeck(note.attack) &&
           !common::core::isScrape(note.attack);
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
allocate per note: a head stacks at most a palm mark, ONE hand mark (tap, slap, pop and pinch are
all the same `attack` slot, so they are mutually exclusive), a connection cell, a harmonic, and the
deadening X. Five, exactly.
*/
struct HighwayHeadMarkStack
{
    /*! \brief Palm, hand, connection, harmonic, deadening — the provable maximum. */
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
string's overlay and the fretted head alike. It used to be two hand-written lists, and they had
already diverged: the open branch drew the connection cell FIRST, underneath everything, while the
fretted branch drew it fifth, and the fretted branch drew the harmonic at the very bottom where the
open branch drew no harmonic at all. Both were "the marker order", written twice, answered
differently — this project's recurring defect rather than a matter of taste.

The order is DERIVED, not authored, from how much of the note's identity each mark overrides.
A palm mute only shades the tone; a hand mark says how the string was struck; a connection says
whether it was struck at all; a harmonic says the pitch is not the fretted one; and the deadening X
says there is no pitch. Each claim swallows the one before it, so each draws over the one before
it, and the X — the mark that must survive intact, because a broken X reads as a different mark
entirely — lands on top by construction rather than by special case.

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

    if (note.palm_mute)
    {
        add(g_head_cell_palm_mute, true);
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

    if (note.attack == common::core::NoteAttack::Pinch)
    {
        add(g_head_cell_pinch_harmonic, true);
    }
    else if (highwayHarmonicMark(note))
    {
        add(g_head_cell_harmonic, true);
    }

    if (note.dead)
    {
        add(g_head_cell_full_mute, false);
    }

    return stack;
}

} // namespace rock_hero::common::ui
