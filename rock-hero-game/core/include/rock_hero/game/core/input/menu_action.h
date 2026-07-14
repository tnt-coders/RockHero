/*!
\file menu_action.h
\brief The navigable menu actions a bound input trigger can produce.
*/

#pragma once

#include <cstdint>

namespace rock_hero::game::core
{

/*! \brief A menu navigation action produced by resolving a bound input trigger. */
enum class MenuAction : std::uint8_t
{
    /*! \brief Move the selection up. */
    NavigateUp,

    /*! \brief Move the selection down. */
    NavigateDown,

    /*! \brief Move the selection left. */
    NavigateLeft,

    /*! \brief Move the selection right. */
    NavigateRight,

    /*! \brief Confirm the current selection. */
    Accept,

    /*! \brief Return to the previous screen. */
    Back,

    /*! \brief Open or close the in-song pause menu. */
    PauseMenu,

    /*! \brief Request a library rescan. */
    Rescan,
};

} // namespace rock_hero::game::core
