/*!
\file input_calibration_view_state.h
\brief Framework-free render state for the input calibration popup.
*/

#pragma once

#include <compare>
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

    /*! \brief True when the automatic calibration button may start a capture pass. */
    bool start_measurement_enabled{true};

    /*! \brief True when manual gain slider and apply button should accept input. */
    bool manual_gain_controls_enabled{true};

    /*! \brief Text shown by the popup dismissal button. */
    std::string dismiss_button_text{"Dismiss"};

    /*!
    \brief True while the popup reflects the game's audio configuration read-only.

    A hard override on both control groups above, which stay capture-owned: the game's calibrated
    value is displayed but nothing here may change it. Carried in the state rather than injected
    into the view separately so one push resolves every control's enablement and its explanatory
    tooltip, and dropping back to the editable flow re-enables through that same path.
    */
    bool read_only{false};

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
               lhs.status_message == rhs.status_message &&
               lhs.start_measurement_enabled == rhs.start_measurement_enabled &&
               lhs.manual_gain_controls_enabled == rhs.manual_gain_controls_enabled &&
               lhs.dismiss_button_text == rhs.dismiss_button_text && lhs.read_only == rhs.read_only;
    }
};

} // namespace rock_hero::editor::core
