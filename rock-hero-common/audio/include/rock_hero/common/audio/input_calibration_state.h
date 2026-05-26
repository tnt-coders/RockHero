/*!
\file input_calibration_state.h
\brief App-local input calibration value tied to one exact input route.
*/

#pragma once

#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/input_device_identity.h>

namespace rock_hero::common::audio
{

/*! \brief Stored input calibration gain plus the exact route it was measured against. */
struct [[nodiscard]] InputCalibrationState
{
    /*! \brief Gain applied before the live guitar signal chain after calibration succeeds. */
    Gain calibration_gain;

    /*! \brief Input route identity that must match before calibration can be applied. */
    InputDeviceIdentity input_device_identity;

    /*!
    \brief Compares two input calibration states by their stored values.
    \param lhs Left-hand input calibration state.
    \param rhs Right-hand input calibration state.
    \return True when both states store equal values.
    */
    friend bool operator==(const InputCalibrationState& lhs, const InputCalibrationState& rhs) =
        default;
};

/*!
\brief Reports whether calibration state belongs to a given exact input route.
\param state Stored calibration state.
\param identity Current input route identity.
\return True when the stored identity is valid and exactly matches the current route.
*/
[[nodiscard]] inline bool inputCalibrationMatchesIdentity(
    const InputCalibrationState& state, const InputDeviceIdentity& identity)
{
    return isValidInputDeviceIdentity(state.input_device_identity) &&
           state.input_device_identity == identity;
}

} // namespace rock_hero::common::audio
