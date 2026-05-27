/*!
\file input_calibration_view_state.h
\brief Framework-free state rendered by the input calibration popup.
*/

#pragma once

#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Describes an active input calibration prompt requested by the editor controller. */
struct InputCalibrationPrompt
{
    /*! \brief Message shown by the calibration prompt. */
    std::string message;

    /*! \brief Input gain currently displayed by the calibration prompt. */
    double input_gain_db{0.0};

    /*!
    \brief Compares two input calibration prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const InputCalibrationPrompt& lhs, const InputCalibrationPrompt& rhs) =
        default;
};

/*! \brief State rendered by the input calibration popup. */
struct InputCalibrationViewState
{
    /*! \brief Target text shown above the live meter. */
    std::string target_text;

    /*! \brief Current status, instruction, or error text. */
    std::string status_text;

    /*! \brief Meter level after applying the currently displayed preview gain. */
    common::audio::AudioMeterLevel input_level;

    /*! \brief Gain value shown by the manual gain control. */
    double input_gain_db{0.0};

    /*! \brief Enables or disables the automatic calibration button. */
    bool calibrate_enabled{true};

    /*! \brief Enables or disables the manual gain slider. */
    bool manual_gain_enabled{true};

    /*! \brief Enables or disables the manual apply button. */
    bool manual_apply_enabled{true};

    /*! \brief Text shown on the close/dismiss button. */
    std::string close_button_text{"Dismiss"};

    /*!
    \brief Compares two input calibration view states by their stored values.
    \param lhs Left-hand input calibration view state.
    \param rhs Right-hand input calibration view state.
    \return True when both view states store equal values.
    */
    friend bool operator==(
        const InputCalibrationViewState& lhs, const InputCalibrationViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
