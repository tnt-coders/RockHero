/*!
\file transport_status.h
\brief Coarse transport status for audio playback control.
*/

#pragma once

#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*! \brief Message-thread snapshot of coarse transport playback status. */
struct TransportStatus
{
    /*! \brief True when the transport is currently playing. */
    bool playing{false};

    /*! \brief Duration of the currently playable content. */
    core::TimeDuration duration{};

    /*!
    \brief Compares two transport status snapshots.
    \param lhs Left-hand transport status snapshot.
    \param rhs Right-hand transport status snapshot.
    \return True when all stored status fields are equal.
    */
    friend bool operator==(const TransportStatus& lhs, const TransportStatus& rhs) = default;
};

} // namespace rock_hero::audio
