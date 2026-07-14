/*!
\file menu_input_trigger.h
\brief A device-specific input signal bindable to a menu action.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*! \brief The kind of device that produced a raw menu input. */
enum class MenuInputSource : std::uint8_t
{
    /*! \brief A keyboard key. */
    Keyboard,

    /*! \brief A gamepad button. */
    Gamepad,

    /*! \brief A MIDI foot-controller trigger. */
    MidiPedal,
};

/*!
\brief One bindable raw input: a device kind plus its device-specific code.

The code is intentionally opaque to game/core — a keyboard keycode, a gamepad button, or a MIDI
note number, assigned by the input adapter — so the binding resolver stays free of any windowing or
SDL dependency. The source disambiguates codes that collide across devices. Ordered so it can key
the binding map.
*/
struct MenuInputTrigger
{
    /*! \brief Device that produced the input. */
    MenuInputSource source{MenuInputSource::Keyboard};

    /*! \brief Device-specific code (keycode, gamepad button, or MIDI note). */
    int code{0};

    /*!
    \brief Orders two triggers by device and code so they can key a binding map.

    The defaulted three-way comparison also yields equality, which the map keying and callers use.

    \param lhs Left-hand trigger.
    \param rhs Right-hand trigger.
    \return The three-way ordering of the two triggers.
    */
    friend auto operator<=>(const MenuInputTrigger& lhs, const MenuInputTrigger& rhs) = default;
};

// A small trivially-copyable value type, so the binding API takes it by value (coding conventions).
static_assert(sizeof(MenuInputTrigger) <= 16 && std::is_trivially_copyable_v<MenuInputTrigger>);

} // namespace rock_hero::game::core
