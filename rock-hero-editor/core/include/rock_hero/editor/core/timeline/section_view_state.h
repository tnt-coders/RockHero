/*!
\file section_view_state.h
\brief Seconds-resolved song-structure section markers for the editor's pinned ruler.
*/

#pragma once

#include <compare>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief One song-structure section marker resolved to a timeline second.

Song-level view data, not tab data: every arrangement shares the same section list, so the
sections ride EditorViewState directly instead of the per-arrangement tab projection.
*/
struct SongSectionView
{
    /*! \brief Absolute timeline second the section starts at. */
    double seconds{0.0};

    /*! \brief Free-form section name shown in the ruler's section lane. */
    std::string name;

    /*!
    \brief Compares two section views by their stored fields.

    Hand-written, not defaulted: seconds is a double of this struct's own, and a defaulted
    comparison trips -Wfloat-equal on the strict compilers once odr-used. Exact equality is
    intended — a projection rebuild reproduces bit-identical seconds.

    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.
    */
    friend bool operator==(const SongSectionView& lhs, const SongSectionView& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.name == rhs.name;
    }
};

} // namespace rock_hero::editor::core
