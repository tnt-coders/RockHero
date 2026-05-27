#include "input_calibration_window.h"

#include "audio_level_meter.h"

#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/input_calibration.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <string>
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

// Keeps tiny negative values that display at one decimal from showing up as "-0.0 dB".
[[nodiscard]] double canonicalInputGainDb(double gain_db)
{
    const double rounded_tenths = std::round(gain_db * 10.0);
    return rounded_tenths == 0.0 ? 0.0 : gain_db;
}

[[nodiscard]] juce::String inputGainLabelText(double gain_db)
{
    return juce::String{"Gain: "} + juce::String{canonicalInputGainDb(gain_db), 1} + " dB";
}

[[nodiscard]] juce::String inputCalibrationTargetText()
{
    return juce::String{"Target: "} +
           juce::String{common::audio::inputCalibrationTargetRmsDb(), 0} + " dBFS average, " +
           juce::String{common::audio::inputCalibrationTargetPeakDb(), 0} + " dBFS peak";
}

[[nodiscard]] juce::String inputCalibrationRecommendationText()
{
    return inputCalibrationTargetText() +
           "\n\n"
           "Manual calibration is preferred when exact device specifications are known.\n\n"
           "Use automatic calibration for Windows audio devices such as \"Real Tone\" cables.";
}

[[nodiscard]] juce::String inputCalibrationWaitingText()
{
    return "Waiting for input...\nStrum all open strings at a steady, moderate volume.";
}

