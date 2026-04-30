/*!
\file transport_state.h
\brief Coarse transport state for audio playback control.
*/

#pragma once

#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*! \brief Message-thread snapshot of coarse transport playback state. */
struct TransportState
{
    /*! \brief True when the transport is currently playing. */
    bool playing{false};

    /*! \brief Duration of the currently playable content. */
    core::TimeDuration duration{};

    /*!
    \brief Compares two transport state snapshots.
    \param lhs Left-hand transport state snapshot.
    \param rhs Right-hand transport state snapshot.
    \return True when all stored state fields are equal.
    */
    friend bool operator==(const TransportState& lhs, const TransportState& rhs) = default;
};

} // namespace rock_hero::audio
