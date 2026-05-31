#include "signal_chain_panel.h"

#include <algorithm>
#include <rock_hero/common/audio/gain.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_panel_inset{8};
constexpr int g_add_button_width{112};
constexpr int g_add_button_height{28};
constexpr int g_header_height{34};
constexpr int g_plugin_row_height{24};
constexpr int g_plugin_row_gap{4};
constexpr int g_remove_button_width{72};
constexpr int g_row_button_gap{8};
constexpr int g_output_gain_width{72};
constexpr int g_gain_slider_width{32};
constexpr int g_gain_meter_width{28};
constexpr int g_gain_meter_gap{2};
constexpr int g_gain_meter_vertical_inset{2};
constexpr int g_gain_value_height{20};
constexpr int g_input_control_width{72};
constexpr int g_calibrate_button_height{26};
constexpr int g_output_gain_visual_width{
    g_gain_slider_width + g_gain_meter_gap + g_gain_meter_width
};
const juce::Colour g_panel_background{juce::Colours::darkgrey.darker(0.24f)};
const juce::Colour g_panel_header_background{juce::Colours::darkgrey.darker(0.34f)};
const juce::Colour g_panel_border{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_plugin_row_background{juce::Colours::darkgrey.darker(0.12f)};
const juce::Colour g_plugin_row_hover_background{juce::Colour{0xff263a4c}};
const juce::Colour g_plugin_row_border{juce::Colours::black.withAlpha(0.35f)};
const juce::Colour g_plugin_row_hover_border{juce::Colours::lightskyblue.withAlpha(0.95f)};
const juce::Colour g_plugin_row_hover_accent{juce::Colours::lightskyblue};

// Builds the compact slot label shown in the linear plugin chain.
[[nodiscard]] juce::String pluginLabel(const core::PluginViewState& plugin)
{
    juce::String label{std::to_string(plugin.chain_index + 1)};
    label += ". ";
    label += plugin.name.empty() ? juce::String{"Unnamed Plugin"} : juce::String{plugin.name};

    if (!plugin.manufacturer.empty())
    {
        label += " - ";
        label += juce::String{plugin.manufacturer};
    }

    if (!plugin.format_name.empty())
    {
        label += " (";
        label += juce::String{plugin.format_name};
        label += ")";
    }

    return label;
}

// Keeps JUCE's normal editable slider textbox while shifting only the vertical track left enough
// to sit as a compact pair beside the output meter.
class OutputGainSliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Reuses the stock drawing while aligning the track with the meter column and the textbox
    // with the bottom control row.
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);
        if (slider.getSliderStyle() == juce::Slider::LinearVertical &&
            slider.getTextBoxPosition() == juce::Slider::TextBoxBelow &&
            slider.getWidth() >= g_output_gain_visual_width)
        {
            layout.sliderBounds.setX((slider.getWidth() - g_output_gain_visual_width) / 2);
            layout.sliderBounds.setWidth(g_gain_slider_width);

            const int thumb_radius = getSliderThumbRadius(slider);
            const int meter_top = g_gain_meter_vertical_inset;
            const int meter_height = slider.getHeight() - g_calibrate_button_height -
                                     g_panel_inset - (g_gain_meter_vertical_inset * 2);
            layout.sliderBounds.setY(meter_top + thumb_radius);
            layout.sliderBounds.setHeight(std::max(1, meter_height - (thumb_radius * 2)));

            const int text_box_y = slider.getHeight() - g_calibrate_button_height +
                                   ((g_calibrate_button_height - g_gain_value_height) / 2);
            layout.textBoxBounds.setY(std::max(0, text_box_y));
        }

        return layout;
    }
};

} // namespace

// Presents one plugin-chain row and emits a remove intent for its stored instance ID.
class SignalChainPanel::PluginRowView final : public juce::Component
{
public:
    // Creates the row with a stable plugin snapshot and the parent panel listener.
    PluginRowView(core::PluginViewState plugin, Listener& listener)
        : m_listener(listener)
        , m_plugin(std::move(plugin))
    {
        setComponentID(juce::String{"plugin_row_"} + juce::String{m_plugin.instance_id});
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        m_remove_button.setComponentID(
            juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
        m_remove_button.setButtonText("Remove");
        m_remove_button.setTooltip("Remove plugin");
        m_remove_button.onClick = [this] {
            m_listener.onRemovePluginPressed(m_plugin.instance_id);
        };
        addAndMakeVisible(m_remove_button);
    }

    // Applies controller-derived remove availability to the row button.
    void setRemoveEnabled(bool enabled)
    {
        m_remove_button.setEnabled(enabled);
    }

    // Draws the highlight box around the clickable label area only; the Remove button sits
    // visually beside it so the two interactive zones never overlap.
    void paint(juce::Graphics& g) override
    {
        auto highlight_area = getLocalBounds();
        highlight_area.removeFromRight(g_remove_button_width + g_row_button_gap);

        g.setColour(m_is_hovered ? g_plugin_row_hover_background : g_plugin_row_background);
        g.fillRect(highlight_area);
        if (m_is_hovered)
        {
            g.setColour(g_plugin_row_hover_accent);
            g.fillRect(highlight_area.withWidth(4));
        }

        g.setColour(m_is_hovered ? g_plugin_row_hover_border : g_plugin_row_border);
        g.drawRect(highlight_area, m_is_hovered ? 2 : 1);

        const auto label_area = highlight_area.reduced(8, 0);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(pluginLabel(m_plugin), label_area, juce::Justification::centredLeft, 1);
    }

    // Keeps the remove button fixed on the right side of the row.
    void resized() override
    {
        m_remove_button.setBounds(
            getLocalBounds()
                .reduced(4, 2)
                .removeFromRight(g_remove_button_width)
                .withSizeKeepingCentre(g_remove_button_width, getHeight() - 4));
    }

    // Treats a row click as an editor-window request while ignoring drag releases.
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked())
        {
            m_listener.onOpenPluginPressed(m_plugin.instance_id);
        }
    }

    // Shows that the row itself has a click action independent of the remove button.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = true;
        repaint();
    }

    // Clears the row affordance when the pointer leaves the plugin row.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = false;
        repaint();
    }

