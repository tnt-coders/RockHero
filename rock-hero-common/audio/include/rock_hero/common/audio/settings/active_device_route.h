/*!
\file active_device_route.h
\brief Active audio device route: opaque restore blob paired with the route it resolves to.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief The audio device the app should restore, plus the physical input route it opens.

The serialized state is the opaque JUCE audio-device-manager restore payload. The identity is the
semantic input route that blob resolves to, persisted alongside the blob on each successful device
apply so availability and mirroring questions can be answered offline without opening a device. The
identity is absent when the stored blob does not name a valid input channel (for example, a route
migrated from an older store whose identity is recomputed on the next successful apply).
*/
struct [[nodiscard]] ActiveDeviceRoute
{
    /*! \brief Opaque JUCE audio-device-manager restore payload. */
    std::string serialized_state;

    /*! \brief Input route the blob opens, when it names a valid input channel. */
    std::optional<InputDeviceIdentity> identity;

    /*!
    \brief Compares two active device routes by their stored values.
    \param lhs Left-hand active device route.
    \param rhs Right-hand active device route.
    \return True when both routes store equal values.
    */
    friend bool operator==(const ActiveDeviceRoute& lhs, const ActiveDeviceRoute& rhs) = default;
};

} // namespace rock_hero::common::audio
