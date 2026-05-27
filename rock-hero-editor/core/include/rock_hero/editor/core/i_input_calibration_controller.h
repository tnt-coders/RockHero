/*!
\file i_input_calibration_controller.h
\brief Framework-free intent contract for editor input calibration.
*/

#pragma once

namespace rock_hero::editor::core
{

/*! \brief Controller contract receiving input-calibration view intents. */
class IInputCalibrationController
{
public:
    /*! \brief Destroys the input calibration controller contract. */
    virtual ~IInputCalibrationController() = default;

    /*! \brief Handles an automatic calibration button press. */
    virtual void onAutomaticCalibrationRequested() = 0;

    /*!
    \brief Handles a manual gain slider change.
    \param gain_db Displayed gain in decibels.
    */
    virtual void onManualGainChanged(double gain_db) = 0;

    /*! \brief Handles a manual calibration apply button press. */
    virtual void onManualCalibrationRequested() = 0;

    /*! \brief Handles one live meter timer tick from the host view. */
    virtual void onMeterTick() = 0;

    /*! \brief Handles a dismiss or native-close request. */
    virtual void onDismissRequested() = 0;

protected:
    /*! \brief Creates the input calibration controller contract. */
    IInputCalibrationController() = default;

    /*! \brief Copies the input calibration controller contract. */
    IInputCalibrationController(const IInputCalibrationController&) = default;

    /*! \brief Moves the input calibration controller contract. */
    IInputCalibrationController(IInputCalibrationController&&) = default;

    /*!
    \brief Assigns the input calibration controller contract.
    \return Reference to this input calibration controller contract.
    */
    IInputCalibrationController& operator=(const IInputCalibrationController&) = default;

    /*!
    \brief Move-assigns the input calibration controller contract.
    \return Reference to this input calibration controller contract.
    */
    IInputCalibrationController& operator=(IInputCalibrationController&&) = default;
};

} // namespace rock_hero::editor::core
