/*!
\file transport_controls_state.h
\brief Framework-free state rendered by the transport-controls widget.
*/

#pragma once

namespace rock_hero::ui
{

/*!
\brief State needed to render transport button enabledness and play/pause visuals.
*/
struct TransportControlsState
{
    /*! \brief Enables or disables the play/pause button. */
    bool play_pause_enabled{false};

    /*! \brief Enables or disables the stop button. */
    bool stop_enabled{false};

    /*! \brief Selects whether the primary button renders its pause icon variant. */
    bool play_pause_shows_pause_icon{false};

    /*!
    \brief Compares two transport-control states by value.
    \param lhs Left-hand transport-controls state.
    \param rhs Right-hand transport-controls state.
    \return True when both transport-controls states store equal values.
    */
    friend bool operator==(const TransportControlsState& lhs, const TransportControlsState& rhs) =
        default;
};

} // namespace rock_hero::ui
