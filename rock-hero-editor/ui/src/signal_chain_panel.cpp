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
constexpr int g_insert_rail_width{28};
constexpr int g_signal_path_padding{20};
constexpr int g_signal_path_min_cell_width{128};
constexpr int g_signal_plugin_view_width{118};
constexpr int g_signal_block_width{70};
constexpr int g_signal_block_height{64};
constexpr int g_signal_block_icon_size{44};
constexpr int g_signal_block_label_gap{6};
constexpr int g_signal_block_name_height{18};
constexpr int g_signal_block_maker_height{15};
constexpr int g_signal_plugin_view_height{
    g_signal_block_height + g_signal_block_label_gap + g_signal_block_name_height +
    g_signal_block_maker_height
};
constexpr int g_tile_remove_button_size{20};
constexpr int g_tile_remove_button_inset{3};
constexpr int g_tile_inset{6};
constexpr std::size_t g_signal_path_block_capacity{8};
// Insert rails stay faintly visible for discoverability; tile removal stays invisible at rest so
// plugin blocks read as icon-only until hovered.
constexpr float g_idle_insert_affordance_alpha{0.12f};
constexpr float g_idle_remove_affordance_alpha{0.0f};
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
const juce::Colour g_path_background{juce::Colour{0xff101318}};
const juce::Colour g_signal_path_line{juce::Colours::white.withAlpha(0.82f)};
const juce::Colour g_signal_path_slot_marker{juce::Colours::white.withAlpha(0.12f)};
const juce::Colour g_plugin_tile_background{juce::Colour{0xff1b2027}};
const juce::Colour g_plugin_tile_hover_background{juce::Colour{0xff222b35}};
const juce::Colour g_plugin_tile_border{juce::Colours::black.withAlpha(0.6f)};
const juce::Colour g_plugin_tile_hover_border{juce::Colours::white.withAlpha(0.7f)};
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

enum class PluginIconType
{
    Generic,
    Amp,
    Cab,
    Drive,
    Delay,
    Reverb,
    Modulation,
    Dynamics,
    Eq,
    Gate,
    Pitch,
    Wah,
};

// Returns the number of fixed block positions the path surface should reserve.
[[nodiscard]] std::size_t visualBlockCount(std::size_t plugin_count) noexcept
{
    return std::max(g_signal_path_block_capacity, plugin_count);
}

// Computes the full scrollable content width for the fixed-position signal path.
[[nodiscard]] int chainContentWidth(std::size_t plugin_count, int viewport_width) noexcept
{
    const int natural_width =
        (static_cast<int>(visualBlockCount(plugin_count)) * g_signal_path_min_cell_width) +
        (g_signal_path_padding * 2);
    return std::max(viewport_width, natural_width);
}

// Returns the inner path bounds shared by painting and child layout.
[[nodiscard]] juce::Rectangle<int> signalPathArea(juce::Rectangle<int> bounds)
{
    return bounds.reduced(g_signal_path_padding, 0);
}

// Computes one fixed visual block cell by index.
[[nodiscard]] juce::Rectangle<int> blockCellBounds(
    juce::Rectangle<int> path_area, std::size_t cell_index, std::size_t block_count)
{
    const int cell_width = std::max(1, path_area.getWidth() / static_cast<int>(block_count));
    return path_area.withX(path_area.getX() + (static_cast<int>(cell_index) * cell_width))
        .withWidth(cell_width);
}

// Places one plugin block-plus-label view inside a fixed signal-path cell.
[[nodiscard]] juce::Rectangle<int> pluginBlockBounds(
    juce::Rectangle<int> path_area, std::size_t block_index, std::size_t block_count)
{
    const juce::Rectangle<int> cell = blockCellBounds(path_area, block_index, block_count);
    const int view_width = std::min(g_signal_plugin_view_width, std::max(1, cell.getWidth() - 10));
    const int view_height =
        std::min(g_signal_plugin_view_height, std::max(1, path_area.getHeight() - 6));
    auto bounds = cell.withSizeKeepingCentre(view_width, view_height);

    // The caption extends below the block, so align the block itself to the path center instead
    // of centering the whole component.
    const int ideal_top = path_area.getCentreY() - (g_signal_block_height / 2);
    const int min_top = path_area.getY();
    const int max_top = std::max(min_top, path_area.getBottom() - view_height);
    bounds.setY(std::clamp(ideal_top, min_top, max_top));
    return bounds;
}

