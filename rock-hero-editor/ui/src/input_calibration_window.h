/*!
\file input_calibration_window.h
\brief Private editor UI window hosting live input calibration.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/editor/core/input_calibration_view_state.h>

namespace rock_hero::common::audio
{
class ILiveInput;
}

namespace rock_hero::editor::core
{
class IEditorController;
class InputCalibrationController;
} // namespace rock_hero::editor::core

namespace rock_hero::editor::ui
{

/*!
\brief Top-level popup that hosts the input calibration controller and view.
*/
class InputCalibrationWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the calibration window and centers it around the editor when possible.
    \param controller Editor workflow that receives calibration route and persistence intents.
    \param live_input Optional live input meter source sampled while the popup is open.
    \param prompt Controller-derived prompt data used to seed display state.
    \param centering_component Optional component used to position the window.
    */
    InputCalibrationWindow(
        core::IEditorController& controller, const common::audio::ILiveInput* live_input,
        core::InputCalibrationPrompt prompt, juce::Component* centering_component);

    /*! \brief Clears the content component before destroying the owned controller. */
    ~InputCalibrationWindow() override;

    /*! \brief Forwards the close request to the controller and hides the window. */
    void closeButtonPressed() override;

private:
    // Controller that owns calibration workflow state for the hosted view.
    std::unique_ptr<core::InputCalibrationController> m_controller;
};

} // namespace rock_hero::editor::ui
