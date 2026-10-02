#include "input_calibration_window.h"

#include "shared/audio_level_meter.h"
#include "shared/editor_theme.h"

#include <BinaryData.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/known_interfaces.h>
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
constexpr int g_input_calibration_meter_width{384};
constexpr int g_input_calibration_content_margin{14};
constexpr int g_input_calibration_preferred_width{
    g_input_calibration_meter_width + (g_input_calibration_content_margin * 2)
};
constexpr int g_row_height{28};
// Wide enough that "Audio device" draws at full width rather than squeezed by the Label's scale.
constexpr int g_label_width{96};
constexpr int g_gap{8};
constexpr int g_status_height{48};
constexpr int g_status_to_meter_gap{10};
constexpr int g_meter_height{26};

// Resolves an installed docs page from the executable location and falls back to build-tree docs.
[[nodiscard]] juce::File documentationFile(const juce::String& documentation_file_name)
{
    constexpr int maximum_directory_search_depth{8};
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

// Opens the local HTML file directly so Windows handles it as a normal filesystem document. The
// shell drops a #fragment from a file URL, so a help target inside the guide is a forwarding page
// of its own (docs/redirects) that refreshes to the section.
[[nodiscard]] bool openDocumentation(const juce::String& documentation_file_name)
{
    const juce::File documentation = documentationFile(documentation_file_name);
    return documentation.existsAsFile() && documentation.startAsProcess();
}

// A row's "?": the help icon, opening one guide page.
void configureHelpButton(
    juce::DrawableButton& button, const juce::Drawable* icon, const juce::String& tooltip)
{
    button.setTooltip(tooltip);
    button.setWantsKeyboardFocus(false);
    button.setMouseClickGrabsKeyboardFocus(false);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    button.setImages(icon);
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
        "Gain = your audio device's dBu at 0 dBFS, minus " +
        juce::String{common::audio::inputLevelReferenceDbu(), 0} + ".");
}

// A disclosure header: a chevron and a line of text, no fill, that opens the section under it. Its
// toggle state renders the open state its owner pushes; a click only asks for the other one.
class DisclosureButton final : public juce::Button
{
public:
    explicit DisclosureButton(const juce::String& header_text)
        : juce::Button(header_text)
    {
        setWantsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool is_over, bool is_down) override
    {
        const EditorTheme& theme = editorTheme();
        juce::Colour ink = theme.primary_text;
        if (!isEnabled())
        {
            ink = theme.muted_text;
        }
        else if (is_over || is_down)
        {
            ink = theme.accent;
        }
        g.setColour(ink);

        // The chevron points at the text while closed and down at the opened section.
        constexpr float chevron_size{8.0F};
        constexpr int chevron_column{16};
        auto bounds = getLocalBounds();
        const juce::Rectangle<float> chevron =
            bounds.removeFromLeft(chevron_column)
                .toFloat()
                .withSizeKeepingCentre(chevron_size, chevron_size);
        juce::Path triangle;
        if (getToggleState())
        {
            triangle.addTriangle(
                chevron.getTopLeft(),
                chevron.getTopRight(),
                {chevron.getCentreX(), chevron.getBottom()});
        }
        else
        {
            triangle.addTriangle(
                chevron.getTopLeft(),
                chevron.getBottomLeft(),
                {chevron.getRight(), chevron.getCentreY()});
        }
        g.fillPath(triangle);

        // A Label's default height, so the header reads as the same text as the rows above it.
        g.setFont(juce::Font{juce::FontOptions{15.0F}});
        g.drawText(getButtonText(), bounds, juce::Justification::centredLeft, true);
    }
};

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
        const std::unique_ptr<juce::Drawable> help_icon =
            juce::Drawable::createFromImageData(BinaryData::help_svg, BinaryData::help_svgSize);
        m_help_button.setComponentID("input_calibration_help_button");
        configureHelpButton(m_help_button, help_icon.get(), "Open the known audio devices table");
        m_help_button.onClick = [this] { openGuidePage("user_known_audio_devices.html"); };
        addAndMakeVisible(m_help_button);

        m_pickup_help_button.setComponentID("input_calibration_pickup_help_button");
        configureHelpButton(m_pickup_help_button, help_icon.get(), "Open the pickup types table");
        m_pickup_help_button.onClick = [this] { openGuidePage("user_pickup_types.html"); };
        addChildComponent(m_pickup_help_button);

        m_interface_label.setComponentID("input_calibration_interface_label");
        m_interface_label.setText("Audio device", juce::dontSendNotification);
        m_interface_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_interface_label);

        m_interface_chooser.setComponentID("input_calibration_interface");
        m_interface_chooser.setTextWhenNothingSelected("Choose your audio device");
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
        m_pickup_label.setText("Pickup type", juce::dontSendNotification);
        m_pickup_label.setJustificationType(juce::Justification::centredLeft);
        addChildComponent(m_pickup_label);

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
        addChildComponent(m_pickup_chooser);

        m_input_meter.setComponentID("input_calibration_meter");
        addAndMakeVisible(m_input_meter);

        m_manual_label.setComponentID("input_calibration_manual_label");
        m_manual_label.setText("Gain", juce::dontSendNotification);
        m_manual_label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_manual_label);

        configureManualInputGainSlider(m_manual_gain_slider);
        m_manual_gain_slider.onValueChange = [this] {
            m_calibration_controller.onManualGainChanged(m_manual_gain_slider.getValue());
        };
        addAndMakeVisible(m_manual_gain_slider);

        m_manual_apply_button.setComponentID("input_calibration_manual_apply_button");
        m_manual_apply_button.setButtonText("Apply");
        m_manual_apply_button.onClick = [this] { m_calibration_controller.onApplyRequested(); };
        addAndMakeVisible(m_manual_apply_button);

        m_status.setComponentID("input_calibration_status");
        // A message, not a control: no box (a dark inset would read as a second field), the
        // labels' own border so the text lines up with them, and top-aligned so a one-line message
        // sits under the gain row it reports on.
        m_status.setJustificationType(juce::Justification::topLeft);
        m_status.setColour(juce::Label::textColourId, editorTheme().primary_text);
        m_status.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(m_status);

        // Measuring is the fallback for an interface the list lacks, so it waits behind a header
        // and the window opens on the two direct routes.
        m_measure_disclosure.setComponentID("input_calibration_measure_disclosure");
        m_measure_disclosure.onClick = [this] {
            m_calibration_controller.onMeasurementSectionToggled(
                !m_measure_disclosure.getToggleState());
        };
        addAndMakeVisible(m_measure_disclosure);

        m_calibrate_button.setComponentID("input_calibration_start_button");
        m_calibrate_button.setButtonText("Start Calibration");
        m_calibrate_button.onClick = [this] {
            m_calibration_controller.onMeasurementStartRequested();
        };
        addChildComponent(m_calibrate_button);

        m_cancel_button.setComponentID("input_calibration_cancel_button");
        m_cancel_button.setButtonText("Cancel");
        m_cancel_button.onClick = [this] { m_owner.closeButtonPressed(); };
        addAndMakeVisible(m_cancel_button);

        // Attaching pushes the first state, which sizes the window to it.
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
        layOut(getLocalBounds());
    }

    void requestDismissal()
    {
        m_calibration_controller.onCloseRequested();
    }

