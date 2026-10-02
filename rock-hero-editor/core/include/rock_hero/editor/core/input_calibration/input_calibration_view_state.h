/*!
\file input_calibration_view_state.h
\brief Framework-free render state for the input calibration popup.
*/

#pragma once

#include <compare>
#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Complete render state for the input calibration popup. */
struct InputCalibrationViewState
{
    /*! \brief The gain Apply stores. */
    double gain_db{0.0};

    /*! \brief The pickups the measurement assumes; humbuckers until the player says otherwise. */
    common::audio::PickupClass pickups{common::audio::PickupClass::Humbucker};

    /*!
    \brief True while a measurement runs. The gain, the pickups and Apply then wait, and the
    calibrate button stops it.
    */
    bool measuring{false};

    /*!
    \brief The input meter: the raw input through the shown gain, or the raw input alone, as the
    measurement hears it, while one runs.
    */
    common::audio::AudioMeterLevel input_meter_level;

    /*!
    \brief Where a hard strum on the chosen pickups peaks at the right gain, marked on the
    meter; empty while a measurement runs, so nobody plays to it.
    */
    std::optional<double> meter_target_db;

    /*! \brief The popup's one message: what to do, what is happening, a result or an error. */
    std::string message;

    /*!
    \brief Compares two popup view states by their stored values.

    Hand-written, not defaulted: gain_db is a double of this struct's own, and a defaulted
    comparison trips -Wfloat-equal on the strict compilers once odr-used. Every field is listed;
    a new field must be added here.

    \param lhs Left-hand view state.
    \param rhs Right-hand view state.
    \return True when both states carry equal popup render data.
    */
    friend bool operator==(
        const InputCalibrationViewState& lhs, const InputCalibrationViewState& rhs)
    {
        return std::is_eq(lhs.gain_db <=> rhs.gain_db) && lhs.pickups == rhs.pickups &&
               lhs.measuring == rhs.measuring && lhs.input_meter_level == rhs.input_meter_level &&
               lhs.meter_target_db == rhs.meter_target_db && lhs.message == rhs.message;
    }
};

} // namespace rock_hero::editor::core