private:
    // Listener that receives this row's remove intent.
    Listener& m_listener;

    // Stable plugin snapshot represented by this row.
    core::PluginViewState m_plugin;

    // Button that emits a remove intent for this row's plugin instance.
    juce::TextButton m_remove_button;

    // True while the pointer is over the row, used only for the clickable-row affordance.
    bool m_is_hovered{false};
};

// Configures a vertical gain slider with the shared gain range and dB suffix.
void configureGainSlider(juce::Slider& slider, const juce::String& component_id)
{
    slider.setComponentID(component_id);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setRange(common::audio::minimumGainDb(), common::audio::maximumGainDb(), 0.1);
    slider.setValue(common::audio::defaultGainDb(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, common::audio::defaultGainDb());
    slider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, g_output_gain_width, g_gain_value_height);
    slider.setTextValueSuffix(" dB");
}

// Creates the panel controls and routes the add command through the owner.
SignalChainPanel::SignalChainPanel(Listener& listener)
    : m_listener(listener)
    , m_input_meter(AudioLevelMeterOrientation::Vertical)
    , m_output_gain_slider_look_and_feel(std::make_unique<OutputGainSliderLookAndFeel>())
    , m_output_meter(AudioLevelMeterOrientation::Vertical)
{
    setComponentID("signal_chain_panel");
    m_add_plugin_button.setComponentID("add_plugin_button");
    m_add_plugin_button.setButtonText("Add Plugin");
    m_add_plugin_button.onClick = [this] { m_listener.onAddPluginPressed(); };
    addAndMakeVisible(m_add_plugin_button);

    m_input_meter.setComponentID("input_meter");
    addAndMakeVisible(m_input_meter);
    m_input_calibrate_button.setComponentID("input_calibrate_button");
    m_input_calibrate_button.setButtonText("Calibrate");
    m_input_calibrate_button.onClick = [this] { m_listener.onInputCalibrationPressed(); };
    addAndMakeVisible(m_input_calibrate_button);

    configureGainSlider(m_output_gain_slider, "output_gain_slider");
    m_output_gain_slider.setLookAndFeel(m_output_gain_slider_look_and_feel.get());
    m_output_gain_slider.onValueChange = [this] {
        m_listener.onOutputGainChanged(m_output_gain_slider.getValue());
    };
    addAndMakeVisible(m_output_gain_slider);
    m_output_meter.setComponentID("output_gain_meter");
    // The meter is visually inside the slider component's textbox-width footprint. Let it
    // consume pointer hits so meter clicks do not pass through as slider adjustments.
    m_output_meter.setInterceptsMouseClicks(true, false);
    addAndMakeVisible(m_output_meter);

    setState(core::SignalChainViewState{});
}

// Detaches the custom slider look-and-feel before owned children are destroyed.
SignalChainPanel::~SignalChainPanel()
{
    m_output_gain_slider.setLookAndFeel(nullptr);
}

// Stores the render state and updates controls whose enabledness is derived outside the view.
void SignalChainPanel::setState(const core::SignalChainViewState& state)
{
    m_state = state;
    m_add_plugin_button.setEnabled(m_state.add_plugin_enabled);
    m_input_calibrate_button.setEnabled(m_state.input_calibrate_enabled);
    m_output_gain_slider.setEnabled(m_state.output_gain_controls_enabled);
    m_output_gain_slider.setValue(m_state.output_gain_db, juce::dontSendNotification);
    rebuildPluginRows();
    resized();
    repaint();
}

// Applies the live-rig meter values without rebuilding plugin rows or changing controls.
void SignalChainPanel::setMeterLevels(
    common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level)
{
    m_input_meter.setLevel(input_level);
    m_output_meter.setLevel(output_level);
}

