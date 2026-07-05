/*!
\file input_calibration_window.h
\brief Private editor UI window hosting live input calibration.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::common::audio
{
class ILiveInput;
}

namespace rock_hero::editor::core
{
class IEditorController;
}

namespace rock_hero::editor::ui
{

/*!
\brief Top-level popup that owns input calibration controls and live meter sampling.
*/
class InputCalibrationWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the calibration window and centers it around the editor when possible.
    \param controller Controller that receives calibration workflow intents.
    \param live_input Optional live input meter source sampled while the popup is open.
    \param prompt Controller-derived prompt data used to seed display state.
    \param centering_component Optional component used to position the window.
    */
    InputCalibrationWindow(
        core::IEditorController& controller, const common::audio::ILiveInput* live_input,
        const core::InputCalibrationPrompt& prompt, juce::Component* centering_component);

    /*! \brief Forwards the close request to the controller and hides the window. */
    void closeButtonPressed() override;

private:
    // Popup content is private to the implementation file.
    class Content;

    // Raw pointer mirrors the DocumentWindow-owned content so title-bar close can emit dismissal.
    Content* m_content{};
};

} // namespace rock_hero::editor::ui
