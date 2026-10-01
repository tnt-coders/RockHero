/*!
\file input_calibration_view_state.h
\brief Framework-free render state for the input calibration popup.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Complete render state for the input calibration popup controls. */
struct InputCalibrationViewState
{
    /*! \brief Meter level shown for raw input after the displayed calibration gain preview. */
    common::audio::AudioMeterLevel input_meter_level;

    /*! \brief Current input gain value shown in the manual gain control. */
    double input_gain_db{0.0};

    /*! \brief Status text shown in the calibration popup. */
    std::string status_message;

    /*!
    \brief True while a measurement holds the route; the calibrate and manual-gain controls are
    locked until it ends.
    */
    bool measuring{false};

    /*! \brief Text shown by the popup dismissal button. */
    std::string dismiss_button_text;

    /*!
    \brief The chosen row of common::audio::knownInterfaces(), or empty while none is chosen: on
    opening, after the gain is changed by hand, and during and after a measurement.
    */
    std::optional<std::size_t> selected_interface;

    /*!
    \brief Compares two popup view states by their stored values.

    Hand-written, not defaulted: input_gain_db is a double of this struct's own, and a defaulted
    comparison trips -Wfloat-equal on the strict compilers once odr-used. Every field is listed;
    a new field must be added here, exactly like the sibling gated view states.

    \param lhs Left-hand view state.
    \param rhs Right-hand view state.
    \return True when both states carry equal popup render data.
    */
    friend bool operator==(
        const InputCalibrationViewState& lhs, const InputCalibrationViewState& rhs)
    {
        return lhs.input_meter_level == rhs.input_meter_level &&
               std::is_eq(lhs.input_gain_db <=> rhs.input_gain_db) &&
               lhs.status_message == rhs.status_message && lhs.measuring == rhs.measuring &&
               lhs.dismiss_button_text == rhs.dismiss_button_text &&
               lhs.selected_interface == rhs.selected_interface;
    }
};

} // namespace rock_hero::editor::core
