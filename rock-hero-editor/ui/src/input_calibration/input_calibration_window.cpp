#include "input_calibration_window.h"

#include "shared/audio_level_meter.h"
#include "shared/editor_theme.h"

#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <rock_hero/editor/core/controller/i_editor_controller.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_controller.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Match the transport-bar master meter's preferred width so the popup stays visually compact;
// the live master meter can flex narrower when the window-centered transport needs the room.
constexpr int g_content_width{384};
constexpr int g_margin{14};
constexpr int g_row_height{28};
// Wide enough that "Pickup type" draws at full width rather than squeezed by the Label's scale.
constexpr int g_label_width{96};
constexpr int g_gap{8};
// The meter's bar and, above it, the band its peak target caption and arrow sit in.
constexpr int g_meter_height{46};
constexpr int g_button_width{96};
constexpr int g_pickup_chooser_width{150};

// The guide the "?" opens, resolved from the executable location, else from the build tree.
[[nodiscard]] juce::File documentationFile()
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

// The height a label needs to show text wrapped at its width, read from the label's own font and
// border, so the fixed window fits its longest message instead of a guessed line count. A Label
// draws as many lines as whole font heights fit inside its border.
[[nodiscard]] int wrappedLabelHeight(const juce::Label& label, const juce::String& text, int width)
{
    const juce::Font font = label.getFont();
    const juce::BorderSize<int> border = label.getBorderSize();
    juce::GlyphArrangement glyphs;
    glyphs.addJustifiedText(
        font,
        text,
        0.0F,
        0.0F,
        static_cast<float>(width - border.getLeftAndRight()),
        juce::Justification::topLeft);
    const int lines = std::max(
        1,
        static_cast<int>(
            std::ceil(glyphs.getBoundingBox(0, -1, true).getHeight() / font.getHeight())));
    return (lines * static_cast<int>(std::ceil(font.getHeight()))) + border.getTopAndBottom();
}

void configureGainSlider(juce::Slider& slider)
{
    slider.setComponentID("input_calibration_manual_gain");
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setRange(common::audio::minimumGainDb(), common::audio::maximumGainDb(), 0.1);
    slider.setValue(common::audio::defaultGainDb(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, common::audio::defaultGainDb());
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    slider.setTextValueSuffix(" dB");
    // JUCE appends the suffix after this function (juce_Slider.cpp getTextFromValue), so it
    // supplies only the number.
    slider.textFromValueFunction = [](double value) {
        return juce::String{core::signedGainText(value)};
    };
    // The player typing a gain needs the formula where they type it.
    slider.setTooltip(juce::String{core::gainFormulaText()});
}

} // namespace

