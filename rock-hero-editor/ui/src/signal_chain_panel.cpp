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
constexpr int g_header_height{34};
constexpr int g_insert_rail_width{16};
constexpr int g_plugin_tile_width{128};
constexpr int g_chain_gap{3};
constexpr int g_tile_remove_button_size{18};
constexpr int g_tile_inset{6};
// Idle opacity for the hover-revealed insert "+" and tile remove "x" affordances. They stay present
// (and hit-testable) for discoverability and testing, but fade until the pointer enters their host.
constexpr float g_idle_affordance_alpha{0.0f};
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

// Computes the full scrollable content width for a left-to-right rail/tile strip.
[[nodiscard]] int chainContentWidth(std::size_t plugin_count) noexcept
{
    const auto rail_count = static_cast<int>(plugin_count + 1);
    const auto tile_count = static_cast<int>(plugin_count);
    return (rail_count * g_insert_rail_width) +
           (tile_count * (g_plugin_tile_width + (g_chain_gap * 2)));
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
        // The rail is mostly empty space; the "+" stays dim until the pointer enters the rail (or
        // the button itself), so the gap reads as a discoverable insertion affordance on hover.
        m_button.setAlpha(g_idle_affordance_alpha);
        m_button.addMouseListener(this, false);
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

    // Draws the slot as a thin vertical insertion rail, with stronger feedback while dragging.
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        if (m_is_drag_hovered)
        {
            g.setColour(g_insert_slot_drop_fill);
            g.fillRect(area);
        }

        const int x = area.getCentreX();
        g.setColour(m_is_drag_hovered ? g_insert_slot_drop_line : g_insert_slot_line);
        g.drawLine(
            static_cast<float>(x),
            0.0f,
            static_cast<float>(x),
            static_cast<float>(area.getHeight()),
            m_is_drag_hovered ? 2.0f : 1.0f);
    }

    // Keeps the compact insertion button centered near the top of the vertical rail.
    void resized() override
    {
        const int button_size = std::min(g_insert_rail_width, getWidth());
        m_button.setBounds(
            getLocalBounds()
                .removeFromTop(g_insert_rail_width)
                .withSizeKeepingCentre(button_size, button_size));
    }

    // Brightens the "+" affordance while the pointer is over the rail or its button.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        updateButtonAffordance();
    }

    // Restores the dim "+" affordance once the pointer leaves the rail and its button.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        updateButtonAffordance();
    }

private:
    // Recomputes the "+" opacity from whether the pointer is over the rail or its child button.
    // isMouseOver(true) includes descendants, so it stays bright while the pointer sits on the
    // button itself even though that hides the rail's own hover.
    void updateButtonAffordance()
    {
        m_button.setAlpha(isMouseOver(true) ? 1.0f : g_idle_affordance_alpha);
    }

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

// Presents one fixed-width plugin tile in the horizontal chain strip and emits edit intents for
// its stored instance ID. SettableTooltipClient carries the full plugin label, which the fixed
// tile width would otherwise truncate.
class SignalChainPanel::PluginTileView final : public juce::Component,
                                               public juce::SettableTooltipClient
{
public:
    // Creates the tile with a stable plugin snapshot and the parent panel listener.
    PluginTileView(core::PluginViewState plugin, std::size_t plugin_count, Listener& listener)
        : m_listener(listener)
        , m_plugin(std::move(plugin))
        , m_plugin_count(plugin_count)
    {
        setComponentID(juce::String{"plugin_tile_"} + juce::String{m_plugin.instance_id});
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        // The fixed tile width truncates the on-tile name, so surface the full label on hover.
        setTooltip(pluginLabel(m_plugin));

        m_remove_button.setComponentID(
            juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
        // Keep the remove affordance compact in the tile corner.
        m_remove_button.setButtonText("x");
        m_remove_button.setTooltip("Remove plugin");
        m_remove_button.onClick = [this] {
            m_listener.onRemovePluginPressed(m_plugin.instance_id);
        };
        // The "x" stays dim until the tile is hovered, so a resting tile reads as one clean target.
        m_remove_button.setAlpha(g_idle_affordance_alpha);
        addAndMakeVisible(m_remove_button);
    }

    // Applies controller-derived edit availability. The move gate now governs drag-to-reorder
    // rather than discrete buttons, so it no longer toggles any child control.
    void setEditEnabled(bool move_enabled, bool remove_enabled)
    {
        m_move_enabled = move_enabled;
        m_remove_button.setEnabled(remove_enabled);
    }

    // Draws the tile background, hover accent, order badge, wrapped name, and dimmed maker/format.
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        g.setColour(m_is_hovered ? g_plugin_row_hover_background : g_plugin_row_background);
        g.fillRect(bounds);
        if (m_is_hovered)
        {
            g.setColour(g_plugin_row_hover_accent);
            g.fillRect(bounds.withWidth(4));
        }
        g.setColour(m_is_hovered ? g_plugin_row_hover_border : g_plugin_row_border);
        g.drawRect(bounds, m_is_hovered ? 2 : 1);

        auto content = bounds.reduced(g_tile_inset);

        // Top: order-number badge on the left; the remove "x" child occupies the right corner.
        auto badge_area = content.removeFromTop(g_tile_remove_button_size);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{13.0f, juce::Font::bold});
        g.drawText(
            juce::String{std::to_string(m_plugin.chain_index + 1)},
            badge_area.withTrimmedRight(g_tile_remove_button_size),
            juce::Justification::centredLeft);

        // Bottom: dimmed manufacturer/format line; what remains in the middle holds the name.
        auto maker_area = content.removeFromBottom(g_tile_inset * 3);
        const juce::String name =
            m_plugin.name.empty() ? juce::String{"Unnamed Plugin"} : juce::String{m_plugin.name};
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(name, content, juce::Justification::topLeft, 2);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{11.0f});
        g.drawFittedText(makerLabel(m_plugin), maker_area, juce::Justification::bottomLeft, 1);
    }

    // Pins the remove button to the tile's top-right corner.
    void resized() override
    {
        m_remove_button.setBounds(
            getLocalBounds()
                .reduced(g_tile_inset)
                .removeFromTop(g_tile_remove_button_size)
                .removeFromRight(g_tile_remove_button_size));
    }

    // Resets drag-start state at the beginning of each pointer sequence.
    void mouseDown(const juce::MouseEvent& /*event*/) override
    {
        m_drag_started = false;
    }

    // Starts a JUCE drag operation for reorderable plugin tiles.
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

    // Treats a tile click as an editor-window request while ignoring drag releases.
    void mouseUp(const juce::MouseEvent& event) override
    {
        m_drag_started = false;
        if (event.mouseWasClicked())
        {
            m_listener.onOpenPluginPressed(m_plugin.instance_id);
        }
    }

    // Highlights the tile and reveals its remove "x" while the pointer is over it.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = true;
        m_remove_button.setAlpha(1.0f);
        repaint();
    }

    // Clears the tile affordances when the pointer leaves the plugin tile.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = false;
        m_remove_button.setAlpha(g_idle_affordance_alpha);
        repaint();
    }

