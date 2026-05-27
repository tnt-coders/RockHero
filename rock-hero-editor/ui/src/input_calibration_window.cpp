#include "input_calibration_window.h"

#include "input_calibration_view.h"

#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/input_calibration_controller.h>
#include <utility>

namespace rock_hero::editor::ui
{

// Builds the top-level calibration window around a headless controller and passive view.
InputCalibrationWindow::InputCalibrationWindow(
    core::IEditorController& controller, const common::audio::ILiveInput* live_input,
    core::InputCalibrationPrompt prompt, juce::Component* centering_component)
    : juce::DocumentWindow(
          "Input Calibration", juce::Colours::darkgrey.darker(0.16f),
          juce::DocumentWindow::closeButton)
    , m_controller(
          std::make_unique<core::InputCalibrationController>(
              controller, live_input, std::move(prompt)))
{
    setComponentID("input_calibration_window");
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());

    auto content = std::make_unique<InputCalibrationView>(*m_controller);
    m_controller->attachView(*content);
    setContentOwned(content.release(), true);

    centreAroundComponent(centering_component, getWidth(), getHeight());
    addToDesktop(juce::ComponentPeer::windowHasCloseButton);
    setVisible(true);
    toFront(true);
}

// Clears the DocumentWindow-owned content before its referenced controller is destroyed.
InputCalibrationWindow::~InputCalibrationWindow()
{
    clearContentComponent();
}

// Routes native close through the same controller path as the popup close button.
void InputCalibrationWindow::closeButtonPressed()
{
    if (m_controller != nullptr)
    {
        m_controller->onDismissRequested();
    }
    else
    {
        setVisible(false);
    }
}

} // namespace rock_hero::editor::ui
