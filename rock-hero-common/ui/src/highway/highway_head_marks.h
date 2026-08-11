/*!
\file highway_head_marks.h
\brief What a highway note head draws for its resolved connection, and when it takes the tech base.

Two decisions the 3D draw path made inline, hoisted out for two reasons. They were each read in more
than one place — the connection cell in the open-string branch and again in the fretted one, where
the open branch restated a SUBSET of the rule — and nothing inside the renderer's draw pass is
reachable from a test, so inline they had no witness at all.
*/

#pragma once

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
    const common::core::LegatoMotion motion) noexcept
{
    switch (motion)
    {
        case common::core::LegatoMotion::Hammer:
            return HighwayLegatoCell::Upright;
        case common::core::LegatoMotion::Pull:
            return HighwayLegatoCell::Flipped;
        case common::core::LegatoMotion::Unjustified:
            break;
    }
    return HighwayLegatoCell::None;
}

/*!
\brief True when a head takes the darker technique base cell instead of the standard one.

Charter's base-cell selection: a head wearing a left-hand technique marker, and a scrape — whose
travel is unpitched noise, so it takes the base a full-muted note takes and lets its pick mark sit
on that rather than on an X.

Asks \ref highwayLegatoCell rather than testing the motion again, so the base can never darken for
a claim that draws no mark (or stay light under one that does).

\param note Projected note whose head is being drawn.
\return True when the technique base cell applies.
*/
[[nodiscard]] constexpr bool highwayTechHead(const common::core::HighwayNoteView& note) noexcept
{
    return note.mute == common::core::NoteMute::Full ||
           (note.harmonic_node.has_value() && common::core::nodeIsOnNeck(note.attack)) ||
           highwayLegatoCell(note.legato) != HighwayLegatoCell::None ||
           note.attack == common::core::NoteAttack::PickSlide;
}

} // namespace rock_hero::common::ui