// Draws a compact plugin-chain panel with gain labels and an empty-chain placeholder.
void SignalChainPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(g_panel_background);
    g.setColour(g_panel_border);
    g.drawRect(bounds);

    auto area = bounds.reduced(g_panel_inset);

    // Input label above the left meter.
    const auto input_label_area =
        area.removeFromLeft(g_input_control_width).removeFromTop(g_header_height);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions{12.0f});
    g.drawFittedText("Input", input_label_area, juce::Justification::centred, 1);

    // Output gain label above the right slider and post-fader meter group.
    const auto output_label_area =
        area.removeFromRight(g_output_gain_width).removeFromTop(g_header_height);
    g.drawFittedText("Output", output_label_area, juce::Justification::centred, 1);

    // Center header with title.
    area.removeFromLeft(g_panel_inset);
    area.removeFromRight(g_panel_inset);
    auto header = area.removeFromTop(g_header_height);
    header.removeFromRight(g_add_button_width + g_panel_inset);

    g.setColour(g_panel_header_background);
    g.fillRect(header);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions{16.0f, juce::Font::bold});
    g.drawFittedText("Signal Chain", header.reduced(8, 0), juce::Justification::centredLeft, 1);

    area.removeFromTop(g_panel_inset);
    if (!m_state.disabled_message.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(m_state.disabled_message, area, juce::Justification::centredLeft, 2);
        return;
    }

    if (m_state.plugins.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText("No plugins loaded", area, juce::Justification::centredLeft, 1);
        return;
    }
}

// Keeps the add button in the header area, gain sliders on the sides, and plugin rows in the
// center.
void SignalChainPanel::resized()
{
    auto area = getLocalBounds().reduced(g_panel_inset);

    // Input meter on the left, with calibration command anchored below it.
    auto input_control_area = area.removeFromLeft(g_input_control_width);
    input_control_area.removeFromTop(g_header_height);
    auto calibrate_area = input_control_area.removeFromBottom(
        std::min(g_calibrate_button_height, input_control_area.getHeight()));
    input_control_area.removeFromBottom(std::min(g_panel_inset, input_control_area.getHeight()));
    auto input_meter_area = input_control_area.withSizeKeepingCentre(
        g_gain_meter_width, input_control_area.getHeight());
    m_input_meter.setBounds(input_meter_area.reduced(0, g_gain_meter_vertical_inset));
    m_input_calibrate_button.setBounds(calibrate_area);

    // Output gain flows into its post-fader meter, with one centered readout below the pair.
    auto output_control_area = area.removeFromRight(g_output_gain_width);
    output_control_area.removeFromTop(g_header_height);
    auto output_slider_area = output_control_area.withSizeKeepingCentre(
        g_output_gain_width, output_control_area.getHeight());
    auto output_meter_area = output_slider_area.withTrimmedBottom(
        std::min(g_calibrate_button_height + g_panel_inset, output_slider_area.getHeight()));
    output_meter_area.setX(
        output_slider_area.getX() + ((g_output_gain_width - g_output_gain_visual_width) / 2) +
        g_gain_slider_width + g_gain_meter_gap);
    output_meter_area.setWidth(g_gain_meter_width);
    m_output_gain_slider.setBounds(output_slider_area);
    m_output_meter.setBounds(output_meter_area.reduced(0, g_gain_meter_vertical_inset));

    // Leave a gap between the sliders and the center content.
    area.removeFromLeft(g_panel_inset);
    area.removeFromRight(g_panel_inset);

    auto header = area.removeFromTop(g_header_height);
    m_add_plugin_button.setBounds(
        header.removeFromRight(g_add_button_width)
            .withSizeKeepingCentre(
                g_add_button_width, std::min(g_add_button_height, header.getHeight())));

    area.removeFromTop(g_panel_inset);
    for (const std::unique_ptr<PluginRowView>& row : m_plugin_rows)
    {
        if (row == nullptr)
        {
            continue;
        }

        if (area.getHeight() < g_plugin_row_height)
        {
            row->setVisible(false);
            continue;
        }

        row->setVisible(true);
        row->setBounds(area.removeFromTop(g_plugin_row_height));
        area.removeFromTop(std::min(g_plugin_row_gap, area.getHeight()));
    }
}

// Recreates child rows from the latest controller state so each button carries a stable ID.
void SignalChainPanel::rebuildPluginRows()
{
    for (const std::unique_ptr<PluginRowView>& row : m_plugin_rows)
    {
        if (row != nullptr)
        {
            removeChildComponent(row.get());
        }
    }

    m_plugin_rows.clear();
    if (!m_state.disabled_message.empty())
    {
        return;
    }

    m_plugin_rows.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto row = std::make_unique<PluginRowView>(plugin, m_listener);
        row->setRemoveEnabled(m_state.remove_plugins_enabled);
        addAndMakeVisible(*row);
        m_plugin_rows.push_back(std::move(row));
    }
}

} // namespace rock_hero::editor::ui
