/*!
\file highway_view_preferences.h
\brief The player's reading choices for the 3D highway, shared by both products.
*/

#pragma once

namespace rock_hero::common::core
{

/*!
\brief How the player chooses to read the 3D highway.

Stated once and nested twice: in \ref HighwayDisplayOptions, which the renderer draws with, and in
\ref HighwayPlayerSettings, which both products persist in the shared settings file. The defaults
are the player's defaults, so a fresh install and a missing setting both read them.

Two kinds of choice live here. The mirror and the string order decide where the board lays things
out; the `show_` reading aids decide only whether one element draws. Turning an aid off
never changes what the chart says or where anything else sits.
*/
struct HighwayViewPreferences
{
    /*! \brief True to reflect the fret axis for left-handed display. */
    bool mirrored{false};

    /*!
    \brief True to stack the lowest-pitched string at the bottom, as tablature reads, instead of
    on top.
    */
    bool invert_strings{false};

    /*!
    \brief True to draw note targets: a hollow copy of each head at its landing spot on the hit
    line, growing to full size as the note arrives.
    */
    bool show_note_targets{false};

    /*!
    \brief True to roll single notes from upright to flat across their approach; false travels
    them flat the whole way, as they land. Chords never roll either way.
    */
    bool show_note_rotation{true};

    /*!
    \brief True to draw an arpeggio's posture mark: the panel at the mark's instant and the bracket
    on each string it holds. The span's rails draw either way.
    */
    bool show_arpeggio_brackets{true};

    /*!
    \brief Compares two preference sets by their stored fields.
    \param lhs Left-hand preferences.
    \param rhs Right-hand preferences.
    \return True when both sets store equal values.
    */
    friend constexpr bool operator==(
        const HighwayViewPreferences& lhs, const HighwayViewPreferences& rhs) noexcept = default;
};

/*!
\brief One on/off choice of \ref HighwayViewPreferences, named by the member that stores it.

Names a choice by the field itself, so the store can pair each one with its key and the editor can
ask for one to be set without restating the field list as a parallel enum. Never null wherever
one is passed.
*/
using HighwayViewFlag = bool HighwayViewPreferences::*;

} // namespace rock_hero::common::core
