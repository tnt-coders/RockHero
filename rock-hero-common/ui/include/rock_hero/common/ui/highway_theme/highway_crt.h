/*!
\file highway_crt.h
\brief The CRT monitor a theme presents the 3D highway through, if any.
*/

#pragma once

#include <cstdint>

namespace rock_hero::common::ui
{

/*!
\brief Which CRT monitor a theme presents its board through, as its `crt` key names it.

Each monitor is a fixed preset of the CRT present (`fs_crt.sc`); a theme chooses one and tunes
nothing.
*/
enum class HighwayCrt : std::uint8_t
{
    /*! \brief No CRT: the board draws straight to the window. */
    Off,

    /*!
    \brief A medium-resolution arcade monitor: about 540 scan lines at the window's full width,
    tuned so every note and fret number reads as on the plain board.
    */
    Sharp,

    /*!
    \brief The sharp monitor on an early-80s board: every line's color held to 3-3-2 bits, the 256
    colors such a board could show.
    */
    Sharp256,

    /*!
    \brief A late-70s low-resolution arcade board and monitor: hard-edged square pixels about 240
    to the window's height, one scan line per row of them, and every pixel held to the eight colors
    of a board that drives each of red, green and blue on or off. Pixels step as things move;
    readability yields to the look.
    */
    Vintage,
};

} // namespace rock_hero::common::ui
