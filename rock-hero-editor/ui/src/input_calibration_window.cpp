#include "input_calibration_window.h"

#include "audio_level_meter.h"

#include <BinaryData.h>
#include <memory>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/input_calibration.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/input_calibration_controller.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_input_calibration_meter_hz{30};
constexpr int g_input_calibration_measurement_seconds{8};
constexpr int g_input_calibration_wait_seconds{10};
constexpr int g_input_calibration_sample_count{
    g_input_calibration_meter_hz * g_input_calibration_measurement_seconds
};
constexpr int g_input_calibration_wait_sample_count{
    g_input_calibration_meter_hz * g_input_calibration_wait_seconds
};
// Discard the first half-second so backend gain resets and meter windows settle before capture.
constexpr int g_input_calibration_settle_sample_count{g_input_calibration_meter_hz / 2};
// Match the normal transport-bar master meter width so the popup stays visually compact.
constexpr int g_input_calibration_meter_width{384};
constexpr int g_input_calibration_content_margin{14};
constexpr int g_input_calibration_preferred_width{
    g_input_calibration_meter_width + (g_input_calibration_content_margin * 2)
};

[[nodiscard]] juce::String inputCalibrationTargetText()
{
    return juce::String{"Target: "} +
           juce::String{common::audio::inputCalibrationTargetRmsDb(), 0} + " dBFS average, " +
           juce::String{common::audio::inputCalibrationTargetPeakDb(), 0} + " dBFS peak";
}

// Resolves installed docs from the executable location and falls back to build-tree docs.
[[nodiscard]] juce::File inputCalibrationDocumentationFile()
{
    constexpr int maximum_directory_search_depth{8};
    const juce::String documentation_file_name{"user_input_calibration.html"};
    juce::File search_root =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    for (int depth = 0; depth < maximum_directory_search_depth; ++depth)
    {
        const juce::File candidate =
            search_root.getChildFile(ROCK_HERO_INSTALLED_DOCS_RELATIVE_PATH)
                .getChildFile(documentation_file_name);
        if (candidate.existsAsFile())
        {
            return candidate;
        }

        const juce::File parent = search_root.getParentDirectory();
        if (parent == search_root)
        {
            break;
        }
        search_root = parent;
    }

    const juce::File build_tree_documentation =
        juce::File{ROCK_HERO_BUILD_DOCS_DIR}.getChildFile(documentation_file_name);
    return build_tree_documentation.existsAsFile() ? build_tree_documentation : juce::File{};
}

// Opens the local HTML file directly so Windows handles it as a normal filesystem document.
[[nodiscard]] bool openInputCalibrationDocumentation()
{
    const juce::File documentation = inputCalibrationDocumentationFile();
    return documentation.existsAsFile() && documentation.startAsProcess();
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

// Self-contained calibration prompt that samples raw input and reports the result to controller.
class InputCalibrationWindow::Content final : public juce::Component,
                                              private juce::Timer,
                                              private core::IInputCalibrationView,
                                              private core::InputCalibrationController::Host
{
public:
    Content(
        InputCalibrationWindow& owner, core::IEditorController& controller,
        const common::audio::ILiveInput* live_input, const core::InputCalibrationPrompt& prompt)
        : m_owner(owner)
        , m_editor_controller(controller)
        , m_live_input(live_input)
        , m_calibration_controller(
              *this, prompt,
              core::InputCalibrationController::CaptureSettings{
                  .settle_sample_count = g_input_calibration_settle_sample_count,
                  .wait_sample_count = g_input_calibration_wait_sample_count,
                  .measurement_sample_count = g_input_calibration_sample_count,
              })
        , m_input_meter(AudioLevelMeterOrientation::Horizontal, "Input")
    {
        m_target_label.setComponentID("input_calibration_target");
        m_target_label.setText(inputCalibrationTargetText(), juce::dontSendNotification);
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
        m_help_button.onClick = [this] { openDocumentation(); };
        addAndMakeVisible(m_help_button);

        m_input_meter.setComponentID("input_calibration_meter");
        addAndMakeVisible(m_input_meter);

        m_manual_label.setComponentID("input_calibration_manual_label");
        m_manual_label.setText("Gain:", juce::dontSendNotification);
        m_manual_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_manual_label);

        configureManualInputGainSlider(m_manual_gain_slider);
        m_manual_gain_slider.onValueChange = [this] {
            m_calibration_controller.onManualGainChanged(m_manual_gain_slider.getValue());
        };
        addAndMakeVisible(m_manual_gain_slider);

        m_manual_apply_button.setComponentID("input_calibration_manual_apply_button");
        m_manual_apply_button.setButtonText("Apply");
        m_manual_apply_button.onClick = [this] {
            m_calibration_controller.onManualApplyRequested();
        };
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
        m_calibrate_button.onClick = [this] { startMeasurement(); };
        addAndMakeVisible(m_calibrate_button);

        m_cancel_button.setComponentID("input_calibration_cancel_button");
        m_cancel_button.setButtonText("Dismiss");
        m_cancel_button.onClick = [this] { m_owner.closeButtonPressed(); };
        addAndMakeVisible(m_cancel_button);

        setSize(g_input_calibration_preferred_width, preferredHeight());
        m_calibration_controller.attachView(*this);
        startTimerHz(g_input_calibration_meter_hz);
    }

    Content(const Content&) = delete;
    Content& operator=(const Content&) = delete;
    Content(Content&&) = delete;
    Content& operator=(Content&&) = delete;

    ~Content() override
    {
        stopTimer();
        m_calibration_controller.detachView(*this);
    }

    void resized() override
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
        m_cancel_button.setBounds(buttons.removeFromRight(96));
        buttons.removeFromRight(8);
        m_calibrate_button.setBounds(buttons.removeFromRight(96));
    }

    void requestDismissal()
    {
        m_calibration_controller.onDismissRequested();
    }