[[nodiscard]] juce::String inputCalibrationMeasuringText()
{
    return "Keep strumming all open strings at a steady, moderate volume.";
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

// Inline notification that presents guidance with an embedded info icon.
class InlineInfoNotice final : public juce::Component
{
public:
    InlineInfoNotice()
        : m_icon(
              juce::Drawable::createFromImageData(BinaryData::info_svg, BinaryData::info_svgSize))
    {
        m_text.setComponentID("input_calibration_recommendation_text");
        m_text.setJustificationType(juce::Justification::centredLeft);
        m_text.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        m_text.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(m_text);
    }

    void setText(const juce::String& text)
    {
        m_text.setText(text, juce::dontSendNotification);
    }

    [[nodiscard]] int preferredHeightForWidth(int width) const
    {
        constexpr int icon_column_width{36};
        constexpr int horizontal_padding{16};
        constexpr int vertical_padding{12};
        constexpr int minimum_height{50};

        const int text_width = std::max(1, width - icon_column_width - horizontal_padding);
        const juce::Font font = m_text.getFont();
        const std::string text = m_text.getText().toStdString();
        int visual_line_count = 0;
        std::size_t line_start = 0;
        while (line_start <= text.size())
        {
            const std::size_t line_end = text.find('\n', line_start);
            const std::string line = text.substr(
                line_start, line_end == std::string::npos ? line_end : line_end - line_start);
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(font, juce::String{line}, 0.0f, 0.0f);
            const float line_width = glyphs.getBoundingBox(0, -1, true).getWidth();
            visual_line_count += std::max(
                1, static_cast<int>(std::ceil(line_width / static_cast<float>(text_width))));

            if (line_end == std::string::npos)
            {
                break;
            }

            line_start = line_end + 1;
        }

        const int text_height =
            static_cast<int>(std::ceil(font.getHeight() * static_cast<float>(visual_line_count)));
        return std::max(minimum_height, text_height + vertical_padding);
    }

    // Paints a quiet info panel around the SVG icon and recommendation text.
    void paint(juce::Graphics& graphics) override
    {
        constexpr float corner_radius{5.0f};
        constexpr float border_width{1.2f};
        constexpr int icon_column_width{36};

        const juce::Colour accent{juce::Colour::fromRGB(104, 184, 255)};
        const juce::Rectangle<float> bounds = getLocalBounds().toFloat().reduced(0.5f);
        graphics.setColour(juce::Colour::fromRGB(24, 47, 70).withAlpha(0.86f));
        graphics.fillRoundedRectangle(bounds, corner_radius);
        graphics.setColour(accent.withAlpha(0.95f));
        graphics.drawRoundedRectangle(bounds, corner_radius, border_width);

        const juce::Rectangle<int> icon_area =
            getLocalBounds().removeFromLeft(icon_column_width).reduced(8, 12);
        if (m_icon != nullptr)
        {
            m_icon->drawWithin(
                graphics,
                icon_area.toFloat(),
                juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                1.0f);
        }
    }

    void resized() override
    {
        constexpr int icon_column_width{36};

        auto area = getLocalBounds().reduced(8, 4);
        area.removeFromLeft(icon_column_width);
        m_text.setBounds(area);
    }

private:
    std::unique_ptr<juce::Drawable> m_icon;
    juce::Label m_text;
};

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
        , m_prompt(std::move(prompt))
        , m_input_gain_db(canonicalInputGainDb(m_prompt.input_gain_db))
        , m_input_meter(AudioLevelMeterOrientation::Horizontal, "Input")
    {
        m_recommendation.setComponentID("input_calibration_recommendation");
        m_recommendation.setText(inputCalibrationRecommendationText());
        addAndMakeVisible(m_recommendation);

        m_input_meter.setComponentID("input_calibration_meter");
        addAndMakeVisible(m_input_meter);

        m_gain_label.setComponentID("input_calibration_gain");
        m_gain_label.setText(inputGainLabelText(m_input_gain_db), juce::dontSendNotification);
        m_gain_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_gain_label);

        m_manual_label.setComponentID("input_calibration_manual_label");
        m_manual_label.setText("Manual:", juce::dontSendNotification);
        m_manual_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_manual_label);

        configureManualInputGainSlider(m_manual_gain_slider);
        m_manual_gain_slider.setValue(m_input_gain_db, juce::dontSendNotification);
        m_manual_gain_slider.onValueChange = [this] { updateManualGainPreview(); };
        addAndMakeVisible(m_manual_gain_slider);

        m_manual_apply_button.setComponentID("input_calibration_manual_apply_button");
        m_manual_apply_button.setButtonText("Apply");
        m_manual_apply_button.onClick = [this] { applyManualCalibration(); };
        addAndMakeVisible(m_manual_apply_button);

        m_status.setComponentID("input_calibration_status");
        m_status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_status);
        m_status.setVisible(false);

        m_retry_button.setComponentID("input_calibration_start_button");
        m_retry_button.setButtonText("Calibrate");
        m_retry_button.onClick = [this] { startMeasurement(); };
        addAndMakeVisible(m_retry_button);

        m_cancel_button.setComponentID("input_calibration_cancel_button");
        m_cancel_button.setButtonText("Dismiss");
        m_cancel_button.onClick = [this] { m_owner.closeButtonPressed(); };
        addAndMakeVisible(m_cancel_button);

        constexpr int preferred_width{520};
        setSize(preferred_width, preferredHeightForWidth(preferred_width));
        startTimerHz(g_input_calibration_meter_hz);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        m_recommendation.setBounds(
            area.removeFromTop(m_recommendation.preferredHeightForWidth(area.getWidth())));
        area.removeFromTop(8);
        m_gain_label.setBounds(area.removeFromTop(22));
        area.removeFromTop(4);
        m_input_meter.setBounds(area.removeFromTop(26));
        area.removeFromTop(8);
        auto manual_area = area.removeFromTop(28);
        m_manual_label.setBounds(manual_area.removeFromLeft(60));
        m_manual_apply_button.setBounds(manual_area.removeFromRight(72));
        manual_area.removeFromRight(8);
        m_manual_gain_slider.setBounds(manual_area);
        if (m_status.isVisible())
        {
            area.removeFromTop(8);
            m_status.setBounds(area.removeFromTop(44));
        }
        else
        {
            m_status.setBounds({});
        }

        area.removeFromTop(8);
        auto buttons = area.removeFromBottom(28);
        m_cancel_button.setBounds(buttons.removeFromRight(96));
        buttons.removeFromRight(8);
        m_retry_button.setBounds(buttons.removeFromRight(96));
    }

