/*!
\file live_input_sample.h
\brief One raw input reading and the calibration measurement progress it produced.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>
#include <variant>

namespace rock_hero::common::audio
{

/*!
\brief A measurement that finished: the gain it derived, not yet stored.

Storing is the driver's decision, through LiveInputMonitor::commitCalibration().
*/
struct [[nodiscard]] InputCalibrationMeasured
{
    /*! \brief The calibration gain the measurement derived. */
    Gain gain;

    /*! \brief The pickups the measurement assumed. */
    PickupClass pickups{};
};

/*! \brief A measurement that ended with nothing stored. */
struct [[nodiscard]] InputCalibrationFailed
{
    /*! \brief Why it ended, suitable for display. */
    std::string message;
};

/*! \brief Where a running measurement is after one sample. */
using InputCalibrationProgress =
    std::variant<InputCalibrationRunning, InputCalibrationMeasured, InputCalibrationFailed>;

/*! \brief One reading of the raw input meter, and what it did to a measurement in progress. */
struct [[nodiscard]] LiveInputSample
{
    /*! \brief Raw input peak over the window since the previous reading. */
    AudioMeterLevel raw_level;

    /*! \brief The measurement's progress, or empty when no measurement was running. */
    std::optional<InputCalibrationProgress> measurement;
};

} // namespace rock_hero::common::audio