// Returns the compact block rectangle inside a wider tile view that also carries external labels.
[[nodiscard]] juce::Rectangle<int> pluginTileArea(juce::Rectangle<int> bounds)
{
    const int tile_width = std::min(g_signal_block_width, std::max(1, bounds.getWidth()));
    const int tile_height = std::min(g_signal_block_height, std::max(1, bounds.getHeight()));
    return juce::Rectangle<int>{
        bounds.getX() + ((bounds.getWidth() - tile_width) / 2),
        bounds.getY(),
        tile_width,
        tile_height,
    };
}

// Places insert slots at path boundaries, with the append slot occupying the next empty block.
[[nodiscard]] int insertionSlotCenterX(
    std::size_t slot_index, std::size_t plugin_count, juce::Rectangle<int> path_area,
    std::size_t block_count)
{
    if (block_count == 0)
    {
        return path_area.getCentreX();
    }

    if (plugin_count == 0 || (slot_index == plugin_count && slot_index < block_count))
    {
        return blockCellBounds(path_area, slot_index, block_count).getCentreX();
    }

    if (slot_index == 0)
    {
        return path_area.getX() + (g_insert_rail_width / 2);
    }

    if (slot_index >= block_count)
    {
        return path_area.getRight() - (g_insert_rail_width / 2);
    }

    const juce::Rectangle<int> cell = blockCellBounds(path_area, slot_index, block_count);
    return cell.getX();
}

// Builds a lower-case metadata string used only for visual plugin-type hints.
[[nodiscard]] juce::String pluginSearchText(const core::PluginViewState& plugin)
{
    juce::String text{plugin.name};
    text += " ";
    text += juce::String{plugin.manufacturer};
    text += " ";
    text += juce::String{plugin.format_name};
    return text.toLowerCase();
}

// Infers a display icon from common guitar-plugin naming conventions.
[[nodiscard]] PluginIconType inferPluginIconType(const core::PluginViewState& plugin)
{
    const juce::String text = pluginSearchText(plugin);
    if (text.contains("cab") || text.contains("impulse") || text.contains("loader"))
    {
        return PluginIconType::Cab;
    }
    if (text.contains("amp") || text.contains("amplifier") || text.contains("neural"))
    {
        return PluginIconType::Amp;
    }
    if (text.contains("drive") || text.contains("dist") || text.contains("fuzz") ||
        text.contains("boost") || text.contains("overdrive"))
    {
        return PluginIconType::Drive;
    }
    if (text.contains("delay") || text.contains("echo"))
    {
        return PluginIconType::Delay;
    }
    if (text.contains("reverb") || text.contains("room") || text.contains("hall") ||
        text.contains("plate"))
    {
        return PluginIconType::Reverb;
    }
    if (text.contains("chorus") || text.contains("flanger") || text.contains("phaser") ||
        text.contains("tremolo") || text.contains("vibrato"))
    {
        return PluginIconType::Modulation;
    }
    if (text.contains("comp") || text.contains("limiter"))
    {
        return PluginIconType::Dynamics;
    }
    if (text.contains("eq") || text.contains("equalizer"))
    {
        return PluginIconType::Eq;
    }
    if (text.contains("gate") || text.contains("noise"))
    {
        return PluginIconType::Gate;
    }
    if (text.contains("pitch") || text.contains("octave") || text.contains("harmon"))
    {
        return PluginIconType::Pitch;
    }
    if (text.contains("wah") || text.contains("filter"))
    {
        return PluginIconType::Wah;
    }
    return PluginIconType::Generic;
}

