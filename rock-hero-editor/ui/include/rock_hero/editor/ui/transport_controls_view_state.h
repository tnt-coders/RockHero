/*!
\file transport_controls_view_state.h
\brief Framework-free state rendered by the transport-controls widget.
*/

#pragma once

namespace rock_hero::editor::ui
{

/*!
\brief State needed to render transport button enabledness and play/pause visuals.
*/
struct TransportControlsViewState
{
    /*! \brief Enables or disables the play/pause button. */
    bool play_pause_enabled{false};

    /*! \brief Enables or disables the stop button. */
    bool stop_enabled{false};

    /*! \brief Selects whether the primary button renders its pause icon variant. */
    bool play_pause_shows_pause_icon{false};

    /*!
    \brief Compares two transport-controls view states by value.
    \param lhs Left-hand transport-controls view state.
    \param rhs Right-hand transport-controls view state.
    \return True when both transport-controls view states store equal values.
    */
    friend bool operator==(
        const TransportControlsViewState& lhs, const TransportControlsViewState& rhs) = default;
};

} // namespace rock_hero::editor::ui
