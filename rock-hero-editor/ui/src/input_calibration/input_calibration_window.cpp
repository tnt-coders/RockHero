#include "input_calibration_window.h"

#include "shared/audio_level_meter.h"
#include "shared/editor_theme.h"

#include <BinaryData.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/known_interfaces.h>
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
constexpr int g_input_calibration_meter_width{384};
constexpr int g_input_calibration_content_margin{14};
constexpr int g_input_calibration_preferred_width{
    g_input_calibration_meter_width + (g_input_calibration_content_margin * 2)
};
constexpr int g_row_height{28};
// Wide enough that "Interface:" draws at full width rather than squeezed by the Label's scale.
constexpr int g_label_width{74};
constexpr int g_gap{8};
constexpr int g_status_height{48};
constexpr int g_status_to_meter_gap{10};
constexpr int g_meter_height{26};

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

// The chooser's item index for a pickup class: its position in common::audio::pickupClasses().
[[nodiscard]] int pickupClassIndex(common::audio::PickupClass pickups)
{
    const auto classes = common::audio::pickupClasses();
    return static_cast<int>(std::ranges::find(classes, pickups) - classes.begin());
}

// The chooser's item text: the one class name, capitalized for a list.
[[nodiscard]] juce::String pickupClassName(common::audio::PickupClass pickups)
{
    const juce::String name{std::string{common::audio::pickupClassText(pickups)}};
    return name.substring(0, 1).toUpperCase() + name.substring(1);
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
    // JUCE appends the suffix after this function (juce_Slider.cpp getTextFromValue), so it
    // supplies only the number.
    slider.textFromValueFunction = [](double value) {
        return juce::String{core::signedGainText(value)};
    };
    // The one place a player meets the reference: the player typing a gain needs the formula.
    slider.setTooltip(
        "Gain = your interface's dBu at 0 dBFS, minus " +
        juce::String{common::audio::inputLevelReferenceDbu(), 0} + ".");
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
        const core::InputCalibrationPrompt& prompt)
        : m_owner(owner)
        , m_editor_controller(controller)
        , m_calibration_controller(*this, prompt)
        , m_input_meter(AudioLevelMeterOrientation::Horizontal, "Input")
    {
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

        m_interface_label.setComponentID("input_calibration_interface_label");
        m_interface_label.setText("Interface:", juce::dontSendNotification);
        m_interface_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_interface_label);

        m_interface_chooser.setComponentID("input_calibration_interface");
        m_interface_chooser.setTextWhenNothingSelected("Choose your interface");
        int interface_id = 1;
        for (const common::audio::KnownInterface& row : common::audio::knownInterfaces())
        {
            m_interface_chooser.addItem(juce::String{std::string{row.model}}, interface_id);
            ++interface_id;
        }
        m_interface_chooser.onChange = [this] {
            const int chosen_id = m_interface_chooser.getSelectedId();
            if (chosen_id > 0)
            {
                m_calibration_controller.onInterfaceSelected(
                    static_cast<std::size_t>(chosen_id - 1));
            }
        };
        addAndMakeVisible(m_interface_chooser);

        m_pickup_label.setComponentID("input_calibration_pickup_label");
        m_pickup_label.setText("Pickup:", juce::dontSendNotification);
        m_pickup_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_pickup_label);

        // The items are common::audio::pickupClasses() in order, so an item's index is its class.
        m_pickup_chooser.setComponentID("input_calibration_pickup");
        int pickup_id = 1;
        for (const common::audio::PickupClass pickups : common::audio::pickupClasses())
        {
            m_pickup_chooser.addItem(pickupClassName(pickups), pickup_id);
            ++pickup_id;
        }
        m_pickup_chooser.onChange = [this] {
            const int chosen_index = m_pickup_chooser.getSelectedItemIndex();
            const auto classes = common::audio::pickupClasses();
            if (chosen_index >= 0 && static_cast<std::size_t>(chosen_index) < classes.size())
            {
                m_calibration_controller.onPickupsSelected(
                    classes[static_cast<std::size_t>(chosen_index)]);
            }
        };
        addAndMakeVisible(m_pickup_chooser);

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
        // A message, not a control: no box (a dark inset would read as a second field), the
        // labels' own border so the text lines up with them, and top-aligned so a one-line message
        // sits under the gain row it reports on.
        m_status.setJustificationType(juce::Justification::topLeft);
        m_status.setColour(juce::Label::textColourId, editorTheme().primary_text);
        m_status.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(m_status);

        m_calibrate_button.setComponentID("input_calibration_start_button");
        m_calibrate_button.setButtonText("Measure by playing");
        m_calibrate_button.onClick = [this] {
            m_calibration_controller.onMeasurementStartRequested();
        };
        addAndMakeVisible(m_calibrate_button);

        m_cancel_button.setComponentID("input_calibration_cancel_button");
        m_cancel_button.setButtonText("Later");
        m_cancel_button.onClick = [this] { m_owner.closeButtonPressed(); };
        addAndMakeVisible(m_cancel_button);

        setSize(g_input_calibration_preferred_width, preferredHeight());
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
        auto area = getLocalBounds().reduced(g_input_calibration_content_margin);
        auto interface_row = area.removeFromTop(g_row_height);
        m_interface_label.setBounds(interface_row.removeFromLeft(g_label_width));
        m_help_button.setBounds(interface_row.removeFromRight(g_row_height).reduced(2));
        interface_row.removeFromRight(g_gap);
        m_interface_chooser.setBounds(interface_row);
        area.removeFromTop(g_gap);
        auto gain_row = area.removeFromTop(g_row_height);
        m_manual_label.setBounds(gain_row.removeFromLeft(g_label_width));
        m_manual_apply_button.setBounds(gain_row.removeFromRight(72));
        gain_row.removeFromRight(g_gap);
        m_manual_gain_slider.setBounds(gain_row);
        area.removeFromTop(g_gap);
        m_status.setBounds(area.removeFromTop(g_status_height));
        area.removeFromTop(g_status_to_meter_gap);
        m_input_meter.setBounds(area.removeFromTop(g_meter_height));
        auto buttons = area.removeFromBottom(g_row_height);
        m_calibrate_button.setBounds(buttons.removeFromLeft(150));
        m_cancel_button.setBounds(buttons.removeFromRight(96));
        area.removeFromBottom(g_gap);
        auto pickup_row = area.removeFromBottom(g_row_height);
        m_pickup_label.setBounds(pickup_row.removeFromLeft(g_label_width));
        m_pickup_chooser.setBounds(pickup_row.removeFromLeft(160));
    }

    void requestDismissal()
    {
        m_calibration_controller.onDismissRequested();
    }

