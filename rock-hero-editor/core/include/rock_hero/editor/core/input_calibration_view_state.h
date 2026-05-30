/*!
\file input_calibration_view_state.h
\brief Framework-free render state for the input calibration popup.
*/

#pragma once

#include <rock_hero/common/audio/audio_meter_snapshot.h>
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
    \brief Compares two popup view states by their stored values.
    \param lhs Left-hand view state.
    \param rhs Right-hand view state.
    \return True when both states carry equal popup render data.
    */
    friend bool operator==(
        const InputCalibrationViewState& lhs, const InputCalibrationViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
