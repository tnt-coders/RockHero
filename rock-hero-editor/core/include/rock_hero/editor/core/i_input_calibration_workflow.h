/*!
\file i_input_calibration_workflow.h
\brief Framework-free backend workflow used by input calibration.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/live_input_error.h>

namespace rock_hero::editor::core
{

/*! \brief Narrow editor workflow required by the input calibration controller. */
class IInputCalibrationWorkflow
{
public:
    /*! \brief Destroys the input calibration workflow contract. */
    virtual ~IInputCalibrationWorkflow() = default;

    /*!
    \brief Prepares the live input route for raw input calibration measurement.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() = 0;

    /*! \brief Stops an active calibration measurement while leaving the prompt open. */
    virtual void onInputCalibrationMeasurementCancelled() = 0;

    /*!
    \brief Applies and stores a completed input calibration gain.
    \param gain_db Calibrated input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationSucceeded(double gain_db) = 0;

    /*!
    \brief Applies and stores a manually entered input calibration gain.
    \param gain_db Input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationManuallySet(double gain_db) = 0;

    /*! \brief Handles the calibration prompt closing without a new successful calibration. */
    virtual void onInputCalibrationDismissed() = 0;

protected:
    /*! \brief Creates the input calibration workflow contract. */
    IInputCalibrationWorkflow() = default;

    /*! \brief Copies the input calibration workflow contract. */
    IInputCalibrationWorkflow(const IInputCalibrationWorkflow&) = default;

    /*! \brief Moves the input calibration workflow contract. */
    IInputCalibrationWorkflow(IInputCalibrationWorkflow&&) = default;

    /*!
    \brief Assigns the input calibration workflow contract.
    \return Reference to this input calibration workflow contract.
    */
    IInputCalibrationWorkflow& operator=(const IInputCalibrationWorkflow&) = default;

    /*!
    \brief Move-assigns the input calibration workflow contract.
    \return Reference to this input calibration workflow contract.
    */
    IInputCalibrationWorkflow& operator=(IInputCalibrationWorkflow&&) = default;
};

} // namespace rock_hero::editor::core
