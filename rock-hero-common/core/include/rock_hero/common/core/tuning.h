/*!
\file tuning.h
\brief Per-arrangement instrument tuning used to derive note pitch labels.
*/

#pragma once

#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Open-string pitches of one arrangement's instrument.

A Tuning lists the open (unfretted) pitch of each string as a scientific-pitch note name such as
"E2" or "F#2", indexed by string number minus one (open_strings[0] is string 1, the highest string).
It is persisted in the arrangement document and is used only to derive the display-only note labels
written for single-note events; string and fret remain the source of truth for what is played, so a
re-tuning never changes which notes a chart scores.
*/
struct Tuning
{
    /*! \brief Open-string note names, indexed by string number minus one (string 1 first). */
    std::vector<std::string> open_strings;

    /*!
    \brief Compares two tunings by their stored open-string names.
    \param lhs Left-hand tuning.
    \param rhs Right-hand tuning.
    \return True when both tunings list the same open-string names in the same order.
    */
    friend bool operator==(const Tuning& lhs, const Tuning& rhs) = default;
};

} // namespace rock_hero::common::core
