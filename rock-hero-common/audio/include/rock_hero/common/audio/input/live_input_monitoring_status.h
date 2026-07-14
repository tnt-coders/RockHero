/*!
\file live_input_monitoring_status.h
\brief Framework-free live-input monitoring status shared by the editor and the game.
*/

#pragma once

#include <cstdint>

namespace rock_hero::common::audio
{

/*! \brief Whether processed live-input monitoring is active or gated off. */
enum class LiveInputMonitoringState : std::uint8_t
{
    /*! \brief Live-input monitoring is currently active. */
    Active,

    /*! \brief Live-input monitoring is disabled for the reason reported alongside this state. */
    Disabled,
};

/*! \brief Ordered reason a live-input monitoring request is disabled. */
enum class MonitoringDisabledReason : std::uint8_t
{
    /*! \brief Monitoring is active; no disabling reason applies. */
    None,

    /*! \brief The audio-device settings window is open. */
    AudioDeviceSettingsOpen,

    /*! \brief Arrangement audio is not ready or no arrangement is loaded. */
    SessionNotReady,

    /*! \brief No physical input route is currently identified. */
    NoInputDevice,

    /*! \brief The current route has no stored calibration. */
    MissingCalibration,

    /*! \brief Stored calibration belongs to a different physical route. */
    CalibrationRouteMismatch,

    /*! \brief The live-input backend rejected the route; a post-I/O service outcome only. */
    BackendUnavailable,

    /*! \brief The calibration store could not be reached; a post-I/O service outcome only. */
    CalibrationStoreUnavailable,
};

/*! \brief Session facts the downstream live-input monitoring service evaluates. */
struct LiveInputMonitoringContext
{
    /*! \brief True after arrangement audio and live rig restore have committed. */
    bool session_audio_ready{false};

    /*! \brief True when the session has a current arrangement. */
    bool arrangement_loaded{false};
};

/*! \brief Live-input monitoring state paired with the reason it is disabled. */
struct LiveInputMonitoringStatus
{
    /*! \brief Whether monitoring is active or disabled. */
    LiveInputMonitoringState state{LiveInputMonitoringState::Disabled};

    /*! \brief The reason monitoring is disabled, or None while active. */
    MonitoringDisabledReason reason{MonitoringDisabledReason::None};

    /*!
    \brief Compares two monitoring-status values by their stored fields.
    \param lhs Left-hand monitoring status.
    \param rhs Right-hand monitoring status.
    \return True when both statuses store equal values.
    */
    friend bool operator==(
        const LiveInputMonitoringStatus& lhs, const LiveInputMonitoringStatus& rhs) = default;
};

} // namespace rock_hero::common::audio
