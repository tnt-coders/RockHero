/*!
\file input_calibration_window.h
\brief Private editor UI window hosting live input calibration.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::core
{
class IEditorController;
}

namespace rock_hero::editor::ui
{

/*!
\brief Finds the installed input calibration guide: beside the executable's installed docs, else
in the build tree's generated docs.
\return The guide's HTML page, or a default juce::File when neither location has it.
*/
[[nodiscard]] juce::File installedInputCalibrationGuide();

/*!
\brief Top-level popup that owns input calibration controls and live meter sampling.
*/
class InputCalibrationWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the calibration window and centers it around the editor when possible.
    \param controller Controller that receives calibration workflow intents and samples input.
    \param prompt Controller-derived prompt data used to seed display state.
    \param guide The calibration guide the "?" opens; a file that does not exist disables the "?"
           and says why.
    \param centering_component Optional component used to position the window.
    */
    InputCalibrationWindow(
        core::IEditorController& controller, const core::InputCalibrationPrompt& prompt,
        juce::File guide, juce::Component* centering_component);

    /*! \brief Forwards the close request to the controller and hides the window. */
    void closeButtonPressed() override;

private:
    // Popup content is private to the implementation file.
    class Content;

    // Raw pointer mirrors the DocumentWindow-owned content so title-bar close can emit dismissal.
    Content* m_content{};
};

} // namespace rock_hero::editor::ui
