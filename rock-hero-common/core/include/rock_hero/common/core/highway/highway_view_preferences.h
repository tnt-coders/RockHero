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
*/
struct HighwayViewPreferences
{
    /*! \brief True to reflect the fret axis for left-handed display. */
    bool mirrored{false};

    /*! \brief True to stack the lowest-pitched string on top instead of the bottom. */
    bool invert_string_order{true};

    /*!
    \brief Compares two preference sets by their stored fields.
    \param lhs Left-hand preferences.
    \param rhs Right-hand preferences.
    \return True when both sets store equal values.
    */
    friend constexpr bool operator==(
        const HighwayViewPreferences& lhs, const HighwayViewPreferences& rhs) noexcept = default;
};

} // namespace rock_hero::common::core
