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
\brief True when a head sits ON its harmonic node and takes the round base cell.

A node head lands between fret wires wherever the overtone lives, so the family rectangle reads
as a misaligned ordinary note there; the round base has no edge to disagree with a wire. Asks the
board's own placement rule (\ref highwayDrawnSoundingPosition, the one every 3D consumer must
ask) rather than restating its condition, so the base shape can never disagree with where the
head is actually drawn. Takes precedence over \ref highwayTechHead: the base SHAPE tracks where
the head sits, and the technique markers still stack over it.

\param note Projected note whose head is being drawn.
\return True when the round node base applies.
*/
[[nodiscard]] inline bool highwayNodeHead(const common::core::HighwayNoteView& note)
{
    return common::core::highwayDrawnSoundingPosition(note, note.fret).at_node;
}

/*!
\brief True when a head takes the darker technique base cell instead of the standard one.

Charter's base-cell selection: a head wearing a left-hand technique marker. A node head is no
longer among them: it wears its own round base (\ref highwayNodeHead), which outranks this
darkening. Neither is a scrape any more: it draws no base at all and wears the plectrum alone —
the bare-scrape experiment under sighting, whose gate lives at the renderer's base draw.

Asks \ref highwayLegatoCell rather than testing the motion again, so the base can never darken for
a claim that draws no mark (or stay light under one that does).

\param note Projected note whose head is being drawn.
\return True when the technique base cell applies.
*/
[[nodiscard]] constexpr bool highwayTechHead(const common::core::HighwayNoteView& note) noexcept
{
    return note.mute == common::core::NoteMute::Full ||
           highwayLegatoCell(note.legato) != HighwayLegatoCell::None;
}

} // namespace rock_hero::common::ui
