/*!
\file transport_view_state.h
\brief Headless editor view state for transport presentation.
*/

#pragma once

#include <optional>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief State needed to render editor transport enabledness and play/pause visuals.
*/
struct TransportViewState
{
    /*!
    \brief Why Play and Stop are unavailable, or empty while both are available.

    One datum for both buttons, because they share one gate: present means both are disabled,
    and the text is the tooltip that says why. Disabled by default, like every other control
    before the controller's first push, with no reason derived yet.
    */
    std::optional<std::string> unavailable_reason{std::string{}};

    /*! \brief Selects whether the play/pause control should render a pause icon. */
    bool play_pause_shows_pause_icon{false};

    /*!
    \brief Compares two transport view states by value.
    \param lhs Left-hand transport view state.
    \param rhs Right-hand transport view state.
    \return True when both transport view states store equal values.
    */
    friend bool operator==(const TransportViewState& lhs, const TransportViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