// Assigns a restrained accent so unknown plugins still fit the path while known types differ.
[[nodiscard]] juce::Colour iconAccentColour(PluginIconType icon_type)
{
    switch (icon_type)
    {
        case PluginIconType::Amp:
            return juce::Colour{0xfff1c15b};
        case PluginIconType::Cab:
            return juce::Colour{0xff7ed6a5};
        case PluginIconType::Drive:
            return juce::Colour{0xfff07f5f};
        case PluginIconType::Delay:
            return juce::Colour{0xff7da8ff};
        case PluginIconType::Reverb:
            return juce::Colour{0xffb78cff};
        case PluginIconType::Modulation:
            return juce::Colour{0xff64d7d0};
        case PluginIconType::Dynamics:
            return juce::Colour{0xffffd66b};
        case PluginIconType::Eq:
            return juce::Colour{0xff8fd37f};
        case PluginIconType::Gate:
            return juce::Colour{0xffff8ea1};
        case PluginIconType::Pitch:
            return juce::Colour{0xff8fb8ff};
        case PluginIconType::Wah:
            return juce::Colour{0xffe8a66a};
        case PluginIconType::Generic:
            return juce::Colour{0xffd7dde6};
    }

    return juce::Colour{0xffd7dde6};
}

// Encodes enough tile state for slot drop targets to compute final move destinations.
[[nodiscard]] juce::String makePluginDragDescription(const core::PluginViewState& plugin)
{
    juce::String description{g_plugin_drag_prefix};
    description += juce::String{std::to_string(plugin.chain_index)};
    description += ":";
    description += juce::String{plugin.instance_id};
    return description;
}

// Decodes a plugin-tile drag payload while rejecting unrelated JUCE drag operations.
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

// Translates an insertion slot into the final destination index after removing the source tile.
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

// Builds the plugin name shown below the block.
[[nodiscard]] juce::String pluginDisplayName(const core::PluginViewState& plugin)
{
    return plugin.name.empty() ? juce::String{"Unnamed Plugin"} : juce::String{plugin.name};
}

// Builds the second label line from the separate plugin manufacturer metadata.
[[nodiscard]] juce::String pluginDisplayMaker(const core::PluginViewState& plugin)
{
    if (!plugin.manufacturer.empty())
    {
        return juce::String{plugin.manufacturer};
    }

    return plugin.format_name.empty() ? juce::String{} : juce::String{plugin.format_name};
}

