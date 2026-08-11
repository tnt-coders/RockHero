/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain view.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <rock_hero/editor/core/signal_chain/plugin_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Live input calibration state shown by the signal-chain view. */
enum class InputCalibrationStatus : std::uint8_t
{
    /*! \brief No usable mono input route is active. */
    NoActiveInputDevice,

    /*! \brief The active input route has no saved calibration. */
    MissingCalibration,

    /*! \brief The active input route has a saved calibration available. */
    Calibrated,

    /*! \brief A saved calibration exists but cannot currently be applied by the backend. */
    Unavailable,
};

/*! \brief State rendered by the signal-chain view. */
struct SignalChainViewState
{
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

    /*! \brief Enables the project-mode Import Tone command (copies a tone file's rig in). */
    bool tone_import_enabled{false};

    /*! \brief Enables the project-mode Export Tone command (writes the active rig out). */
    bool tone_export_enabled{false};

    /*!
    \brief Compares two signal-chain view states by their stored values.

    Hand-written rather than defaulted because output_gain_db is a plain double member of this
    struct: a defaulted comparison would compare it with `==`, which does not compile under the
    project's `-Wfloat-equal -Werror` toolchains. std::is_eq on the three-way comparison keeps exact
    equality semantics (and reports an unordered NaN as not-equal).

    \param lhs Left-hand signal-chain view state.
    \param rhs Right-hand signal-chain view state.
    \return True when both signal-chain view states store equal values.
    */
    friend bool operator==(const SignalChainViewState& lhs, const SignalChainViewState& rhs)
    {
        return lhs.insert_plugin_enabled == rhs.insert_plugin_enabled &&
               lhs.move_plugins_enabled == rhs.move_plugins_enabled &&
               lhs.remove_plugins_enabled == rhs.remove_plugins_enabled &&
               lhs.plugins == rhs.plugins &&
               lhs.input_calibration_status == rhs.input_calibration_status &&
               lhs.input_calibrate_enabled == rhs.input_calibrate_enabled &&
               lhs.disabled_message == rhs.disabled_message &&
               lhs.output_gain_controls_enabled == rhs.output_gain_controls_enabled &&
               std::is_eq(lhs.output_gain_db <=> rhs.output_gain_db) &&
               lhs.tone_import_enabled == rhs.tone_import_enabled &&
               lhs.tone_export_enabled == rhs.tone_export_enabled;
    }
};

} // namespace rock_hero::editor::core
