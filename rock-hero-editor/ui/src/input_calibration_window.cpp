#include "input_calibration_window.h"

#include "audio_level_meter.h"

#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/input_calibration.h>
#include <rock_hero/editor/core/i_editor_controller.h>
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

// Keeps tiny negative values that display at one decimal from showing up as "-0.0 dB".
[[nodiscard]] double canonicalInputGainDb(double gain_db)
{
    const double rounded_tenths = std::round(gain_db * 10.0);
    return rounded_tenths == 0.0 ? 0.0 : gain_db;
}

[[nodiscard]] juce::String inputCalibrationTargetText()
{
    return juce::String{"Target: "} +
           juce::String{common::audio::inputCalibrationTargetRmsDb(), 0} + " dBFS average, " +
           juce::String{common::audio::inputCalibrationTargetPeakDb(), 0} + " dBFS peak";
}

[[nodiscard]] juce::URL inputCalibrationDocumentationUrl()
{
    return juce::URL{"https://tnt-coders.github.io/RockHero/user_input_calibration.html"};
}

[[nodiscard]] juce::String inputCalibrationReadyText()
{
    return "Click \"Calibrate\" to run automatic calibration, or adjust gain manually and click "
           "\"Apply\".";
}

[[nodiscard]] juce::String inputCalibrationWaitingText()
{
    return "Waiting for input... Strum all open strings at a steady, moderate volume.";
}

[[nodiscard]] juce::String inputCalibrationMeasuringText()
{
    return "Keep strumming all open strings at a steady, moderate volume.";
}

[[nodiscard]] juce::String inputCalibrationCompleteText(double gain_db)
{
    return juce::String{"Calibration complete. Gain set to "} +
           juce::String{canonicalInputGainDb(gain_db), 1} + " dB.";
}

[[nodiscard]] juce::String inputManualCalibrationCompleteText(double gain_db)
{
    return juce::String{"Manual calibration saved. Gain set to "} +
           juce::String{canonicalInputGainDb(gain_db), 1} + " dB.";
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

[[nodiscard]] common::audio::AudioMeterLevel applyDisplayGain(
    common::audio::AudioMeterLevel level, double gain_db)
{
    if (level.peak_db <= common::audio::minimumAudioMeterDb())
    {
        return level;
    }

    level.peak_db = std::clamp(level.peak_db + gain_db, common::audio::minimumAudioMeterDb(), 12.0);
    level.clipping = level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb();
    return level;
}

} // namespace

// Self-contained calibration prompt that samples raw input and reports the result to controller.
class InputCalibrationWindow::Content final : public juce::Component, private juce::Timer
{
public:
    Content(
        InputCalibrationWindow& owner, core::IEditorController& controller,
        const common::audio::ILiveInput* live_input, core::InputCalibrationPrompt prompt)
        : m_owner(owner)
        , m_controller(controller)
        , m_live_input(live_input)
        , m_input_gain_db(canonicalInputGainDb(prompt.input_gain_db))
        , m_committed_input_gain_db(m_input_gain_db)
        , m_measurement_restore_gain_db(m_input_gain_db)
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
        m_help_button.onClick = [] { inputCalibrationDocumentationUrl().launchInDefaultBrowser(); };
        addAndMakeVisible(m_help_button);

        m_input_meter.setComponentID("input_calibration_meter");
        addAndMakeVisible(m_input_meter);