// Draws a compact category hint inside a tile without requiring plugin-host-specific artwork.
void drawPluginIcon(
    juce::Graphics& g, juce::Rectangle<int> icon_area, PluginIconType icon_type,
    juce::Colour accent)
{
    const auto icon = icon_area.toFloat();
    g.setColour(accent.withAlpha(0.18f));
    g.fillRoundedRectangle(icon, 5.0f);
    g.setColour(accent.withAlpha(0.95f));

    const auto symbol = icon.reduced(5.0f);
    switch (icon_type)
    {
        case PluginIconType::Amp:
        {
            g.drawRoundedRectangle(symbol, 3.0f, 1.6f);
            for (int index = 0; index < 3; ++index)
            {
                const float x = symbol.getX() + 5.0f + (static_cast<float>(index) * 6.0f);
                g.fillEllipse(x, symbol.getY() + 4.0f, 3.0f, 3.0f);
            }
            g.drawLine(
                symbol.getX() + 4.0f,
                symbol.getBottom() - 5.0f,
                symbol.getRight() - 4.0f,
                symbol.getBottom() - 5.0f,
                1.4f);
            break;
        }
        case PluginIconType::Cab:
        {
            const auto speaker = symbol.reduced(2.0f);
            g.drawEllipse(speaker, 1.8f);
            g.fillEllipse(speaker.withSizeKeepingCentre(5.0f, 5.0f));
            break;
        }
        case PluginIconType::Drive:
        {
            const auto pedal = symbol.reduced(2.0f, 0.0f);
            g.drawRoundedRectangle(pedal, 2.5f, 1.6f);
            g.fillEllipse(pedal.getCentreX() - 2.5f, pedal.getY() + 4.0f, 5.0f, 5.0f);
            g.drawLine(
                pedal.getX() + 4.0f,
                pedal.getBottom() - 5.0f,
                pedal.getRight() - 4.0f,
                pedal.getBottom() - 5.0f,
                1.4f);
            break;
        }
        case PluginIconType::Delay:
        {
            for (int index = 0; index < 3; ++index)
            {
                const float radius = 10.0f - (static_cast<float>(index) * 3.0f);
                g.drawEllipse(symbol.withSizeKeepingCentre(radius, radius), 1.3f);
            }
            break;
        }
        case PluginIconType::Reverb:
        {
            juce::Path cloud;
            cloud.addEllipse(symbol.getX(), symbol.getY() + 6.0f, 9.0f, 9.0f);
            cloud.addEllipse(symbol.getX() + 6.0f, symbol.getY() + 2.0f, 10.0f, 10.0f);
            cloud.addEllipse(symbol.getX() + 13.0f, symbol.getY() + 7.0f, 8.0f, 8.0f);
            g.strokePath(cloud, juce::PathStrokeType{1.6f});
            break;
        }
        case PluginIconType::Modulation:
        {
            juce::Path wave;
            wave.startNewSubPath(symbol.getX(), symbol.getCentreY());
            wave.cubicTo(
                symbol.getX() + 5.0f,
                symbol.getY(),
                symbol.getX() + 10.0f,
                symbol.getBottom(),
                symbol.getX() + 15.0f,
                symbol.getCentreY());
            wave.cubicTo(
                symbol.getX() + 18.0f,
                symbol.getY(),
                symbol.getRight() - 2.0f,
                symbol.getBottom(),
                symbol.getRight(),
                symbol.getCentreY());
            g.strokePath(wave, juce::PathStrokeType{1.8f});
            break;
        }
        case PluginIconType::Dynamics:
        {
            for (int index = 0; index < 4; ++index)
            {
                const float height = 6.0f + (static_cast<float>(index % 2) * 8.0f);
                const float x = symbol.getX() + 3.0f + (static_cast<float>(index) * 5.0f);
                g.fillRect(juce::Rectangle<float>{x, symbol.getBottom() - height, 3.0f, height});
            }
            break;
        }
        case PluginIconType::Eq:
        {
            for (int index = 0; index < 3; ++index)
            {
                const float x = symbol.getX() + 4.0f + (static_cast<float>(index) * 7.0f);
                g.drawLine(x, symbol.getY(), x, symbol.getBottom(), 1.1f);
                const float y = symbol.getY() + 4.0f + (static_cast<float>(index) * 4.0f);
                g.fillRoundedRectangle(x - 3.0f, y, 6.0f, 3.0f, 1.5f);
            }
            break;
        }
        case PluginIconType::Gate:
        {
            const float left = symbol.getX();
            const float top = symbol.getY();
            const float centre_x = symbol.getCentreX();
            const float bottom = symbol.getBottom();
            g.drawLine(left, bottom, centre_x, top, 1.6f);
            g.drawLine(centre_x, top, symbol.getRight(), bottom, 1.6f);
            break;
        }
        case PluginIconType::Pitch:
        {
            juce::Path arrow;
            arrow.startNewSubPath(symbol.getX() + 3.0f, symbol.getBottom() - 3.0f);
            arrow.lineTo(symbol.getCentreX(), symbol.getY() + 2.0f);
            arrow.lineTo(symbol.getRight() - 3.0f, symbol.getBottom() - 3.0f);
            g.strokePath(arrow, juce::PathStrokeType{1.8f});
            break;
        }
        case PluginIconType::Wah:
        {
            juce::Path pedal;
            pedal.startNewSubPath(symbol.getX() + 3.0f, symbol.getBottom());
            pedal.lineTo(symbol.getX() + 8.0f, symbol.getY());
            pedal.lineTo(symbol.getRight() - 3.0f, symbol.getY() + 4.0f);
            pedal.lineTo(symbol.getRight() - 2.0f, symbol.getBottom());
            pedal.closeSubPath();
            g.strokePath(pedal, juce::PathStrokeType{1.5f});
            break;
        }
        case PluginIconType::Generic:
        {
            juce::Path wave;
            wave.startNewSubPath(symbol.getX(), symbol.getCentreY());
            wave.lineTo(symbol.getX() + 5.0f, symbol.getCentreY());
            wave.lineTo(symbol.getX() + 8.0f, symbol.getY() + 3.0f);
            wave.lineTo(symbol.getX() + 12.0f, symbol.getBottom() - 3.0f);
            wave.lineTo(symbol.getX() + 15.0f, symbol.getCentreY());
            wave.lineTo(symbol.getRight(), symbol.getCentreY());
            g.strokePath(wave, juce::PathStrokeType{1.7f});
            break;
        }
    }
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

// Paints the Quad Cortex-style signal rail behind insertion slots and plugin tiles.
class SignalChainPanel::SignalPathContent final : public juce::Component
{
public:
    // Stores the current plugin count so empty block positions remain visible.
    void setPluginCount(std::size_t plugin_count)
    {
        if (m_plugin_count == plugin_count)
        {
            return;
        }

        m_plugin_count = plugin_count;
        repaint();
    }

    // Draws the dark path surface, white signal line, and subtle fixed-position markers.
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        g.fillAll(g_path_background);

        const auto path_area = signalPathArea(bounds);
        const std::size_t block_count = visualBlockCount(m_plugin_count);
        if (path_area.isEmpty() || block_count == 0)
        {
            return;
        }

        const int path_y = path_area.getCentreY();
        g.setColour(g_signal_path_line);
        g.drawLine(
            static_cast<float>(path_area.getX()),
            static_cast<float>(path_y),
            static_cast<float>(path_area.getRight()),
            static_cast<float>(path_y),
            3.0f);

        g.setColour(g_signal_path_slot_marker);
        for (std::size_t index = 0; index < block_count; ++index)
        {
            const auto marker_bounds =
                blockCellBounds(path_area, index, block_count).withSizeKeepingCentre(8, 8);
            g.fillEllipse(marker_bounds.toFloat());
        }
    }

private:
    // Number of plugins currently projected into the path renderer.
    std::size_t m_plugin_count{};
};