private:
    // Builds the dimmed bottom line carrying manufacturer and format when present.
    [[nodiscard]] static juce::String makerLabel(const core::PluginViewState& plugin)
    {
        juce::String label;
        if (!plugin.manufacturer.empty())
        {
            label += juce::String{plugin.manufacturer};
        }
        if (!plugin.format_name.empty())
        {
            if (label.isNotEmpty())
            {
                label += " ";
            }
            label += "(";
            label += juce::String{plugin.format_name};
            label += ")";
        }
        return label;
    }

    // Listener that receives this tile's remove, open, and move intents.
    Listener& m_listener;

    // Stable plugin snapshot represented by this tile.
    core::PluginViewState m_plugin;

    // Total user-visible plugin count used to gate single-plugin drag reordering.
    std::size_t m_plugin_count{};

    // Button that emits a remove intent for this tile's plugin instance.
    juce::TextButton m_remove_button;

    // True while the pointer is over the tile, driving the hover accent and remove reveal.
    bool m_is_hovered{false};

    // True when the tile can initiate drag-based reordering.
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

// Creates the panel controls and routes user intents through the owner.
SignalChainPanel::SignalChainPanel(Listener& listener)
    : m_listener(listener)
    , m_input_meter(AudioLevelMeterOrientation::Vertical)
    , m_output_gain_slider_look_and_feel(std::make_unique<OutputGainSliderLookAndFeel>())
    , m_output_meter(AudioLevelMeterOrientation::Vertical)
{
    setComponentID("signal_chain_panel");

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
    m_chain_viewport.setScrollBarsShown(false, true);
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
    m_input_calibrate_button.setEnabled(m_state.input_calibrate_enabled);
    m_output_gain_slider.setEnabled(m_state.output_gain_controls_enabled);
    m_output_gain_slider.setValue(m_state.output_gain_db, juce::dontSendNotification);
    m_chain_viewport.setVisible(m_state.disabled_message.empty());
    rebuildPluginTiles();
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
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText("No plugins loaded", chain_area, juce::Justification::centred, 1);
        return;
    }
}

// Keeps gain sliders on the sides and plugin rows in the center.
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

    area.removeFromTop(g_header_height);
    area.removeFromTop(g_panel_inset);
    m_chain_viewport.setBounds(area);
    const int content_height = std::max(0, m_chain_viewport.getMaximumVisibleHeight());
    const int content_width = std::max(area.getWidth(), chainContentWidth(m_plugin_tiles.size()));
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
        slot->setBounds(content_area.removeFromLeft(g_insert_rail_width));
        content_area.removeFromLeft(std::min(g_chain_gap, content_area.getWidth()));

        if (index >= m_plugin_tiles.size())
        {
            continue;
        }

        const std::unique_ptr<PluginTileView>& tile = m_plugin_tiles[index];
        if (tile == nullptr)
        {
            continue;
        }

        tile->setVisible(true);
        tile->setBounds(content_area.removeFromLeft(g_plugin_tile_width));
        content_area.removeFromLeft(std::min(g_chain_gap, content_area.getWidth()));
    }
}

// Converts a tile drop on an insertion slot into the existing move-plugin intent.
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

// Recreates child tiles from the latest controller state so each control carries a stable ID.
void SignalChainPanel::rebuildPluginTiles()
{
    for (const std::unique_ptr<InsertSlotView>& slot : m_insert_slots)
    {
        if (slot != nullptr)
        {
            m_chain_content.removeChildComponent(slot.get());
        }
    }

    for (const std::unique_ptr<PluginTileView>& tile : m_plugin_tiles)
    {
        if (tile != nullptr)
        {
            m_chain_content.removeChildComponent(tile.get());
        }
    }

    m_insert_slots.clear();
    m_plugin_tiles.clear();
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

    m_plugin_tiles.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto tile = std::make_unique<PluginTileView>(plugin, m_state.plugins.size(), m_listener);
        tile->setEditEnabled(m_state.move_plugins_enabled, m_state.remove_plugins_enabled);
        m_chain_content.addAndMakeVisible(*tile);
        m_plugin_tiles.push_back(std::move(tile));
    }
}

} // namespace rock_hero::editor::ui
