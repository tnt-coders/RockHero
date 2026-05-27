#include "input_calibration_view.h"

#include <BinaryData.h>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/editor/core/input_calibration_controller.h>

namespace rock_hero::editor::ui
{

namespace
{

// Match the normal transport-bar master meter width so the popup stays visually compact.
constexpr int g_input_calibration_meter_width{384};
constexpr int g_input_calibration_content_margin{14};
constexpr int g_input_calibration_preferred_width{
    g_input_calibration_meter_width + (g_input_calibration_content_margin * 2)
};

[[nodiscard]] juce::URL inputCalibrationDocumentationUrl()
{
    return juce::URL{"https://tnt-coders.github.io/RockHero/user_input_calibration.html"};
}

void configureManualInputGainSlider(juce::Slider& slider)
{
    slider.setComponentID("input_calibration_manual_gain");
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setRange(common::audio::minimumGainDb(), common::audio::maximumGainDb(), 0.1);
    slider.setValue(common::audio::defaultGainDb(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, common::audio::defaultGainDb());
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    slider.setTextValueSuffix(" dB");
}

} // namespace

// Configures the passive JUCE controls and starts the meter timer.
InputCalibrationView::InputCalibrationView(core::IInputCalibrationController& controller)
    : m_controller(controller)
    , m_input_meter(AudioLevelMeterOrientation::Horizontal, "Input")
{
    configureControls();
    setSize(preferredWidth(), preferredContentHeight());
    startTimerHz(core::inputCalibrationMeterHz());
}

InputCalibrationView::~InputCalibrationView() = default;

// Returns the fixed content width used by the compact calibration popup.
int InputCalibrationView::preferredWidth() noexcept
{
    return g_input_calibration_preferred_width;
}

// Computes the content height from stable row heights so status updates do not shift controls.
int InputCalibrationView::preferredContentHeight() const noexcept
{
    constexpr int outer_margin{g_input_calibration_content_margin * 2};
    constexpr int gap_height{8};
    constexpr int target_row_height{28};
    constexpr int target_to_status_gap_height{8};
    constexpr int status_height{48};
    constexpr int status_to_meter_gap_height{10};
    constexpr int meter_height{26};
    constexpr int manual_controls_height{28};
    constexpr int buttons_height{28};

    return outer_margin + target_row_height + target_to_status_gap_height + status_height +
           status_to_meter_gap_height + meter_height + gap_height + manual_controls_height +
           gap_height + buttons_height;
}

// Applies headless controller state to the passive controls without emitting new intents.
void InputCalibrationView::setState(const core::InputCalibrationViewState& state)
{
    m_state = state;
    m_target_label.setText(state.target_text, juce::dontSendNotification);
    m_status.setText(state.status_text, juce::dontSendNotification);
    m_input_meter.setLevel(state.input_level);
    m_manual_gain_slider.setValue(state.input_gain_db, juce::dontSendNotification);
    m_manual_gain_slider.updateText();
    m_calibrate_button.setEnabled(state.calibrate_enabled);
    m_manual_gain_slider.setEnabled(state.manual_gain_enabled);
    m_manual_apply_button.setEnabled(state.manual_apply_enabled);
    m_close_button.setButtonText(state.close_button_text);
    syncWindowHeightToContent();
}

// Requests closure through the same host-window path as native close.
void InputCalibrationView::requestClose()
{
    closeWindow();
}

// Lays out the compact calibration form.
void InputCalibrationView::resized()
{
    auto area = getLocalBounds().reduced(g_input_calibration_content_margin);
    auto target_row = area.removeFromTop(28);
    m_help_button.setBounds(target_row.removeFromRight(28).reduced(2));
    target_row.removeFromRight(8);
    m_target_label.setBounds(target_row);
    area.removeFromTop(8);
    m_status.setBounds(area.removeFromTop(48));
    area.removeFromTop(10);
    m_input_meter.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    auto manual_area = area.removeFromTop(28);
    m_manual_label.setBounds(manual_area.removeFromLeft(60));
    m_manual_apply_button.setBounds(manual_area.removeFromRight(72));
    manual_area.removeFromRight(8);
    m_manual_gain_slider.setBounds(manual_area);
    area.removeFromTop(8);
    auto buttons = area.removeFromBottom(28);
    m_close_button.setBounds(buttons.removeFromRight(96));
    buttons.removeFromRight(8);
    m_calibrate_button.setBounds(buttons.removeFromRight(96));
}

// Lets the headless controller sample the raw meter and advance automatic capture.
void InputCalibrationView::timerCallback()
{
    m_controller.onMeterTick();
}

// Configures controls once so state updates only carry dynamic render data.
void InputCalibrationView::configureControls()
{
    m_target_label.setComponentID("input_calibration_target");
    m_target_label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_target_label);

    m_help_icon =
        juce::Drawable::createFromImageData(BinaryData::help_svg, BinaryData::help_svgSize);
    m_help_button.setComponentID("input_calibration_help_button");
    m_help_button.setTooltip("Open input calibration guide");
    m_help_button.setWantsKeyboardFocus(false);
    m_help_button.setMouseClickGrabsKeyboardFocus(false);
    m_help_button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    m_help_button.setImages(m_help_icon.get());
    m_help_button.onClick = [] { inputCalibrationDocumentationUrl().launchInDefaultBrowser(); };
    addAndMakeVisible(m_help_button);

    m_input_meter.setComponentID("input_calibration_meter");
    addAndMakeVisible(m_input_meter);

    m_manual_label.setComponentID("input_calibration_manual_label");
    m_manual_label.setText("Gain:", juce::dontSendNotification);
    m_manual_label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_manual_label);

    configureManualInputGainSlider(m_manual_gain_slider);
    m_manual_gain_slider.onValueChange = [this] {
        m_controller.onManualGainChanged(m_manual_gain_slider.getValue());
    };
    addAndMakeVisible(m_manual_gain_slider);

    m_manual_apply_button.setComponentID("input_calibration_manual_apply_button");
    m_manual_apply_button.setButtonText("Apply");
    m_manual_apply_button.onClick = [this] { m_controller.onManualCalibrationRequested(); };
    addAndMakeVisible(m_manual_apply_button);

    m_status.setComponentID("input_calibration_status");
    m_status.setJustificationType(juce::Justification::centredLeft);
    m_status.setColour(
        juce::Label::backgroundColourId, juce::Colour::fromRGB(31, 39, 47).withAlpha(0.92f));
    m_status.setColour(juce::Label::outlineColourId, juce::Colours::black.withAlpha(0.45f));
    m_status.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
    m_status.setBorderSize(juce::BorderSize<int>{6, 8, 6, 8});
    m_status.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(m_status);

    m_calibrate_button.setComponentID("input_calibration_start_button");
    m_calibrate_button.setButtonText("Calibrate");
    m_calibrate_button.onClick = [this] { m_controller.onAutomaticCalibrationRequested(); };
    addAndMakeVisible(m_calibrate_button);

    m_close_button.setComponentID("input_calibration_cancel_button");
    m_close_button.onClick = [this] { m_controller.onDismissRequested(); };
    addAndMakeVisible(m_close_button);
}

// Keeps the host window aligned with the content's fixed preferred size.
void InputCalibrationView::syncWindowHeightToContent()
{
    setSize(preferredWidth(), preferredContentHeight());
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        window->setSize(preferredWidth(), preferredContentHeight());
    }
}

// Closes the containing window without trying to own prompt-state policy in the view.
void InputCalibrationView::closeWindow()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        window->setVisible(false);
    }
}

} // namespace rock_hero::editor::ui