// Presents one insertion slot on the path and accepts plugin-tile drops.
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
        m_button.onClick = [this] { m_listener.onInsertPluginPressed(m_chain_index); };
        // The rail is mostly empty space; the "+" stays dim until the pointer enters the rail (or
        // the button itself), so the gap reads as a discoverable insertion affordance on hover.
        m_button.setAlpha(g_idle_insert_affordance_alpha);
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

    // Reports whether dropping the current payload here would actually relocate the tile. Slots
    // adjacent to the dragged tile resolve to a no-op, so they must decline interest rather than
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

    // Draws the slot as a short path crossing, with stronger feedback while dragging.
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        if (m_is_drag_hovered)
        {
            g.setColour(g_insert_slot_drop_fill);
            g.fillRoundedRectangle(area.reduced(1).toFloat(), 6.0f);
        }

        const int x = area.getCentreX();
        const int y = area.getCentreY();
        const int half_height = std::min(28, std::max(0, area.getHeight() / 2));
        g.setColour(m_is_drag_hovered ? g_insert_slot_drop_line : g_insert_slot_line);
        g.drawLine(
            static_cast<float>(x),
            static_cast<float>(y - half_height),
            static_cast<float>(x),
            static_cast<float>(y + half_height),
            m_is_drag_hovered ? 2.0f : 1.0f);
    }

    // Keeps the compact insertion button centered on the signal path.
    void resized() override
    {
        const int button_size = std::min(g_insert_rail_width, getWidth());
        m_button.setBounds(getLocalBounds().withSizeKeepingCentre(button_size, button_size));
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
        m_button.setAlpha(isMouseOver(true) ? 1.0f : g_idle_insert_affordance_alpha);
    }

    // Owning panel used to translate drops into move intents.
    SignalChainPanel& m_panel;

    // Listener that receives this slot's insert intent.
    Listener& m_listener;

    // Stable user-visible chain slot represented by this control.
    std::size_t m_chain_index{};

    // Compact insertion command button.
    juce::TextButton m_button;

    // True when plugin tiles may be dropped on this insertion slot.
    bool m_drop_enabled{false};

    // True while a compatible tile drag is hovering over this slot.
    bool m_is_drag_hovered{false};
};

