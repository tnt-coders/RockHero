/*!
\file transport_view_state.h
\brief Headless editor view state for transport presentation.
*/

#pragma once

namespace rock_hero::editor::core
{

/*!
\brief State needed to render editor transport enabledness and play/pause visuals.
*/
struct TransportViewState
{
    /*! \brief Enables or disables the play/pause command. */
    bool play_pause_enabled{false};

    /*! \brief Enables or disables the stop command. */
    bool stop_enabled{false};

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