private:
    [[nodiscard]] static int preferredHeight()
    {
        return (g_input_calibration_content_margin * 2) + g_row_height + g_gap + g_row_height +
               g_gap + g_status_height + g_status_to_meter_gap + g_meter_height + g_gap +
               g_row_height + g_gap + g_row_height;
    }

    void syncPreferredSize()
    {
        setSize(g_input_calibration_preferred_width, preferredHeight());
    }

    // Renders the pushed state; the controller owns every enablement flag.
    void setState(const core::InputCalibrationViewState& state) override
    {
        m_input_meter.setLevel(state.input_meter_level);
        m_manual_gain_slider.setValue(state.input_gain_db, juce::dontSendNotification);
        m_manual_gain_slider.updateText();
        m_status.setText(juce::String{state.status_message}, juce::dontSendNotification);
        const std::optional<std::size_t> selected = state.selected_interface;
        m_interface_chooser.setSelectedId(
            selected.has_value() ? static_cast<int>(*selected) + 1 : 0, juce::dontSendNotification);
        m_interface_chooser.setEnabled(!state.measuring);
        m_pickup_chooser.setSelectedItemIndex(
            pickupClassIndex(state.pickups), juce::dontSendNotification);
        m_pickup_chooser.setEnabled(!state.measuring);
        m_calibrate_button.setEnabled(!state.measuring);
        m_manual_gain_slider.setEnabled(!state.measuring);
        m_manual_apply_button.setEnabled(!state.measuring);
        m_cancel_button.setButtonText(juce::String{state.dismiss_button_text});
        syncPreferredSize();
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

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    applyManualInputCalibration(double gain_db) override
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

    void timerCallback() override
    {
        m_calibration_controller.onSampleTick();
    }

    InputCalibrationWindow& m_owner;
    core::IEditorController& m_editor_controller;
    core::InputCalibrationController m_calibration_controller;
    AudioLevelMeter m_input_meter;
    std::unique_ptr<juce::Drawable> m_help_icon;
    juce::DrawableButton m_help_button{"input_calibration_help", juce::DrawableButton::ImageFitted};
    juce::Label m_interface_label;
    juce::ComboBox m_interface_chooser;
    juce::Label m_pickup_label;
    juce::ComboBox m_pickup_chooser;
    juce::Label m_manual_label;
    juce::Slider m_manual_gain_slider;
    juce::TextButton m_manual_apply_button;
    juce::Label m_status;
    juce::TextButton m_calibrate_button;
    juce::TextButton m_cancel_button;

    // A single application-wide tooltip window (created on first use, shared across all windows)
    // renders the help button's hover text. Using SharedResourcePointer instead of a
    // per-window instance is JUCE's documented fix for the duplicate-tooltip artifact: two live
    // TooltipWindow instances each register a global mouse listener and can paint overlaid tips.
    // Default (desktop) parent gives the native soft-corner drop-shadow window.
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
