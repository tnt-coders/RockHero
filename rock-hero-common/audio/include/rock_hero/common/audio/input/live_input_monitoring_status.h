/*!
\file live_input_monitoring_status.h
\brief Framework-free live-input monitoring status shared by the editor and the game.
*/

#pragma once

#include <cstdint>
#include <string_view>

namespace rock_hero::common::audio
{

/*! \brief What the live input is doing now, or the first reason in gate order it is off. */
enum class LiveInputMonitoringStatus : std::uint8_t
{
    /*! \brief The calibrated route plays through the processed rig. */
    Active,

    /*! \brief A calibration measurement holds the route; processed monitoring is off. */
    Measuring,

    /*! \brief The calibration store could not be read. */
    CalibrationStoreUnavailable,

    /*! \brief The product's session does not allow processed monitoring right now. */
    SessionNotReady,

    /*! \brief No physical input route is currently identified. */
    NoInputDevice,

    /*! \brief The current route has no stored calibration. */
    MissingCalibration,

    /*! \brief The live-input backend refused the calibrated gain or the route. */
    BackendUnavailable,
};

/*!
\brief Reports whether a status is a fault rather than a designed off state.
\param status Monitoring status to classify.
\return True when the store or the backend failed.
*/
[[nodiscard]] constexpr bool isLiveInputFault(LiveInputMonitoringStatus status) noexcept
{
    return status == LiveInputMonitoringStatus::CalibrationStoreUnavailable ||
           status == LiveInputMonitoringStatus::BackendUnavailable;
}

/*!
\brief Returns the one sentence that says why live input is off, for every surface in both
products.
\param status Status the gate reported.
\return A sentence ending in a period; empty for Active.
*/
[[nodiscard]] std::string_view liveInputStatusText(LiveInputMonitoringStatus status) noexcept;

/*! \brief Session facts the live-input monitoring gate evaluates. */
struct LiveInputMonitoringContext
{
    /*!
    \brief True when the product allows processed monitoring now: its session audio and tone rig
    are ready, and nothing (such as an open audio-device settings window) holds the route.
    */
    bool session_ready{false};
};

} // namespace rock_hero::common::audio