// Presents one compact plugin block in the horizontal chain strip and emits edit intents for its
// stored instance ID. The name and manufacturer sit below the block as a caption.
class SignalChainPanel::PluginTileView final : public juce::Component
{
public:
    // Creates the tile with a stable plugin snapshot and the parent panel listener.
    PluginTileView(core::PluginViewState plugin, std::size_t plugin_count, Listener& listener)
        : m_listener(listener)
        , m_plugin(std::move(plugin))
        , m_icon_type(inferPluginIconType(m_plugin))
        , m_accent(iconAccentColour(m_icon_type))
        , m_plugin_count(plugin_count)
    {
        setComponentID(juce::String{"plugin_tile_"} + juce::String{m_plugin.instance_id});
        setMouseCursor(juce::MouseCursor::PointingHandCursor);

        m_remove_button.setComponentID(
            juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
        // Keep the remove affordance compact in the tile corner.
        m_remove_button.setButtonText("X");
        m_remove_button.onClick = [this] {
            m_listener.onRemovePluginPressed(m_plugin.instance_id);
        };
        // Child-button hover can hide the tile's own hover, so keep the shared affordance in sync
        // with descendant pointer state as well as direct tile pointer state.
        m_remove_button.onStateChange = [this] { updateHoverAffordance(); };
        // The "X" stays hidden until hover, so a resting tile reads as one clean icon target.
        m_remove_button.setAlpha(g_idle_remove_affordance_alpha);
        addAndMakeVisible(m_remove_button);
    }

    // Applies controller-derived edit availability. The move gate now governs drag-to-reorder
    // rather than discrete buttons, so it no longer toggles any child control.
    void setEditEnabled(bool move_enabled, bool remove_enabled)
    {
        m_move_enabled = move_enabled;
        m_remove_button.setEnabled(remove_enabled);
    }

    // Draws the icon-only block, primary name, and secondary maker line for quick chain scanning.
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        const auto tile_area = pluginTileArea(bounds);
        auto label_area = bounds.withTrimmedTop(
            (tile_area.getBottom() - bounds.getY()) + g_signal_block_label_gap);
        auto name_area = label_area.removeFromTop(g_signal_block_name_height);
        auto maker_area = label_area.removeFromTop(g_signal_block_maker_height);

        const auto tile_bounds = tile_area.toFloat().reduced(1.0f);
        g.setColour(m_is_hovered ? g_plugin_tile_hover_background : g_plugin_tile_background);
        g.fillRoundedRectangle(tile_bounds, 8.0f);
        g.setColour(m_accent.withAlpha(m_is_hovered ? 0.95f : 0.62f));
        g.fillRoundedRectangle(tile_bounds.withHeight(4.0f), 4.0f);
        g.setColour(m_is_hovered ? g_plugin_tile_hover_border : g_plugin_tile_border);
        g.drawRoundedRectangle(tile_bounds, 8.0f, m_is_hovered ? 1.8f : 1.1f);

        const auto content = tile_area.reduced(g_tile_inset);
        drawPluginIcon(
            g,
            content.withSizeKeepingCentre(g_signal_block_icon_size, g_signal_block_icon_size),
            m_icon_type,
            m_accent);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{13.0f, juce::Font::bold});
        g.drawText(pluginDisplayName(m_plugin), name_area, juce::Justification::centred, true);

