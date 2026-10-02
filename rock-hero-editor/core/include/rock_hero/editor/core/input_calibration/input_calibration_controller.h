/*!
\file input_calibration_controller.h
\brief Headless presentation controller for the input calibration popup.
*/

#pragma once

#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_controller.h>
#include <rock_hero/editor/core/input_calibration/i_input_calibration_view.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Owns popup-local input calibration state without depending on JUCE widgets.

The player types a gain, or measures one by playing, then Apply stores it. A finished measurement
fills the gain; nothing is stored until Apply. The shared live-input monitor runs the measurement;
this controller samples it through the editor controller once per UI tick and projects the readings
into InputCalibrationViewState.
*/
class InputCalibrationController final
{
public:
    /*!
    \brief Creates a popup-local controller.
    \param editor Editor controller the popup's intents go to.
    \param prompt Initial prompt state supplied by the editor workflow; its stored gain, if any,
           fills the slider.
    */
    InputCalibrationController(IEditorController& editor, const InputCalibrationPrompt& prompt);

    /*! \brief Copies are disabled because the controller stores popup view attachment state. */
    InputCalibrationController(const InputCalibrationController&) = delete;

    /*! \brief Copy assignment is disabled because the controller stores an editor reference. */
    InputCalibrationController& operator=(const InputCalibrationController&) = delete;

    /*! \brief Moves are disabled because the attached view stores no back-reference update hook. */
    InputCalibrationController(InputCalibrationController&&) = delete;

    /*! \brief Move assignment is disabled because the controller stores an editor reference. */
    InputCalibrationController& operator=(InputCalibrationController&&) = delete;

    /*! \brief Destroys the InputCalibrationController. */
    ~InputCalibrationController() = default;

    /*!
    \brief Attaches a view and immediately pushes the current state.
    \param view View to update while attached.
    */
    void attachView(IInputCalibrationView& view);

    /*!
    \brief Detaches a view if it is still attached.
    \param view View being destroyed or disconnected.
    */
    void detachView(IInputCalibrationView& view) noexcept;

    /*!
    \brief Sets the gain by hand.
    \param gain_db Gain in decibels.
    */
    void onManualGainChanged(double gain_db);

    /*!
    \brief Stores the shown gain through the editor, whose success ends the prompt; a refused store
    says why.
    */
    void onApplyRequested();

    /*!
    \brief Chooses the pickups the measurement assumes.
    \param pickups The pickups the player will measure with.
    */
    void onPickupsSelected(common::audio::PickupClass pickups);

    /*!
    \brief Starts a measurement, or stops the running one without a result. A refused start says
    why.
    */
    void onMeasureRequested();

    /*!
    \brief Samples the raw input once, following a running measurement.

    The UI calls it at common::audio::inputCalibrationSampleRateHz() while the popup is open.
    */
    void onSampleTick();

    /*! \brief Reports that the installed input calibration guide could not be opened. */
    void onDocumentationUnavailable();

    /*! \brief Closes the popup through the editor without storing anything. */
    void onCloseRequested();

private:
    void setGain(double gain_db);
    void finishMeasurement(std::string message);
    void publishState();

    IEditorController& m_editor;
    IInputCalibrationView* m_view{};
    InputCalibrationViewState m_state;
    common::audio::AudioMeterLevel m_last_raw_meter_level;
};

} // namespace rock_hero::editor::core