private:
    // Lays the rows out top-down in bounds and returns the height they take, so the window's size
    // comes from the one layout rather than a second sum of it.
    int layOut(juce::Rectangle<int> bounds)
    {
        auto area = bounds.reduced(g_input_calibration_content_margin);
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
        area.removeFromTop(g_gap);
        m_measure_disclosure.setBounds(area.removeFromTop(g_row_height));
        if (m_measure_disclosure.getToggleState())
        {
            area.removeFromTop(g_gap);
            auto pickup_row = area.removeFromTop(g_row_height);
            m_pickup_label.setBounds(pickup_row.removeFromLeft(g_label_width));
            m_pickup_chooser.setBounds(pickup_row.removeFromLeft(160));
            pickup_row.removeFromLeft(g_gap);
            m_pickup_help_button.setBounds(pickup_row.removeFromLeft(g_row_height).reduced(2));
        }
        area.removeFromTop(g_gap);
        auto buttons = area.removeFromTop(g_row_height);
        m_calibrate_button.setBounds(buttons.removeFromLeft(150));
        m_cancel_button.setBounds(buttons.removeFromRight(96));
        return area.getY() - bounds.getY() + g_input_calibration_content_margin;
    }

    // Opens or closes the measuring section under its header, and fits the window to it.
    void showMeasurementSection(bool open)
    {
        m_measure_disclosure.setToggleState(open, juce::dontSendNotification);
        m_pickup_label.setVisible(open);
        m_pickup_chooser.setVisible(open);
        m_pickup_help_button.setVisible(open);
        m_calibrate_button.setVisible(open);
        constexpr int unbounded_height{1 << 15};
        setSize(
            g_input_calibration_preferred_width,
            layOut({g_input_calibration_preferred_width, unbounded_height}));
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
            static_cast<int>(std::to_underlying(state.pickups)), juce::dontSendNotification);
        m_pickup_chooser.setEnabled(!state.measuring);
        m_calibrate_button.setEnabled(!state.measuring);
        m_measure_disclosure.setEnabled(!state.measuring);
        m_manual_gain_slider.setEnabled(!state.measuring);
        m_manual_apply_button.setEnabled(!state.measuring);
        showMeasurementSection(state.measurement_section_open);
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

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError> applyInputCalibration(
        double gain_db) override
    {
        return m_editor_controller.onInputCalibrationApplied(gain_db);
    }

    void closeInputCalibration() override
    {
        m_editor_controller.onInputCalibrationClosed();
    }

    // Reports missing generated docs in the popup instead of letting a help button fail silently.
    void openGuidePage(const juce::String& documentation_file_name)
    {
        if (openDocumentation(documentation_file_name))
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
    juce::DrawableButton m_help_button{"input_calibration_help", juce::DrawableButton::ImageFitted};
    juce::DrawableButton m_pickup_help_button{
        "input_calibration_pickup_help", juce::DrawableButton::ImageFitted
    };
    juce::Label m_interface_label;
    juce::ComboBox m_interface_chooser;
    juce::Label m_pickup_label;
    juce::ComboBox m_pickup_chooser;
    juce::Label m_manual_label;
    juce::Slider m_manual_gain_slider;
    juce::TextButton m_manual_apply_button;
    juce::Label m_status;
    DisclosureButton m_measure_disclosure{"Device not listed? Calibrate by playing"};
    juce::TextButton m_calibrate_button;
    juce::TextButton m_cancel_button;

    // A single application-wide tooltip window (created on first use, shared across all windows)
    // renders the help buttons' hover text. Using SharedResourcePointer instead of a
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