private:
    [[nodiscard]] int preferredHeight() const
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

    void syncPreferredSize()
    {
        setSize(g_input_calibration_preferred_width, preferredHeight());
    }

    void setState(const core::InputCalibrationViewState& state) override
    {
        m_input_meter.setLevel(state.input_meter_level);
        m_manual_gain_slider.setValue(state.input_gain_db, juce::dontSendNotification);
        m_manual_gain_slider.updateText();
        m_status.setText(juce::String{state.status_message}, juce::dontSendNotification);
        m_calibrate_button.setEnabled(state.start_measurement_enabled);
        m_manual_gain_slider.setEnabled(state.manual_gain_controls_enabled);
        m_manual_apply_button.setEnabled(state.manual_gain_controls_enabled);
        m_cancel_button.setButtonText(juce::String{state.dismiss_button_text});
        syncPreferredSize();
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    startInputCalibrationMeasurement() override
    {
        return m_editor_controller.onInputCalibrationMeasurementStarted();
    }

    void cancelInputCalibrationMeasurement() override
    {
        m_editor_controller.onInputCalibrationMeasurementCancelled();
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> applyAutomaticInputCalibration(
        double gain_db) override
    {
        return m_editor_controller.onInputCalibrationSucceeded(gain_db);
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> applyManualInputCalibration(
        double gain_db) override
    {
        return m_editor_controller.onInputCalibrationManuallySet(gain_db);
    }

    void dismissInputCalibration() override
    {
        m_editor_controller.onInputCalibrationDismissed();
    }

    // Reports missing generated docs in the popup instead of letting the help button fail silently.
    void openDocumentation()
    {
        if (openInputCalibrationDocumentation())
        {
            return;
        }

        m_calibration_controller.onDocumentationUnavailable();
    }

    void startMeasurement()
    {
        if (m_live_input == nullptr)
        {
            m_calibration_controller.onMeterSourceUnavailable();
            return;
        }

        if (!m_calibration_controller.onMeasurementStartRequested())
        {
            return;
        }

        // rawInputMeterLevel() clears the Tracktion meter reader, so this primes retry runs
        // after the controller has reset input gain and rebuilt the calibration route.
        [[maybe_unused]] const common::audio::AudioMeterLevel primed_level =
            m_live_input->rawInputMeterLevel();
    }

    void timerCallback() override
    {
        common::audio::AudioMeterLevel level{};
        if (m_live_input != nullptr)
        {
            level = m_live_input->rawInputMeterLevel();
        }
        m_calibration_controller.onMeterSampled(level);
    }

    InputCalibrationWindow& m_owner;
    core::IEditorController& m_editor_controller;
    const common::audio::ILiveInput* m_live_input{};
    core::InputCalibrationController m_calibration_controller;
    AudioLevelMeter m_input_meter;
    juce::Label m_target_label;
    std::unique_ptr<juce::Drawable> m_help_icon;
    juce::DrawableButton m_help_button{"input_calibration_help", juce::DrawableButton::ImageFitted};
    juce::Label m_manual_label;
    juce::Slider m_manual_gain_slider;
    juce::TextButton m_manual_apply_button;
    juce::Label m_status;
    juce::TextButton m_calibrate_button;
    juce::TextButton m_cancel_button;
};

InputCalibrationWindow::InputCalibrationWindow(
    core::IEditorController& controller, const common::audio::ILiveInput* live_input,
    const core::InputCalibrationPrompt& prompt, juce::Component* centering_component)
    : juce::DocumentWindow(
          "Input Calibration", juce::Colours::darkgrey.darker(0.16f),
          juce::DocumentWindow::closeButton)
{
    setComponentID("input_calibration_window");
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
    auto content = std::make_unique<Content>(*this, controller, live_input, prompt);
    m_content = content.get();
    setContentOwned(content.release(), true);
    centreAroundComponent(centering_component, getWidth(), getHeight());
    addToDesktop(juce::ComponentPeer::windowHasCloseButton);
    setVisible(true);
    toFront(true);
}

void InputCalibrationWindow::closeButtonPressed()
{
    if (m_content != nullptr)
    {
        m_content->requestDismissal();
    }
    setVisible(false);
}

} // namespace rock_hero::editor::ui
