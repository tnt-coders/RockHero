/*!
\file transport_state.h
\brief Transport state snapshot for audio playback control.
*/

#pragma once

#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*! \brief Message-thread snapshot of the current transport playback state. */
struct TransportState
{
    /*! \brief True when the transport is currently playing. */
    bool playing{false};

    /*! \brief Current playback position in the song timeline. */
    core::TimePosition position{};

    /*! \brief Duration of the currently playable content. */
    core::TimeDuration duration{};

    /*!
    \brief Compares two transport snapshots.
    \param lhs Left-hand transport snapshot.
    \param rhs Right-hand transport snapshot.
    \return True when all stored transport fields are equal.
    */
    friend bool operator==(const TransportState& lhs, const TransportState& rhs) = default;
};

} // namespace rock_hero::audio
