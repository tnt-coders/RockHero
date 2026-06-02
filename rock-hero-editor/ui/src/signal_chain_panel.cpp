#include "signal_chain_panel.h"

#include <algorithm>
#include <optional>
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
constexpr int g_insert_slot_height{18};
constexpr int g_plugin_row_height{28};
constexpr int g_plugin_row_gap{3};
constexpr int g_row_icon_button_width{42};
constexpr int g_remove_button_width{64};
constexpr int g_row_button_gap{4};
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
const juce::Colour g_insert_slot_line{juce::Colours::lightgrey.withAlpha(0.36f)};
const juce::Colour g_insert_slot_drop_fill{juce::Colour{0xff1f3447}};
const juce::Colour g_insert_slot_drop_line{juce::Colours::lightskyblue};
constexpr const char* g_plugin_drag_prefix{"rockhero.signal-chain.plugin:"};
constexpr int g_plugin_drag_prefix_length{static_cast<int>(
    std::char_traits<char>::length(g_plugin_drag_prefix))};

struct DraggedPlugin
{
    std::string instance_id;
    std::size_t source_index{};
};

// Computes the full scrollable content height for slots and rows.
[[nodiscard]] int chainContentHeight(std::size_t plugin_count) noexcept
{
    const auto slot_count = static_cast<int>(plugin_count + 1);
    const auto row_count = static_cast<int>(plugin_count);
    return (slot_count * g_insert_slot_height) +
           (row_count * (g_plugin_row_height + (g_plugin_row_gap * 2)));
}

// Encodes enough row state for slot drop targets to compute final move destinations.
[[nodiscard]] juce::String makePluginDragDescription(const core::PluginViewState& plugin)
{
    juce::String description{g_plugin_drag_prefix};
    description += juce::String{std::to_string(plugin.chain_index)};
    description += ":";
    description += juce::String{plugin.instance_id};
    return description;
}

// Decodes a plugin-row drag payload while rejecting unrelated JUCE drag operations.
[[nodiscard]] std::optional<DraggedPlugin> parsePluginDragDescription(const juce::var& description)
{
    if (!description.isString())
    {
        return std::nullopt;
    }

    const juce::String text = description.toString();
    if (!text.startsWith(g_plugin_drag_prefix))
    {
        return std::nullopt;
    }

    const juce::String body = text.substring(g_plugin_drag_prefix_length);
    const int separator = body.indexOfChar(':');
    if (separator <= 0)
    {
        return std::nullopt;
    }

    const juce::String source_index_text = body.substring(0, separator);
    if (!source_index_text.containsOnly("0123456789"))
    {
        return std::nullopt;
    }

    const juce::String instance_id = body.substring(separator + 1);
    if (instance_id.isEmpty())
    {
        return std::nullopt;
    }

    return DraggedPlugin{
        .instance_id = instance_id.toStdString(),
        .source_index = static_cast<std::size_t>(source_index_text.getIntValue()),
    };
}

// Translates an insertion slot into the final destination index after removing the source row.
[[nodiscard]] std::optional<std::size_t> destinationIndexForDrop(
    std::size_t source_index, std::size_t slot_index, std::size_t plugin_count) noexcept
{
    if (plugin_count < 2 || source_index >= plugin_count || slot_index > plugin_count)
    {
        return std::nullopt;
    }

    std::size_t destination_index = slot_index;
    if (source_index < slot_index)
    {
        destination_index -= 1;
    }
    if (destination_index >= plugin_count)
    {
        destination_index = plugin_count - 1;
    }
    if (destination_index == source_index)
    {
        return std::nullopt;
    }

    return destination_index;
}

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

