/*!
\file live_input_sample.h
\brief One raw input reading and the calibration measurement progress it produced.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <variant>

namespace rock_hero::common::audio
{

/*!
\brief A measurement that finished: the gain it derived for the route it measured, not yet stored.

Storing is the driver's decision, through LiveInputMonitor::commitCalibration() with this route.
*/
struct [[nodiscard]] InputCalibrationMeasured
{
    /*! \brief The route the whole measurement heard; a route change interrupts it instead. */
    InputDeviceIdentity route;

    /*! \brief The calibration gain the measurement derived, in its normal form. */
    Gain gain;
};

/*! \brief A measurement after one sample: still running, finished, or ended without a gain. */
using InputCalibrationProgress =
    std::variant<InputCalibrationRunning, InputCalibrationMeasured, InputCalibrationFailure>;

/*! \brief One reading of the raw input meter, and what it did to a measurement in progress. */
struct [[nodiscard]] LiveInputSample
{
    /*! \brief Raw input peak over the window since the previous reading. */
    AudioMeterLevel raw_level;

    /*! \brief The measurement's progress, or empty when no measurement was running. */
    std::optional<InputCalibrationProgress> measurement;
};

} // namespace rock_hero::common::audio
