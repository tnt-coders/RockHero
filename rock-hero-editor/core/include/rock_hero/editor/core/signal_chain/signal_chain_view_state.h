/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain view.
*/

#pragma once

#include <cstdint>
#include <optional>
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

    /*!
    \brief A saved calibration is held, but it was measured against a different physical route.

    The panel treats this exactly as \ref MissingCalibration -- the route in front of the user needs
    calibrating either way. The audio-device settings window, where calibration is reached, is the
    one surface that draws the distinction, because only there can the user act on it.
    */
    CalibrationRouteMismatch,

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

    /*!
    \brief Gain of the calibration held for the current input route, absent when none is held.

    Read by the audio-device settings window, which hosts calibration and names the selected route's
    gain on its status line. It rides beside the status it belongs to rather than in a second
    calibration state of its own.
    */
    std::optional<double> input_calibration_gain_db{};

    /*! \brief Enables or disables the manual calibrate command. */
    bool input_calibrate_enabled{false};

    /*! \brief Message shown when live guitar audition is disabled. */
    std::string disabled_message{};

    /*! \brief Enables the project-mode Import Tone command (copies a tone file's rig in). */
    bool tone_import_enabled{false};

    /*! \brief Enables the project-mode Export Tone command (writes the active rig out). */
    bool tone_export_enabled{false};

    /*!
    \brief Compares two signal-chain view states by their stored values.

    Safely defaulted: no member is a floating-point type of this struct's own, so nothing here trips
    -Wfloat-equal and a new field is compared automatically instead of depending on a
    hand-maintained member list. input_calibration_gain_db is a double inside std::optional, whose
    comparison lives in a standard library header and is not diagnosed; a bare floating-point field
    must travel inside a type carrying its own std::is_eq comparison.

    \param lhs Left-hand signal-chain view state.
    \param rhs Right-hand signal-chain view state.
    \return True when both signal-chain view states store equal values.
    */
    friend bool operator==(const SignalChainViewState& lhs, const SignalChainViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
