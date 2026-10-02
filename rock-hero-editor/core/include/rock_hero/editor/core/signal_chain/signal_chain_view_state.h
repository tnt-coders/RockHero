/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain view.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/audio/shared/gain.h>
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

    /*! \brief The calibration store or the live-input backend failed for the active route. */
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

    /*! \brief Enables or disables opening a plugin's editor. */
    bool open_plugins_enabled{false};

    /*! \brief Enables or disables the plugin display-type override menu. */
    bool display_type_override_enabled{false};

    /*! \brief Current linear plugin chain. */
    std::vector<PluginViewState> plugins{};

    /*!
    \brief Why the chain is unavailable, drawn over it at full strength while another window holds
    it; empty when there is nothing to explain. Set only while every chain verb is unavailable:
    the notice's plate covers the tiles.
    */
    std::string chain_notice{};

    /*! \brief Live input calibration status for the current input route. */
    InputCalibrationStatus input_calibration_status{InputCalibrationStatus::NoActiveInputDevice};

    /*! \brief Enables or disables the manual calibrate command. */
    bool input_calibrate_enabled{false};

    /*!
    \brief Why the current input route cannot play live (no device, no calibration, a fault), or
    empty when it can. A session holding live input off for a moment (the settings window open,
    the project's audio loading) is not a route problem and leaves it empty.
    */
    std::string disabled_message{};

    /*! \brief Enables or disables the output gain control. */
    bool output_gain_controls_enabled{false};

    /*! \brief Current output gain after the signal chain. */
    common::audio::Gain output_gain{};

    /*! \brief Enables the project-mode Import Tone command (copies a tone file's rig in). */
    bool tone_import_enabled{false};

    /*! \brief Enables the project-mode Export Tone command (writes the active rig out). */
    bool tone_export_enabled{false};

    /*!
    \brief Compares two signal-chain view states by their stored values.

    Safely defaulted: the one float travels inside \ref common::audio::Gain, whose hand-written
    std::is_eq comparison keeps -Wfloat-equal quiet — so a new field here is compared
    automatically instead of depending on a hand-maintained member list.

    \param lhs Left-hand signal-chain view state.
    \param rhs Right-hand signal-chain view state.
    \return True when both signal-chain view states store equal values.
    */
    friend bool operator==(const SignalChainViewState& lhs, const SignalChainViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
