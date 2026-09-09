/*!
\file section_view_state.h
\brief Seconds-resolved song-structure section markers for the editor's pinned ruler.
*/

#pragma once

#include <compare>
#include <rock_hero/common/core/chart/chart.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief One song-structure section marker resolved to a timeline second.

Song-level view data, not tab data: every arrangement shares the same section list, so the
sections ride EditorViewState directly instead of the per-arrangement tab projection.
*/
struct SongSectionViewState
{
    /*! \brief Absolute timeline second the section starts at. */
    double seconds{0.0};

    /*!
    \brief Musical position the section starts at.

    Carried beside the seconds so hit-testing and the authoring verbs address a section by the
    position the song actually stores, instead of re-deriving musical time back out of pixels.
    */
    common::core::GridPosition position{};

    /*! \brief Free-form section name shown in the ruler's section lane. */
    std::string name;

    /*! \brief True when this section is the formally selected one. */
    bool selected{false};

    /*!
    \brief Compares two section views by their stored fields.

    Hand-written, not defaulted: seconds is a double of this struct's own, and a defaulted
    comparison trips -Wfloat-equal on the strict compilers once odr-used. Exact equality is
    intended — a projection rebuild reproduces bit-identical seconds.

    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.
    */
    friend bool operator==(const SongSectionViewState& lhs, const SongSectionViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.position == rhs.position &&
               lhs.name == rhs.name && lhs.selected == rhs.selected;
    }
};

} // namespace rock_hero::editor::core