// Presents one insertion slot between plugin rows and accepts plugin-row drops.
class SignalChainPanel::InsertSlotView final : public juce::Component,
                                               public juce::DragAndDropTarget
{
public:
    // Creates the slot control for a stable chain insertion index.
    InsertSlotView(std::size_t chain_index, SignalChainPanel& panel, Listener& listener)
        : m_panel(panel)
        , m_listener(listener)
        , m_chain_index(chain_index)
    {
        const juce::String chain_index_text{std::to_string(m_chain_index)};
        setComponentID(juce::String{"insert_slot_"} + chain_index_text);
        m_button.setComponentID(juce::String{"insert_plugin_button_"} + chain_index_text);
        m_button.setButtonText("+");
        m_button.setTooltip("Insert plugin here");
        m_button.onClick = [this] { m_listener.onInsertPluginPressed(m_chain_index); };
        addAndMakeVisible(m_button);
    }

    // Applies controller-derived editing availability to the slot.
    void setEditingEnabled(bool insert_enabled, bool move_enabled)
    {
        m_button.setEnabled(insert_enabled);
        m_drop_enabled = move_enabled;
        if (!m_drop_enabled && m_is_drag_hovered)
        {
            m_is_drag_hovered = false;
            repaint();
        }
    }

    // Reports whether dropping the current payload here would actually relocate the row. Slots
    // adjacent to the dragged row resolve to a no-op, so they must decline interest rather than
    // highlight a drop that would do nothing.
    [[nodiscard]] bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        if (!m_drop_enabled)
        {
            return false;
        }

        const std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        if (!plugin.has_value())
        {
            return false;
        }

        return destinationIndexForDrop(
                   plugin->source_index, m_chain_index, m_panel.m_state.plugins.size())
            .has_value();
    }

    // Highlights the slot as a concrete drop target.
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        if (!isInterestedInDragSource(drag_source_details))
        {
            return;
        }

        m_is_drag_hovered = true;
        repaint();
    }

    // Clears the drop highlight when the drag leaves the slot.
    void itemDragExit(
        const juce::DragAndDropTarget::SourceDetails& /*drag_source_details*/) override
    {
        m_is_drag_hovered = false;
        repaint();
    }

    // Emits the same move intent used by keyboard/button move paths.
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        m_is_drag_hovered = false;
        repaint();

        if (!m_drop_enabled)
        {
            return;
        }

        std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        if (!plugin.has_value())
        {
            return;
        }

        m_panel.movePluginToInsertionSlot(
            std::move(plugin->instance_id), plugin->source_index, m_chain_index);
    }

    // Draws the slot as a thin insertion rail, with stronger feedback while dragging.
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        if (m_is_drag_hovered)
        {
            g.setColour(g_insert_slot_drop_fill);
            g.fillRect(area);
        }

        const int y = area.getCentreY();
        g.setColour(m_is_drag_hovered ? g_insert_slot_drop_line : g_insert_slot_line);
        g.drawLine(
            0.0f,
            static_cast<float>(y),
            static_cast<float>(area.getWidth()),
            static_cast<float>(y),
            m_is_drag_hovered ? 2.0f : 1.0f);
    }

    // Keeps the compact insertion button centered in the available strip.
    void resized() override
    {
        m_button.setBounds(getLocalBounds().withSizeKeepingCentre(32, std::max(14, getHeight())));
    }

private:
    // Owning panel used to translate drops into move intents.
    SignalChainPanel& m_panel;

    // Listener that receives this slot's insert intent.
    Listener& m_listener;

    // Stable user-visible chain slot represented by this control.
    std::size_t m_chain_index{};

    // Compact insertion command button.
    juce::TextButton m_button;

    // True when plugin rows may be dropped on this insertion slot.
    bool m_drop_enabled{false};

    // True while a compatible row drag is hovering over this slot.
    bool m_is_drag_hovered{false};
};

