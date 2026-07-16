/*!
\file section_view_state.h
\brief Seconds-resolved song-structure section markers for the editor's pinned ruler.
*/

#pragma once

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
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.
    */
    friend bool operator==(const SongSectionView& lhs, const SongSectionView& rhs) = default;
};

} // namespace rock_hero::editor::core
