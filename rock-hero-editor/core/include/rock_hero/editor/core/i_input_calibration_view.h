/*!
\file i_input_calibration_view.h
\brief Framework-free view contract for editor input calibration.
*/

#pragma once

#include <rock_hero/editor/core/input_calibration_view_state.h>

namespace rock_hero::editor::core
{

/*! \brief View contract driven by InputCalibrationController. */
class IInputCalibrationView
{
public:
    /*! \brief Destroys the input calibration view contract. */
    virtual ~IInputCalibrationView() = default;

    /*!
    \brief Applies derived render state to the calibration view.
    \param state State to render.
    */
    virtual void setState(const InputCalibrationViewState& state) = 0;

    /*! \brief Requests that the host close the calibration window. */
    virtual void requestClose() = 0;

protected:
    /*! \brief Creates the input calibration view contract. */
    IInputCalibrationView() = default;

    /*! \brief Copies the input calibration view contract. */
    IInputCalibrationView(const IInputCalibrationView&) = default;

    /*! \brief Moves the input calibration view contract. */
    IInputCalibrationView(IInputCalibrationView&&) = default;

    /*!
    \brief Assigns the input calibration view contract.
    \return Reference to this input calibration view contract.
    */
    IInputCalibrationView& operator=(const IInputCalibrationView&) = default;

    /*!
    \brief Move-assigns the input calibration view contract.
    \return Reference to this input calibration view contract.
    */
    IInputCalibrationView& operator=(IInputCalibrationView&&) = default;
};

} // namespace rock_hero::editor::core
