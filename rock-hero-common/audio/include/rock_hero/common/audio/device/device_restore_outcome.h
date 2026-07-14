/*!
\file device_restore_outcome.h
\brief Success outcomes of applying a saved audio-device route under the no-fallback policy.
*/

#pragma once

#include <cstdint>

namespace rock_hero::common::audio
{

/*!
\brief Which designed outcome a successful serialized device-route restore produced.

Under the no-fallback policy, applying a saved route has two designed outcomes: the saved device
opened, or the saved device was unavailable and the route was applied as a closed device with the
user's choice retained. Both are successes of the restore operation; the error channel is reserved
for genuine faults such as unparseable state or a wrong-thread call. Carrying the distinction in the
value channel keeps the two outcomes impossible to conflate without asking callers to interpret
error codes for expected behavior.
*/
enum class DeviceRestoreOutcome : std::uint8_t
{
    /*! \brief The saved route was applied and the saved device is open. */
    Opened,

    /*!
    \brief The saved route was applied but the saved device is absent or could not be opened (for
           example, held by another application), so the device was left closed and the saved
           choice retained.
    */
    DeviceUnavailable,
};

} // namespace rock_hero::common::audio