// Presents one plugin-chain row and emits row edit intents for its stored instance ID.
class SignalChainPanel::PluginRowView final : public juce::Component
{
public:
    // Creates the row with a stable plugin snapshot and the parent panel listener.
    PluginRowView(core::PluginViewState plugin, std::size_t plugin_count, Listener& listener)
        : m_listener(listener)
        , m_plugin(std::move(plugin))
        , m_plugin_count(plugin_count)
    {
        setComponentID(juce::String{"plugin_row_"} + juce::String{m_plugin.instance_id});
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        m_move_up_button.setComponentID(
            juce::String{"move_plugin_up_button_"} + juce::String{m_plugin.instance_id});
        m_move_up_button.setButtonText("Up");
        m_move_up_button.setTooltip("Move plugin up");
        m_move_up_button.onClick = [this] {
            if (m_plugin.chain_index > 0)
            {
                m_listener.onMovePluginPressed(m_plugin.instance_id, m_plugin.chain_index - 1);
            }
        };
        addAndMakeVisible(m_move_up_button);

        m_move_down_button.setComponentID(
            juce::String{"move_plugin_down_button_"} + juce::String{m_plugin.instance_id});
        m_move_down_button.setButtonText("Down");
        m_move_down_button.setTooltip("Move plugin down");
        m_move_down_button.onClick = [this] {
            if (m_plugin.chain_index + 1 < m_plugin_count)
            {
                m_listener.onMovePluginPressed(m_plugin.instance_id, m_plugin.chain_index + 1);
            }
        };
        addAndMakeVisible(m_move_down_button);

        m_remove_button.setComponentID(
            juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
        m_remove_button.setButtonText("Remove");
        m_remove_button.setTooltip("Remove plugin");
        m_remove_button.onClick = [this] {
            m_listener.onRemovePluginPressed(m_plugin.instance_id);
        };
        addAndMakeVisible(m_remove_button);
    }

    // Applies controller-derived edit availability to the row buttons.
    void setEditEnabled(bool move_enabled, bool remove_enabled)
    {
        m_move_enabled = move_enabled;
        m_move_up_button.setEnabled(move_enabled && m_plugin.chain_index > 0);
        m_move_down_button.setEnabled(move_enabled && m_plugin.chain_index + 1 < m_plugin_count);
        m_remove_button.setEnabled(remove_enabled);
    }

    // Draws the highlight box around the clickable label area only; the Remove button sits
    // visually beside it so the two interactive zones never overlap.
    void paint(juce::Graphics& g) override
    {
        auto highlight_area = getLocalBounds();
        const int button_area_width =
            (g_row_icon_button_width * 2) + g_remove_button_width + (g_row_button_gap * 3);
        highlight_area.removeFromRight(button_area_width);

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
        auto button_area = getLocalBounds().reduced(4, 3);
        m_remove_button.setBounds(
            button_area.removeFromRight(g_remove_button_width)
                .withSizeKeepingCentre(g_remove_button_width, button_area.getHeight()));
        button_area.removeFromRight(std::min(g_row_button_gap, button_area.getWidth()));
        m_move_down_button.setBounds(
            button_area.removeFromRight(g_row_icon_button_width)
                .withSizeKeepingCentre(g_row_icon_button_width, button_area.getHeight()));
        button_area.removeFromRight(std::min(g_row_button_gap, button_area.getWidth()));
        m_move_up_button.setBounds(
            button_area.removeFromRight(g_row_icon_button_width)
                .withSizeKeepingCentre(g_row_icon_button_width, button_area.getHeight()));
    }

    // Resets drag-start state at the beginning of each pointer sequence.
    void mouseDown(const juce::MouseEvent& /*event*/) override
    {
        m_drag_started = false;
    }

    // Starts a JUCE drag operation for reorderable plugin rows.
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!m_move_enabled || m_drag_started || m_plugin_count < 2 ||
            !event.mouseWasDraggedSinceMouseDown())
        {
            return;
        }

        juce::DragAndDropContainer* const container =
            juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container == nullptr)
        {
            return;
        }

        m_drag_started = true;
        container->startDragging(
            makePluginDragDescription(m_plugin),
            this,
            juce::ScaledImage(),
            false,
            nullptr,
            &event.source);
    }

    // Treats a row click as an editor-window request while ignoring drag releases.
    void mouseUp(const juce::MouseEvent& event) override
    {
        m_drag_started = false;
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

    // Total user-visible plugin count used to disable edge move controls.
    std::size_t m_plugin_count{};

    // Button that emits a move-up intent for this row's plugin instance.
    juce::TextButton m_move_up_button;

    // Button that emits a move-down intent for this row's plugin instance.
    juce::TextButton m_move_down_button;

    // Button that emits a remove intent for this row's plugin instance.
    juce::TextButton m_remove_button;

    // True while the pointer is over the row, used only for the clickable-row affordance.
    bool m_is_hovered{false};

    // True when the row can initiate drag-based reordering.
    bool m_move_enabled{false};

    // Prevents repeated startDragging() calls during one mouse drag sequence.
    bool m_drag_started{false};
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

    m_chain_viewport.setComponentID("signal_chain_viewport");
    m_chain_content.setComponentID("signal_chain_content");
    m_chain_viewport.setViewedComponent(&m_chain_content, false);
    m_chain_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_chain_viewport);

    setState(core::SignalChainViewState{});
}