        m_manual_label.setComponentID("input_calibration_manual_label");
        m_manual_label.setText("Gain:", juce::dontSendNotification);
        m_manual_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_manual_label);

        configureManualInputGainSlider(m_manual_gain_slider);
        setDisplayedInputGain(m_input_gain_db);
        m_manual_gain_slider.onValueChange = [this] { updateManualGainPreview(); };
        addAndMakeVisible(m_manual_gain_slider);

        m_manual_apply_button.setComponentID("input_calibration_manual_apply_button");
        m_manual_apply_button.setButtonText("Apply");
        m_manual_apply_button.onClick = [this] { applyManualCalibration(); };
        addAndMakeVisible(m_manual_apply_button);

        m_status.setComponentID("input_calibration_status");
        m_status.setText(inputCalibrationReadyText(), juce::dontSendNotification);
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
        startTimerHz(g_input_calibration_meter_hz);
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

    void setStatusText(const juce::String& text)
    {
        m_status.setText(text, juce::dontSendNotification);
        syncPreferredSize();
    }

    void setDisplayedInputGain(double gain_db)
    {
        m_input_gain_db = canonicalInputGainDb(gain_db);
        m_manual_gain_slider.setValue(m_input_gain_db, juce::dontSendNotification);
        m_manual_gain_slider.updateText();
    }

    void refreshInputMeter()
    {
        if (m_live_input != nullptr)
        {
            m_input_meter.setLevel(
                applyDisplayGain(m_live_input->rawInputMeterLevel(), m_input_gain_db));
        }
    }

    void updateManualGainPreview()
    {
        if (m_capture.active())
        {
            return;
        }

        m_input_gain_db = canonicalInputGainDb(m_manual_gain_slider.getValue());
        refreshInputMeter();
    }

    void setManualControlsEnabled(bool enabled)
    {
        m_manual_gain_slider.setEnabled(enabled);
        m_manual_apply_button.setEnabled(enabled);
    }

    void applyManualCalibration()
    {
        if (m_capture.active())
        {
            return;
        }

        m_input_gain_db = canonicalInputGainDb(m_manual_gain_slider.getValue());
        auto applied = m_controller.onInputCalibrationManuallySet(m_input_gain_db);
        if (!applied.has_value())
        {
            setStatusText(juce::String{applied.error().message});
            m_calibrate_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        setStatusText(inputManualCalibrationCompleteText(m_input_gain_db));
        m_committed_input_gain_db = m_input_gain_db;
        m_measurement_restore_gain_db = m_input_gain_db;
        m_calibrate_button.setEnabled(true);
        setManualControlsEnabled(true);
        m_cancel_button.setButtonText("Close");
    }

    void startMeasurement()
    {
        if (m_live_input == nullptr)
        {
            setStatusText("Live input is unavailable.");
            m_calibrate_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        auto started = m_controller.onInputCalibrationMeasurementStarted();
        if (!started.has_value())
        {
            setStatusText(juce::String{started.error().message});
            m_calibrate_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        m_measurement_restore_gain_db = m_committed_input_gain_db;
        setDisplayedInputGain(common::audio::defaultGainDb());
        m_capture.start();
        m_last_capture_phase = m_capture.phase();
        // rawInputMeterLevel() clears the Tracktion meter reader, so this primes retry runs
        // after the controller has reset input gain and rebuilt the calibration route.
        (void)m_live_input->rawInputMeterLevel();
        m_calibrate_button.setEnabled(false);
        setManualControlsEnabled(false);
        m_cancel_button.setButtonText("Dismiss");
        setStatusText(inputCalibrationWaitingText());
    }

    void timerCallback() override
    {
        common::audio::AudioMeterLevel level{};
        if (m_live_input != nullptr)
        {
            level = m_live_input->rawInputMeterLevel();
        }
        m_input_meter.setLevel(applyDisplayGain(level, m_input_gain_db));

        if (!m_capture.active())
        {
            return;
        }

        const common::audio::InputCalibrationCaptureUpdate update = m_capture.pushSample(level);
        if (update.phase == common::audio::InputCalibrationCapturePhase::Measuring &&
            m_last_capture_phase != common::audio::InputCalibrationCapturePhase::Measuring)
        {
            setStatusText(inputCalibrationMeasuringText());
        }
        m_last_capture_phase = update.phase;
        if (update.error.has_value())
        {
            finishMeasurementError(juce::String{update.error->message});
            return;
        }
        if (update.result.has_value())
        {
            finishMeasurementSuccess(*update.result, level);
        }
    }

    // Applies a successful automatic capture and mirrors the result in the manual gain control.
    void finishMeasurementSuccess(
        const common::audio::InputCalibrationResult& result, common::audio::AudioMeterLevel level)
    {
        setDisplayedInputGain(result.calibration_gain.db);
        m_input_meter.setLevel(applyDisplayGain(level, m_input_gain_db));

        auto applied = m_controller.onInputCalibrationSucceeded(m_input_gain_db);
        if (!applied.has_value())
        {
            finishMeasurementError(juce::String{applied.error().message});
            return;
        }

        setStatusText(inputCalibrationCompleteText(m_input_gain_db));
        m_capture.reset();
        m_last_capture_phase = m_capture.phase();
        m_committed_input_gain_db = m_input_gain_db;
        m_measurement_restore_gain_db = m_input_gain_db;
        m_calibrate_button.setEnabled(true);
        setManualControlsEnabled(true);
        m_cancel_button.setButtonText("Close");
    }

    // Stops backend calibration monitoring and leaves the popup open for another attempt.
    void finishMeasurementError(const juce::String& message)
    {
        m_controller.onInputCalibrationMeasurementCancelled();
        m_capture.reset();
        m_last_capture_phase = m_capture.phase();
        setDisplayedInputGain(m_measurement_restore_gain_db);
        refreshInputMeter();
        setStatusText(message);
        m_calibrate_button.setEnabled(true);
        setManualControlsEnabled(true);
    }

    InputCalibrationWindow& m_owner;
    core::IEditorController& m_controller;
    const common::audio::ILiveInput* m_live_input{};
    common::audio::InputCalibrationCapture m_capture{
        g_input_calibration_settle_sample_count,
        g_input_calibration_wait_sample_count,
        g_input_calibration_sample_count,
    };
    common::audio::InputCalibrationCapturePhase m_last_capture_phase{
        common::audio::InputCalibrationCapturePhase::Idle,
    };
    double m_input_gain_db{0.0};
    double m_committed_input_gain_db{0.0};
    double m_measurement_restore_gain_db{0.0};
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
    core::InputCalibrationPrompt prompt, juce::Component* centering_component)
    : juce::DocumentWindow(
          "Input Calibration", juce::Colours::darkgrey.darker(0.16f),
          juce::DocumentWindow::closeButton)
    , m_controller(controller)
{
    setComponentID("input_calibration_window");
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
    setContentOwned(new Content{*this, controller, live_input, std::move(prompt)}, true);
    centreAroundComponent(centering_component, getWidth(), getHeight());
    addToDesktop(juce::ComponentPeer::windowHasCloseButton);
    setVisible(true);
    toFront(true);
}

void InputCalibrationWindow::closeButtonPressed()
{
    m_controller.onInputCalibrationDismissed();
    setVisible(false);
}

} // namespace rock_hero::editor::ui