        const juce::String maker = pluginDisplayMaker(m_plugin);
        if (maker.isNotEmpty())
        {
            g.setColour(juce::Colours::lightgrey.withAlpha(0.68f));
            g.setFont(juce::FontOptions{11.0f});
            g.drawText(maker, maker_area, juce::Justification::centred, true);
        }
    }

    // Pins the remove button to the tile's top-right corner.
    void resized() override
    {
        m_remove_button.setBounds(pluginTileArea(getLocalBounds())
                                      .reduced(g_tile_remove_button_inset)
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

    // Highlights the tile and reveals its remove "X" while the pointer is over it.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        updateHoverAffordance();
    }

    // Clears the tile affordances when the pointer leaves the plugin tile.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        updateHoverAffordance();
    }

private:
    // Keeps the block highlight and remove affordance alive while either the tile or the child
    // remove button has the pointer.
    void updateHoverAffordance()
    {
        const bool is_hovered = isMouseOver(true);
        m_is_hovered = is_hovered;
        m_remove_button.setAlpha(is_hovered ? 1.0f : g_idle_remove_affordance_alpha);
        repaint();
    }

    // Listener that receives this tile's remove, open, and move intents.
    Listener& m_listener;

    // Stable plugin snapshot represented by this tile.
    core::PluginViewState m_plugin;

    // Inferred display-only category used to draw the tile icon.
    PluginIconType m_icon_type{PluginIconType::Generic};

    // Accent color paired with the inferred display category.
    juce::Colour m_accent{};

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
    , m_chain_content(std::make_unique<SignalPathContent>())
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
    m_chain_content->setComponentID("signal_chain_content");
    m_chain_viewport.setViewedComponent(m_chain_content.get(), false);
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
    m_chain_content->setPluginCount(m_state.plugins.size());
    rebuildPluginTiles();
    resized();
    repaint();
}

// Applies the live-rig meter values without rebuilding plugin tiles or changing controls.
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

// Keeps gain sliders on the sides and plugin tiles in the center.
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
    const int content_width = chainContentWidth(m_plugin_tiles.size(), area.getWidth());
    m_chain_content->setSize(content_width, content_height);
    const auto path_area = signalPathArea(m_chain_content->getLocalBounds());
    const std::size_t block_count = visualBlockCount(m_plugin_tiles.size());

    for (std::size_t index = 0; index < m_insert_slots.size(); ++index)
    {
        const std::unique_ptr<InsertSlotView>& slot = m_insert_slots[index];
        if (slot == nullptr)
        {
            continue;
        }

        slot->setVisible(true);
        const int slot_center_x =
            insertionSlotCenterX(index, m_plugin_tiles.size(), path_area, block_count);
        auto slot_bounds = path_area.withWidth(g_insert_rail_width);
        slot_bounds.setCentre(slot_center_x, path_area.getCentreY());
        slot->setBounds(slot_bounds);

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
        tile->setBounds(pluginBlockBounds(path_area, index, block_count));
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
            m_chain_content->removeChildComponent(slot.get());
        }
    }

    for (const std::unique_ptr<PluginTileView>& tile : m_plugin_tiles)
    {
        if (tile != nullptr)
        {
            m_chain_content->removeChildComponent(tile.get());
        }
    }

    m_insert_slots.clear();
    m_plugin_tiles.clear();
    if (!m_state.disabled_message.empty())
    {
        return;
    }

    m_insert_slots.reserve(m_state.plugins.size() + 1);
    const bool has_free_block = m_state.plugins.size() < g_signal_path_block_capacity;
    for (std::size_t index = 0; index <= m_state.plugins.size(); ++index)
    {
        auto slot = std::make_unique<InsertSlotView>(index, *this, m_listener);
        slot->setEditingEnabled(
            m_state.insert_plugin_enabled && has_free_block, m_state.move_plugins_enabled);
        m_chain_content->addAndMakeVisible(*slot);
        m_insert_slots.push_back(std::move(slot));
    }

    m_plugin_tiles.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto tile = std::make_unique<PluginTileView>(plugin, m_state.plugins.size(), m_listener);
        tile->setEditEnabled(m_state.move_plugins_enabled, m_state.remove_plugins_enabled);
        m_chain_content->addAndMakeVisible(*tile);
        m_plugin_tiles.push_back(std::move(tile));
    }
}

} // namespace rock_hero::editor::ui