// Detaches the custom slider look-and-feel before owned children are destroyed.
SignalChainPanel::~SignalChainPanel()
{
    m_chain_viewport.setViewedComponent(nullptr, false);
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
    m_chain_viewport.setVisible(m_state.disabled_message.empty());
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
    const auto chain_area = area;
    if (!m_state.disabled_message.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(m_state.disabled_message, area, juce::Justification::centredLeft, 2);
        return;
    }

    if (m_state.plugins.empty())
    {
        auto placeholder_area = chain_area;
        placeholder_area.removeFromTop(
            std::min(g_insert_slot_height + g_plugin_row_gap, placeholder_area.getHeight()));
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(
            "No plugins loaded", placeholder_area, juce::Justification::centredLeft, 1);
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
    m_chain_viewport.setBounds(area);
    const int content_width = std::max(0, m_chain_viewport.getMaximumVisibleWidth());
    const int content_height = std::max(area.getHeight(), chainContentHeight(m_plugin_rows.size()));
    m_chain_content.setSize(content_width, content_height);
    auto content_area = m_chain_content.getLocalBounds();

    for (std::size_t index = 0; index < m_insert_slots.size(); ++index)
    {
        const std::unique_ptr<InsertSlotView>& slot = m_insert_slots[index];
        if (slot == nullptr)
        {
            continue;
        }

        slot->setVisible(true);
        slot->setBounds(content_area.removeFromTop(g_insert_slot_height));
        content_area.removeFromTop(std::min(g_plugin_row_gap, content_area.getHeight()));

        if (index >= m_plugin_rows.size())
        {
            continue;
        }

        const std::unique_ptr<PluginRowView>& row = m_plugin_rows[index];
        if (row == nullptr)
        {
            continue;
        }

        row->setVisible(true);
        row->setBounds(content_area.removeFromTop(g_plugin_row_height));
        content_area.removeFromTop(std::min(g_plugin_row_gap, content_area.getHeight()));
    }
}

// Converts a row drop on an insertion slot into the existing move-plugin intent.
void SignalChainPanel::movePluginToInsertionSlot(
    std::string instance_id, std::size_t source_index, std::size_t slot_index)
{
    if (!m_state.move_plugins_enabled)
    {
        return;
    }

    const std::optional<std::size_t> destination_index =
        destinationIndexForDrop(source_index, slot_index, m_state.plugins.size());
    if (!destination_index.has_value())
    {
        return;
    }

    m_listener.onMovePluginPressed(std::move(instance_id), *destination_index);
}

// Recreates child rows from the latest controller state so each button carries a stable ID.
void SignalChainPanel::rebuildPluginRows()
{
    for (const std::unique_ptr<InsertSlotView>& slot : m_insert_slots)
    {
        if (slot != nullptr)
        {
            m_chain_content.removeChildComponent(slot.get());
        }
    }

    for (const std::unique_ptr<PluginRowView>& row : m_plugin_rows)
    {
        if (row != nullptr)
        {
            m_chain_content.removeChildComponent(row.get());
        }
    }

    m_insert_slots.clear();
    m_plugin_rows.clear();
    if (!m_state.disabled_message.empty())
    {
        return;
    }

    m_insert_slots.reserve(m_state.plugins.size() + 1);
    for (std::size_t index = 0; index <= m_state.plugins.size(); ++index)
    {
        auto slot = std::make_unique<InsertSlotView>(index, *this, m_listener);
        slot->setEditingEnabled(m_state.insert_plugin_enabled, m_state.move_plugins_enabled);
        m_chain_content.addAndMakeVisible(*slot);
        m_insert_slots.push_back(std::move(slot));
    }

    m_plugin_rows.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto row = std::make_unique<PluginRowView>(plugin, m_state.plugins.size(), m_listener);
        row->setEditEnabled(m_state.move_plugins_enabled, m_state.remove_plugins_enabled);
        m_chain_content.addAndMakeVisible(*row);
        m_plugin_rows.push_back(std::move(row));
    }
}

} // namespace rock_hero::editor::ui
