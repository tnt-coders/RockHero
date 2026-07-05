/*!
\file i_input_calibration_view.h
\brief Framework-free view contract for the input calibration popup.
*/

#pragma once

#include <rock_hero/editor/core/input_calibration/input_calibration_view_state.h>

namespace rock_hero::editor::core
{

/*! \brief View boundary driven by InputCalibrationController. */
class IInputCalibrationView
{
public:
    /*! \brief Destroys the input-calibration view interface. */
    virtual ~IInputCalibrationView() = default;

    /*!
    \brief Pushes a complete popup render state into the concrete view.
    \param state Framework-free render state to display.
    */
    virtual void setState(const InputCalibrationViewState& state) = 0;

protected:
    /*! \brief Creates the input-calibration view interface. */
    IInputCalibrationView() = default;

    /*! \brief Copies the input-calibration view interface. */
    IInputCalibrationView(const IInputCalibrationView&) = default;

    /*! \brief Moves the input-calibration view interface. */
    IInputCalibrationView(IInputCalibrationView&&) = default;

    /*!
    \brief Assigns the input-calibration view interface from another interface.
    \return Reference to this input-calibration view interface.
    */
    IInputCalibrationView& operator=(const IInputCalibrationView&) = default;

    /*!
    \brief Move-assigns the input-calibration view interface from another interface.
    \return Reference to this input-calibration view interface.
    */
    IInputCalibrationView& operator=(IInputCalibrationView&&) = default;
};

} // namespace rock_hero::editor::core