private:
    [[nodiscard]] int preferredHeightForWidth(int width) const
    {
        constexpr int outer_margin{28};
        constexpr int gap_height{8};
        constexpr int gain_label_height{22};
        constexpr int meter_gap_height{4};
        constexpr int meter_height{26};
        constexpr int manual_controls_height{28};
        constexpr int status_height{44};
        constexpr int buttons_height{28};

        const int content_width = std::max(1, width - outer_margin);
        const int status_section_height = m_status.isVisible() ? gap_height + status_height : 0;
        return outer_margin + m_recommendation.preferredHeightForWidth(content_width) + gap_height +
               gain_label_height + meter_gap_height + meter_height + gap_height +
               manual_controls_height + status_section_height + gap_height + buttons_height;
    }

    void syncPreferredSize()
    {
        constexpr int preferred_width{520};
        setSize(preferred_width, preferredHeightForWidth(preferred_width));
    }

    void setStatusText(const juce::String& text)
    {
        m_status.setText(text, juce::dontSendNotification);
        m_status.setVisible(text.isNotEmpty());
        syncPreferredSize();
    }

    enum class CalibrationPhase
    {
        Idle,
        Settling,
        WaitingForInput,
        Measuring,
    };

    void updateInputGainLabel()
    {
        m_gain_label.setText(inputGainLabelText(m_input_gain_db), juce::dontSendNotification);
    }

    void updateManualGainPreview()
    {
        if (m_phase != CalibrationPhase::Idle)
        {
            return;
        }

        m_input_gain_db = canonicalInputGainDb(m_manual_gain_slider.getValue());
        updateInputGainLabel();
        if (m_live_input != nullptr)
        {
            m_input_meter.setLevel(
                applyDisplayGain(m_live_input->rawInputMeterLevel(), m_input_gain_db));
        }
    }

    void setManualControlsEnabled(bool enabled)
    {
        m_manual_gain_slider.setEnabled(enabled);
        m_manual_apply_button.setEnabled(enabled);
    }

    void applyManualCalibration()
    {
        if (m_phase != CalibrationPhase::Idle)
        {
            return;
        }

        m_input_gain_db = canonicalInputGainDb(m_manual_gain_slider.getValue());
        updateInputGainLabel();
        auto applied = m_controller.onInputCalibrationManuallySet(m_input_gain_db);
        if (!applied.has_value())
        {
            setStatusText(juce::String{applied.error().message});
            m_retry_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        setStatusText("Manual calibration saved.");
        m_retry_button.setEnabled(true);
        setManualControlsEnabled(true);
        m_cancel_button.setButtonText("Close");
    }

    void startMeasurement()
    {
        if (m_live_input == nullptr)
        {
            setStatusText("Live input is unavailable.");
            m_retry_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        auto started = m_controller.onInputCalibrationMeasurementStarted();
        if (!started.has_value())
        {
            setStatusText(juce::String{started.error().message});
            m_retry_button.setEnabled(true);
            setManualControlsEnabled(true);
            return;
        }

        m_accumulator.reset();
        m_input_gain_db = common::audio::defaultGainDb();
        m_manual_gain_slider.setValue(m_input_gain_db, juce::dontSendNotification);
        updateInputGainLabel();
        m_samples_remaining = 0;
        m_wait_samples_remaining = g_input_calibration_wait_sample_count;
        m_settle_samples_remaining = g_input_calibration_settle_sample_count;
        m_phase = CalibrationPhase::Settling;
        // rawInputMeterLevel() clears the Tracktion meter reader, so this primes retry runs
        // after the controller has reset input gain and rebuilt the calibration route.
        (void)m_live_input->rawInputMeterLevel();
        m_retry_button.setEnabled(false);
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

        if (m_phase == CalibrationPhase::Idle)
        {
            return;
        }

        if (m_phase == CalibrationPhase::Settling)
        {
            handleSettlingSample();
            return;
        }

        if (m_phase == CalibrationPhase::WaitingForInput)
        {
            handleWaitingSample(level);
            return;
        }

        handleMeasurementSample(level);
    }

    // Discards transient meter samples immediately after retry resets the backend route.
    void handleSettlingSample()
    {
        --m_settle_samples_remaining;
        if (m_settle_samples_remaining <= 0)
        {
            m_phase = CalibrationPhase::WaitingForInput;
        }
    }

    // Waits for a real input signal before starting the fixed calibration capture window.
    void handleWaitingSample(common::audio::AudioMeterLevel level)
    {
        if (level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb())
        {
            finishMeasurementError("Input clipped. Lower the interface input gain and try again.");
            return;
        }

        if (level.peak_db >= common::audio::minimumInputCalibrationSignalDb())
        {
            m_accumulator.reset();
            m_samples_remaining = g_input_calibration_sample_count;
            m_phase = CalibrationPhase::Measuring;
            setStatusText(inputCalibrationMeasuringText());
            handleMeasurementSample(level);
            return;
        }

        --m_wait_samples_remaining;
        if (m_wait_samples_remaining <= 0)
        {
            finishMeasurementError(
                "No usable input signal was detected. Check the input and try again.");
        }
    }

    // Accumulates active-window RMS and finalizes calibration once the capture window ends.
    void handleMeasurementSample(common::audio::AudioMeterLevel level)
    {
        m_accumulator.pushSample(level);
        --m_samples_remaining;
        if (m_samples_remaining > 0)
        {
            return;
        }

        m_phase = CalibrationPhase::Idle;
        const auto result = common::audio::calculateInputCalibration(m_accumulator.measurement());
        if (!result.has_value())
        {
            finishMeasurementError(juce::String{result.error().message});
            return;
        }

        m_input_gain_db = canonicalInputGainDb(result->calibration_gain.db);
        m_manual_gain_slider.setValue(m_input_gain_db, juce::dontSendNotification);
        updateInputGainLabel();
        m_input_meter.setLevel(applyDisplayGain(level, m_input_gain_db));

        auto applied = m_controller.onInputCalibrationSucceeded(m_input_gain_db);
        if (!applied.has_value())
        {
            finishMeasurementError(juce::String{applied.error().message});
            return;
        }

        setStatusText("Calibration complete. You can recalibrate or set the gain manually.");
        m_retry_button.setEnabled(true);
        m_retry_button.setButtonText("Retry");
        setManualControlsEnabled(true);
        m_cancel_button.setButtonText("Close");
    }

    // Stops backend calibration monitoring and leaves the popup open for another attempt.
    void finishMeasurementError(const juce::String& message)
    {
        m_controller.onInputCalibrationMeasurementCancelled();
        m_accumulator.reset();
        m_phase = CalibrationPhase::Idle;
        m_samples_remaining = 0;
        m_wait_samples_remaining = 0;
        m_settle_samples_remaining = 0;
        setStatusText(message);
        m_retry_button.setEnabled(true);
        setManualControlsEnabled(true);
    }

    InputCalibrationWindow& m_owner;
    core::IEditorController& m_controller;
    const common::audio::ILiveInput* m_live_input{};
    core::InputCalibrationPrompt m_prompt;
    common::audio::InputCalibrationAccumulator m_accumulator;
    double m_input_gain_db{0.0};
    AudioLevelMeter m_input_meter;
    InlineInfoNotice m_recommendation;
    juce::Label m_gain_label;
    juce::Label m_manual_label;
    juce::Slider m_manual_gain_slider;
    juce::TextButton m_manual_apply_button;
    juce::Label m_status;
    juce::TextButton m_retry_button;
    juce::TextButton m_cancel_button;
    int m_samples_remaining{0};
    int m_wait_samples_remaining{0};
    int m_settle_samples_remaining{0};
    CalibrationPhase m_phase{CalibrationPhase::Idle};
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
