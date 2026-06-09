/*!
\file i_live_input.h
\brief Live guitar input calibration and monitoring port.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/live_input_error.h>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned live guitar input boundary.

All methods are message-thread operations. The port separates user-local input calibration from
authored tone-chain state so the live rig can persist output gain without persisting input gain.
*/
class ILiveInput
{
public:
    /*! \brief Destroys the live input interface. */
    virtual ~ILiveInput() = default;

    /*!
    \brief Reads the current input gain applied before the signal chain.
    \return Current calibrated input gain.
    */
    [[nodiscard]] virtual Gain inputGain() const = 0;

    /*!
    \brief Applies the calibrated input gain before the signal chain.
    \param gain Desired input gain; clamped by the implementation.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveInputError> setInputGain(Gain gain) = 0;

    /*!
    \brief Returns the raw input peak meter used by calibration.
    \return Latest raw input meter level.
    */
    [[nodiscard]] virtual AudioMeterLevel rawInputMeterLevel() const = 0;

    /*!
    \brief Reports whether processed live guitar monitoring is currently enabled.
    \return True when live guitar is routed through the calibrated signal chain.
    */
    [[nodiscard]] virtual bool liveInputMonitoringEnabled() const = 0;

    /*!
    \brief Enables or disables processed live guitar monitoring explicitly.
    \param enabled True to route calibrated live guitar through the chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveInputError> setLiveInputMonitoringEnabled(
        bool enabled) = 0;

    /*!
    \brief Reports whether unprocessed calibration monitoring is currently enabled.
    \return True when live guitar is routed directly to output for calibration.
    */
    [[nodiscard]] virtual bool calibrationInputMonitoringEnabled() const = 0;

    /*!
    \brief Enables or disables unprocessed calibration monitoring explicitly.
    \param enabled True to route live guitar directly to output for calibration.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveInputError> setCalibrationInputMonitoringEnabled(
        bool enabled) = 0;

protected:
    /*! \brief Creates the live input interface. */
    ILiveInput() = default;

    /*! \brief Copies the live input interface. */
    ILiveInput(const ILiveInput&) = default;

    /*! \brief Moves the live input interface. */
    ILiveInput(ILiveInput&&) = default;

    /*!
    \brief Assigns the live input interface from another interface.
    \return Reference to this live input interface.
    */
    ILiveInput& operator=(const ILiveInput&) = default;

    /*!
    \brief Move-assigns the live input interface from another interface.
    \return Reference to this live input interface.
    */
    ILiveInput& operator=(ILiveInput&&) = default;
};

} // namespace rock_hero::common::audio
