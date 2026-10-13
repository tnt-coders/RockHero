/*!
\file tab_shape_inks.h
\brief The colors the 2D tab lane paints each kind of span with.
*/

#pragma once

#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief The inks a span's rails and its label chip are painted with in the 2D tab lane, one per kind
of span.

A theme states them beside the highway's chord and arpeggio, as the `tab` half of those roles. They
are authored, not derived: the two surfaces put a span on different grounds (the highway's lit board
under its own ambience, the lane beside the editor's chrome), and how one hue carries from one to
the other depends on what the theme means by it. Each is opaque and dark enough to carry the white
chip label, which the theme loader holds it to.
*/
struct TabShapeInks
{
    /*! \brief A chord span's ink. */
    ArgbColor chord;

    /*! \brief An arpeggio span's ink. */
    ArgbColor arpeggio;

    /*!
    \brief Returns the ink of one kind of span.
    \param is_arpeggio True for an arpeggio span; false for a chord span.
    \return The arpeggio ink or the chord ink.
    */
    [[nodiscard]] constexpr ArgbColor of(const bool is_arpeggio) const
    {
        return is_arpeggio ? arpeggio : chord;
    }
};

} // namespace rock_hero::common::ui