// The calibration prompt: one fixed-size screen, so nothing ever resizes its native window. The
// player types a gain or measures one by playing, then Apply stores it.
class InputCalibrationWindow::Content final : public juce::Component,
                                              private juce::Timer,
                                              private core::IInputCalibrationView,
                                              private core::InputCalibrationController::Host
{
public:
    Content(
        InputCalibrationWindow& owner, core::IEditorController& controller,
        const core::InputCalibrationPrompt& prompt)
        : m_owner(owner)
        , m_editor_controller(controller)
        , m_guide(documentationFile())
        , m_calibration_controller(*this, prompt)
        , m_input_meter(AudioLevelMeterOrientation::Horizontal, "Input")
    {
        const std::unique_ptr<juce::Drawable> help_icon =
            juce::Drawable::createFromImageData(BinaryData::help_svg, BinaryData::help_svgSize);
        m_help_button.setComponentID("input_calibration_help_button");
        // Resolved once: a build without its docs disables the "?" and says why, instead of
        // failing when clicked.
        m_help_button.setEnabled(m_guide.existsAsFile());
        m_help_button.setTooltip(
            m_guide.existsAsFile() ? "Open the input calibration guide"
                                   : "The input calibration guide is not installed.");
        m_help_button.setWantsKeyboardFocus(false);
        m_help_button.setMouseClickGrabsKeyboardFocus(false);
        m_help_button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        m_help_button.setImages(help_icon.get());
        m_help_button.onClick = [this] { openGuide(); };
        addAndMakeVisible(m_help_button);

        m_gain_label.setText("Gain", juce::dontSendNotification);
        addAndMakeVisible(m_gain_label);

        configureGainSlider(m_gain_slider);
        m_gain_slider.onValueChange = [this] {
            m_calibration_controller.onManualGainChanged(m_gain_slider.getValue());
        };
        addAndMakeVisible(m_gain_slider);

        m_pickup_label.setText("Pickup type", juce::dontSendNotification);
        addAndMakeVisible(m_pickup_label);

        // The items are common::audio::pickupTypes() in order, so an item's index is its row.
        m_pickup_chooser.setComponentID("input_calibration_pickup");
        int pickup_id = 1;
        for (const common::audio::PickupType& row : common::audio::pickupTypes())
        {
            m_pickup_chooser.addItem(
                juce::String{common::audio::pickupTypeLabel(row.pickups)}, pickup_id);
            ++pickup_id;
        }
        m_pickup_chooser.onChange = [this] {
            const int chosen_index = m_pickup_chooser.getSelectedItemIndex();
            const auto rows = common::audio::pickupTypes();
            if (chosen_index >= 0 && static_cast<std::size_t>(chosen_index) < rows.size())
            {
                m_calibration_controller.onPickupsSelected(
                    rows[static_cast<std::size_t>(chosen_index)].pickups);
            }
        };
        addAndMakeVisible(m_pickup_chooser);

        // One button that starts a measurement and, while one runs, stops it.
        m_measure_button.setComponentID("input_calibration_measure_button");
        m_measure_button.onClick = [this] { m_calibration_controller.onMeasureRequested(); };
        addAndMakeVisible(m_measure_button);

        m_message.setComponentID("input_calibration_message");
        m_message.setJustificationType(juce::Justification::topLeft);
        m_message.setColour(juce::Label::textColourId, editorTheme().primary_text);
        m_message.setMinimumHorizontalScale(1.0F);
        addAndMakeVisible(m_message);

        m_input_meter.setComponentID("input_calibration_meter");
        m_input_meter.setTargetCaption("Peak target");
        addAndMakeVisible(m_input_meter);

        m_apply_button.setComponentID("input_calibration_apply_button");
        m_apply_button.setButtonText("Apply");
        m_apply_button.onClick = [this] { m_calibration_controller.onApplyRequested(); };
        addAndMakeVisible(m_apply_button);

        m_cancel_button.setComponentID("input_calibration_cancel_button");
        m_cancel_button.setButtonText("Cancel");
        m_cancel_button.onClick = [this] { m_owner.closeButtonPressed(); };
        addAndMakeVisible(m_cancel_button);

        // Sized once, for the longest message, so nothing ever resizes the native window.
        setSize(
            g_content_width + (2 * g_margin),
            layOut({g_content_width + (2 * g_margin), std::numeric_limits<int>::max() / 2}));
        m_calibration_controller.attachView(*this);
        startTimerHz(common::audio::inputCalibrationSampleRateHz());
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
        static_cast<void>(layOut(getLocalBounds()));
    }

    void requestDismissal()
    {
        m_calibration_controller.onCloseRequested();
    }

private:
    // Lays the rows out top-down in bounds and returns the height they take.
    [[nodiscard]] int layOut(juce::Rectangle<int> bounds)
    {
        auto area = bounds.reduced(g_margin);

        // The message first, the "?" at its corner, tall enough for the popup's own sentences; the
        // error reasons it passes through are shorter.
        const int message_width = area.getWidth() - g_row_height - g_gap;
        const auto fitted = [this, message_width](const std::string& text) {
            return wrappedLabelHeight(m_message, juce::String{text}, message_width);
        };
        int message_height = std::max(
            {g_row_height,
             fitted(core::idleText()),
             fitted(core::measuringText(common::audio::InputCalibrationWaiting{}))});
        for (const common::audio::PickupType& row : common::audio::pickupTypes())
        {
            message_height = std::max(
                message_height,
                fitted(core::measuredText(common::audio::minimumGainDb(), row.pickups)));
        }
        auto message_row = area.removeFromTop(message_height);
        m_help_button.setBounds(
            message_row.removeFromRight(g_row_height).removeFromTop(g_row_height).reduced(2));
        message_row.removeFromRight(g_gap);
        m_message.setBounds(message_row);
        area.removeFromTop(g_gap);

        m_input_meter.setBounds(area.removeFromTop(g_meter_height));
        area.removeFromTop(g_gap * 2);

        auto pickup_row = area.removeFromTop(g_row_height);
        m_pickup_label.setBounds(pickup_row.removeFromLeft(g_label_width));
        m_pickup_chooser.setBounds(pickup_row.removeFromLeft(g_pickup_chooser_width));
        pickup_row.removeFromLeft(g_gap);
        m_measure_button.setBounds(pickup_row);
        area.removeFromTop(g_gap);

        auto gain_row = area.removeFromTop(g_row_height);
        m_gain_label.setBounds(gain_row.removeFromLeft(g_label_width));
        m_gain_slider.setBounds(gain_row);
        area.removeFromTop(g_gap * 2);

        auto buttons = area.removeFromTop(g_row_height);
        m_cancel_button.setBounds(buttons.removeFromRight(g_button_width));
        buttons.removeFromRight(g_gap);
        m_apply_button.setBounds(buttons.removeFromRight(g_button_width));
        return buttons.getBottom() + g_margin - bounds.getY();
    }

    // Renders the pushed state. While a measurement runs, the gain, the pickups and Apply wait,
    // and the calibrate button stops it.
    void setState(const core::InputCalibrationViewState& state) override
    {
        m_gain_slider.setValue(state.gain_db, juce::dontSendNotification);
        m_gain_slider.updateText();
        m_gain_slider.setEnabled(!state.measuring);
        m_pickup_chooser.setSelectedItemIndex(
            static_cast<int>(std::to_underlying(state.pickups)), juce::dontSendNotification);
        // The chosen kind's description, on demand: the list holds names only.
        m_pickup_chooser.setTooltip(
            juce::String{std::string{common::audio::pickupType(state.pickups).covers}});
        m_pickup_chooser.setEnabled(!state.measuring);
        m_measure_button.setButtonText(state.measuring ? "Stop" : "Measure");
        m_apply_button.setEnabled(!state.measuring);
        m_message.setText(juce::String{state.message}, juce::dontSendNotification);
        m_input_meter.setLevel(state.input_meter_level);
        m_input_meter.setTargetDb(state.meter_target_db);
        m_input_meter.setTooltip(
            state.meter_target_db.has_value() ? juce::String{core::peakTargetText(state.pickups)}
                                              : juce::String{});
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    startInputCalibrationMeasurement(common::audio::PickupClass pickups) override
    {
        return m_editor_controller.onInputCalibrationMeasurementStarted(pickups);
    }

    [[nodiscard]] common::audio::LiveInputSample sampleInputCalibration() override
    {
        return m_editor_controller.onInputCalibrationSampled();
    }

    void stopInputCalibrationMeasurement() override
    {
        m_editor_controller.onInputCalibrationMeasurementStopped();
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError> applyInputCalibration(
        double gain_db) override
    {
        return m_editor_controller.onInputCalibrationApplied(gain_db);
    }

    void closeInputCalibration() override
    {
        m_editor_controller.onInputCalibrationClosed();
    }

    // Opens the local HTML file directly so Windows handles it as a normal filesystem document; a
    // guide that will not open says so rather than failing silently.
    void openGuide()
    {
        if (!m_guide.startAsProcess())
        {
            m_calibration_controller.onDocumentationUnavailable();
        }
    }

    void timerCallback() override
    {
        m_calibration_controller.onSampleTick();
    }

    InputCalibrationWindow& m_owner;
    core::IEditorController& m_editor_controller;
    // The guide the "?" opens; a default File when the build has no docs.
    juce::File m_guide;
    core::InputCalibrationController m_calibration_controller;
    juce::DrawableButton m_help_button{"input_calibration_help", juce::DrawableButton::ImageFitted};
    juce::Label m_gain_label;
    juce::Slider m_gain_slider;
    juce::Label m_pickup_label;
    juce::ComboBox m_pickup_chooser;
    juce::TextButton m_measure_button;
    juce::Label m_message;
    AudioLevelMeter m_input_meter;
    juce::TextButton m_apply_button;
    juce::TextButton m_cancel_button;

    // A single application-wide tooltip window (created on first use, shared across all windows)
    // renders the hover text. Using SharedResourcePointer instead of a per-window instance is
    // JUCE's documented fix for the duplicate-tooltip artifact: two live TooltipWindow instances
    // each register a global mouse listener and can paint overlaid tips. Default (desktop) parent
    // gives the native soft-corner drop-shadow window.
    juce::SharedResourcePointer<juce::TooltipWindow> m_tooltip_window;
};

InputCalibrationWindow::InputCalibrationWindow(
    core::IEditorController& controller, const core::InputCalibrationPrompt& prompt,
    juce::Component* centering_component)
    : juce::DocumentWindow(
          "Input Calibration", editorTheme().bar_background, juce::DocumentWindow::closeButton)
{
    setComponentID("input_calibration_window");
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
    auto content = std::make_unique<Content>(*this, controller, prompt);
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
