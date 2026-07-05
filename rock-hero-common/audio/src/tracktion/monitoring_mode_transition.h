/*!
\file monitoring_mode_transition.h
\brief Testable policy for the mutually-exclusive live and calibration monitoring toggles.
*/

#pragma once

#include <cstdint>
#include <optional>

namespace rock_hero::common::audio
{

/*! \brief Which monitoring mode a toggle request targets. */
enum class MonitorChannel : std::uint8_t
{
    /*! \brief Processed live input routed through the signal chain. */
    LiveInput,

    /*! \brief Unprocessed calibration input routed directly to the output. */
    Calibration,
};

/*! \brief The two mutually-exclusive monitoring enable flags. */
struct MonitoringFlags
{
    /*! \brief Processed live input monitoring is enabled. */
    bool live_input{};

    /*! \brief Direct calibration monitoring is enabled. */
    bool calibration{};
};

/*!
\brief Computes the monitoring flags that should result from a toggle request.

The two modes are mutually exclusive: enabling one disables the other, while disabling a mode leaves
the other untouched. Enabling either mode requires an input device to route from.

\param current Current monitoring flags.
\param channel The monitoring mode the request targets.
\param enabled Whether the request enables or disables \p channel.
\param input_device_available Whether an input device is currently available to route from.
\return The flags to adopt, or empty when the request asks to enable a mode with no input device.
*/
[[nodiscard]] std::optional<MonitoringFlags> monitoringFlagsForRequest(
    MonitoringFlags current, MonitorChannel channel, bool enabled, bool input_device_available);

} // namespace rock_hero::common::audio
