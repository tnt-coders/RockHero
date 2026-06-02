/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain panel.
*/

#pragma once

#include <rock_hero/editor/core/plugin_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Live input calibration state shown by the signal-chain panel. */
enum class InputCalibrationStatus
{
    NoActiveInputDevice,
    MissingCalibration,
    Calibrated,
    Unavailable,
};

/*! \brief State rendered by the signal-chain panel. */
struct SignalChainViewState
{
    /*! \brief Enables or disables the add-plugin command. */
    bool add_plugin_enabled{false};

    /*! \brief Enables or disables gap insert commands. */
    bool insert_plugin_enabled{false};

    /*! \brief Enables or disables plugin move commands. */
    bool move_plugins_enabled{false};

    /*! \brief Enables or disables plugin removal commands. */
    bool remove_plugins_enabled{false};

    /*! \brief Current linear plugin chain. */
    std::vector<PluginViewState> plugins{};

    /*! \brief Live input calibration status for the current input route. */
    InputCalibrationStatus input_calibration_status{InputCalibrationStatus::NoActiveInputDevice};

    /*! \brief Enables or disables the manual calibrate command. */
    bool input_calibrate_enabled{false};

    /*! \brief Message shown when live guitar audition is disabled. */
    std::string disabled_message{};

    /*! \brief Enables or disables the output gain control. */
    bool output_gain_controls_enabled{false};

    /*! \brief Current output gain in decibels, after the signal chain. */
    double output_gain_db{0.0};

    /*!
    \brief Compares two signal-chain view states by their stored values.
    \param lhs Left-hand signal-chain view state.
    \param rhs Right-hand signal-chain view state.
    \return True when both signal-chain view states store equal values.
    */
    friend bool operator==(const SignalChainViewState& lhs, const SignalChainViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
