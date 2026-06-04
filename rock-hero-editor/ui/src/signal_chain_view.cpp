#include "signal_chain_view.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/plugin_chain_limits.h>
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
constexpr int g_signal_preview_animation_ms{250};
constexpr double g_signal_preview_animation_start_speed{1.0};
constexpr double g_signal_preview_animation_end_speed{0.0};
constexpr int g_tile_remove_button_size{20};
constexpr int g_tile_remove_button_inset{3};
constexpr int g_tile_inset{6};
constexpr std::size_t g_signal_path_min_block_count{common::audio::max_signal_chain_plugins};
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
const juce::Colour g_insert_slot_placeholder{juce::Colours::white.withAlpha(0.16f)};
const juce::Colour g_insert_slot_drop_line{juce::Colours::lightskyblue};
constexpr const char* g_plugin_drag_prefix{"rockhero.signal-chain.plugin:"};
constexpr int g_plugin_drag_prefix_length{static_cast<int>(
    std::char_traits<char>::length(g_plugin_drag_prefix))};

struct DraggedPlugin
{
    std::string instance_id;
    std::size_t source_index{};
};

enum class BlockDropDirection
{
    PushLeft,
    PushRight,
};

struct BlockDropIntent
{
    std::size_t destination_index{};
    std::vector<std::size_t> block_indices;
};

// Splits an optional block-placement intent into the shape the view's drop finalizer consumes.
[[nodiscard]] std::pair<std::optional<std::size_t>, std::vector<std::size_t>> dropPlacementParts(
    std::optional<BlockDropIntent> intent)
{
    if (!intent.has_value())
    {
        return {std::nullopt, std::vector<std::size_t>{}};
    }

    return {
        std::optional<std::size_t>{intent->destination_index}, std::move(intent->block_indices)
    };
}

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
    return std::max(g_signal_path_min_block_count, plugin_count);
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

// Returns the direction the occupied target should move to make room for the dragged block.
[[nodiscard]] BlockDropDirection blockDropDirectionForLocalX(int local_x, int width) noexcept
{
    return local_x >= width / 2 ? BlockDropDirection::PushLeft : BlockDropDirection::PushRight;
}

// Finds the plugin currently assigned to a fixed visual block, if any.
[[nodiscard]] std::optional<std::size_t> pluginIndexAtBlock(
    const std::vector<std::size_t>& block_indices, std::size_t block_index) noexcept
{
    for (std::size_t plugin_index = 0; plugin_index < block_indices.size(); ++plugin_index)
    {
        if (block_indices[plugin_index] == block_index)
        {
            return plugin_index;
        }
    }

    return std::nullopt;
}

// Reports whether every plugin has one unique fixed block assignment inside the visual range.
[[nodiscard]] bool validBlockIndices(
    const std::vector<std::size_t>& block_indices, std::size_t plugin_count,
    std::size_t block_count)
{
    if (block_indices.size() != plugin_count)
    {
        return false;
    }

    std::vector<bool> used_blocks(block_count, false);
    for (const std::size_t block_index : block_indices)
    {
        if (block_index >= block_count || used_blocks[block_index])
        {
            return false;
        }

        used_blocks[block_index] = true;
    }

    return true;
}

// Builds the default compact visual placement for a controller-provided linear plugin order.
[[nodiscard]] std::vector<std::size_t> compactPluginBlockIndices(std::size_t plugin_count)
{
    std::vector<std::size_t> block_indices;
    block_indices.reserve(plugin_count);
    for (std::size_t index = 0; index < plugin_count; ++index)
    {
        block_indices.push_back(index);
    }

    return block_indices;
}

// Compares plugin identity order while ignoring non-identity state updates.
[[nodiscard]] bool samePluginOrder(
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<core::PluginViewState>& next_plugins)
{
    if (previous_plugins.size() != next_plugins.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < previous_plugins.size(); ++index)
    {
        if (previous_plugins[index].instance_id != next_plugins[index].instance_id)
        {
            return false;
        }
    }

    return true;
}

// Preserves visual block gaps across state refreshes that did not reorder the plugin chain.
[[nodiscard]] std::vector<std::size_t> reconciledPluginBlockIndices(
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<std::size_t>& previous_block_indices,
    const std::vector<core::PluginViewState>& next_plugins)
{
    const std::size_t block_count = visualBlockCount(next_plugins.size());
    if (samePluginOrder(previous_plugins, next_plugins) &&
        validBlockIndices(previous_block_indices, next_plugins.size(), block_count))
    {
        return previous_block_indices;
    }

    return compactPluginBlockIndices(next_plugins.size());
}

// Maps one pending insert onto the clicked fixed block while preserving existing gaps.
[[nodiscard]] std::optional<std::vector<std::size_t>> pendingInsertBlockIndicesForState(
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<std::size_t>& previous_block_indices,
    const std::vector<core::PluginViewState>& next_plugins, std::size_t insert_chain_index,
    std::size_t insert_block_index)
{
    const std::size_t previous_plugin_count = previous_plugins.size();
    const std::size_t next_plugin_count = next_plugins.size();
    const std::size_t block_count = visualBlockCount(next_plugin_count);
    if (next_plugin_count != previous_plugin_count + 1 ||
        insert_chain_index > previous_plugin_count || insert_block_index >= block_count ||
        !validBlockIndices(previous_block_indices, previous_plugin_count, block_count) ||
        pluginIndexAtBlock(previous_block_indices, insert_block_index).has_value())
    {
        return std::nullopt;
    }

    std::vector<std::size_t> next_block_indices;
    next_block_indices.reserve(next_plugin_count);
    for (std::size_t next_index = 0; next_index < next_plugin_count; ++next_index)
    {
        if (next_index == insert_chain_index)
        {
            next_block_indices.push_back(insert_block_index);
            continue;
        }

        const std::size_t previous_index =
            next_index < insert_chain_index ? next_index : next_index - 1;
        if (next_plugins[next_index].instance_id != previous_plugins[previous_index].instance_id)
        {
            return std::nullopt;
        }

        next_block_indices.push_back(previous_block_indices[previous_index]);
    }

    if (!validBlockIndices(next_block_indices, next_plugin_count, block_count))
    {
        return std::nullopt;
    }

    return next_block_indices;
}

// Maps a committed preview onto the next controller state only when the order matches the drop.
[[nodiscard]] std::optional<std::vector<std::size_t>> committedPreviewBlockIndicesForState(
    const std::vector<std::size_t>& preview_block_indices,
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<core::PluginViewState>& next_plugins)
{
    const std::size_t plugin_count = previous_plugins.size();
    if (plugin_count != next_plugins.size() ||
        !validBlockIndices(preview_block_indices, plugin_count, visualBlockCount(plugin_count)))
    {
        return std::nullopt;
    }

    std::vector<std::size_t> preview_order;
    preview_order.reserve(plugin_count);
    for (std::size_t index = 0; index < plugin_count; ++index)
    {
        preview_order.push_back(index);
    }

    std::ranges::sort(preview_order, [&preview_block_indices](std::size_t lhs, std::size_t rhs) {
        return preview_block_indices[lhs] == preview_block_indices[rhs]
                   ? lhs < rhs
                   : preview_block_indices[lhs] < preview_block_indices[rhs];
    });

    std::vector<std::size_t> next_block_indices(plugin_count);
    for (std::size_t next_index = 0; next_index < plugin_count; ++next_index)
    {
        const std::size_t previous_index = preview_order[next_index];
        if (next_plugins[next_index].instance_id != previous_plugins[previous_index].instance_id)
        {
            return std::nullopt;
        }

        next_block_indices[next_index] = preview_block_indices[previous_index];
    }

    return next_block_indices;
}

// Converts an empty visual block to the linear insertion index implied by left-to-right order.
[[nodiscard]] std::size_t chainIndexForBlockInsertion(
    const std::vector<std::size_t>& block_indices, std::size_t target_block_index) noexcept
{
    std::size_t chain_index = 0;
    for (const std::size_t block_index : block_indices)
    {
        if (block_index < target_block_index)
        {
            ++chain_index;
        }
    }

    return chain_index;
}

// Computes the moved plugin's final linear index from a proposed fixed-block placement.
[[nodiscard]] std::size_t destinationIndexForBlockIndices(
    const std::vector<std::size_t>& block_indices, std::size_t source_index) noexcept
{
    const std::size_t source_block_index = block_indices[source_index];
    std::size_t destination_index = 0;
    for (std::size_t plugin_index = 0; plugin_index < block_indices.size(); ++plugin_index)
    {
        if (plugin_index != source_index && block_indices[plugin_index] < source_block_index)
        {
            ++destination_index;
        }
    }

    return destination_index;
}

// Maps an empty or source-owned fixed block drop to its implied linear destination.
[[nodiscard]] std::optional<BlockDropIntent> emptyBlockDropIntent(
    std::size_t source_index, std::size_t target_block_index,
    const std::vector<std::size_t>& block_indices, std::size_t block_count)
{
    if (source_index >= block_indices.size() || target_block_index >= block_count)
    {
        return std::nullopt;
    }

    const std::optional<std::size_t> target_plugin =
        pluginIndexAtBlock(block_indices, target_block_index);
    if (target_plugin.has_value() && *target_plugin != source_index)
    {
        return std::nullopt;
    }

    std::vector<std::size_t> next_block_indices = block_indices;
    next_block_indices[source_index] = target_block_index;
    return BlockDropIntent{
        .destination_index = destinationIndexForBlockIndices(next_block_indices, source_index),
        .block_indices = std::move(next_block_indices),
    };
}

// Maps an occupied fixed block drop to a placement by pushing blocks toward the nearest gap.
[[nodiscard]] std::optional<BlockDropIntent> occupiedBlockDropIntent(
    std::size_t source_index, std::size_t target_block_index, BlockDropDirection direction,
    const std::vector<std::size_t>& block_indices, std::size_t block_count)
{
    if (source_index >= block_indices.size() || target_block_index >= block_count ||
        block_indices[source_index] == target_block_index ||
        !validBlockIndices(block_indices, block_indices.size(), block_count))
    {
        return std::nullopt;
    }

    std::vector<std::optional<std::size_t>> plugin_at_block(block_count);
    for (std::size_t plugin_index = 0; plugin_index < block_indices.size(); ++plugin_index)
    {
        if (plugin_index == source_index)
        {
            continue;
        }

        plugin_at_block[block_indices[plugin_index]] = plugin_index;
    }

    if (!plugin_at_block[target_block_index].has_value())
    {
        return std::nullopt;
    }

    std::size_t empty_block_index = target_block_index;
    while (plugin_at_block[empty_block_index].has_value())
    {
        if (direction == BlockDropDirection::PushLeft)
        {
            if (empty_block_index == 0)
            {
                return std::nullopt;
            }

            --empty_block_index;
        }
        else
        {
            if (empty_block_index + 1 >= block_count)
            {
                return std::nullopt;
            }

            ++empty_block_index;
        }
    }

    std::vector<std::size_t> next_block_indices = block_indices;
    if (direction == BlockDropDirection::PushLeft)
    {
        for (std::size_t block_index = empty_block_index; block_index < target_block_index;
             ++block_index)
        {
            const std::optional<std::size_t> shifted_plugin = plugin_at_block[block_index + 1];
            if (!shifted_plugin.has_value())
            {
                return std::nullopt;
            }

            next_block_indices[*shifted_plugin] = block_index;
        }
    }
    else
    {
        for (std::size_t block_index = empty_block_index; block_index > target_block_index;
             --block_index)
        {
            const std::optional<std::size_t> shifted_plugin = plugin_at_block[block_index - 1];
            if (!shifted_plugin.has_value())
            {
                return std::nullopt;
            }

            next_block_indices[*shifted_plugin] = block_index;
        }
    }

    next_block_indices[source_index] = target_block_index;
    if (!validBlockIndices(next_block_indices, block_indices.size(), block_count))
    {
        return std::nullopt;
    }

    return BlockDropIntent{
        .destination_index = destinationIndexForBlockIndices(next_block_indices, source_index),
        .block_indices = std::move(next_block_indices),
    };
}

// Resolves the hovered fixed block and pointer side into a concrete visual drop placement.
[[nodiscard]] std::optional<BlockDropIntent> blockDropIntent(
    std::size_t source_index, std::size_t target_block_index, int local_x, int width,
    const std::vector<std::size_t>& block_indices, std::size_t block_count)
{
    const std::optional<std::size_t> target_plugin =
        pluginIndexAtBlock(block_indices, target_block_index);
    if (target_plugin.has_value() && *target_plugin != source_index)
    {
        return occupiedBlockDropIntent(
            source_index,
            target_block_index,
            blockDropDirectionForLocalX(local_x, width),
            block_indices,
            block_count);
    }

    return emptyBlockDropIntent(source_index, target_block_index, block_indices, block_count);
}

// Reports whether a fixed cell has any valid side or empty-space drop for this dragged plugin.
[[nodiscard]] bool canBlockReceiveDrop(
    std::size_t source_index, std::size_t target_block_index,
    const std::vector<std::size_t>& block_indices, std::size_t block_count)
{
    if (source_index >= block_indices.size() || target_block_index >= block_count)
    {
        return false;
    }

    if (emptyBlockDropIntent(source_index, target_block_index, block_indices, block_count)
            .has_value())
    {
        return true;
    }

    return occupiedBlockDropIntent(
               source_index,
               target_block_index,
               BlockDropDirection::PushLeft,
               block_indices,
               block_count)
               .has_value() ||
           occupiedBlockDropIntent(
               source_index,
               target_block_index,
               BlockDropDirection::PushRight,
               block_indices,
               block_count)
               .has_value();
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

// Paints the signal rail behind fixed block placeholders and plugin tiles.
class SignalChainView::SignalPathContent final : public juce::Component
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

    // Draws the dark path surface, full-width signal line, and subtle fixed-position markers.
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
            static_cast<float>(bounds.getX()),
            static_cast<float>(path_y),
            static_cast<float>(bounds.getRight()),
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

// Presents one fixed block placeholder on the path and accepts plugin-tile drops.
class SignalChainView::InsertSlotView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    // Creates the placeholder control for a stable fixed block index.
    InsertSlotView(std::size_t block_index, SignalChainView& view)
        : m_view(view)
        , m_block_index(block_index)
    {
        const juce::String block_index_text{std::to_string(m_block_index)};
        setComponentID(juce::String{"insert_slot_"} + block_index_text);
        m_button.setComponentID(juce::String{"insert_plugin_button_"} + block_index_text);
        m_button.setButtonText("+");
        m_button.setWantsKeyboardFocus(false);
        m_button.setMouseClickGrabsKeyboardFocus(false);
        m_button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m_button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        m_button.setColour(juce::TextButton::textColourOffId, g_insert_slot_placeholder);
        m_button.setColour(juce::TextButton::textColourOnId, g_insert_slot_drop_line);
        m_button.onClick = [this] { m_view.insertPluginAtBlockLocation(m_block_index); };
        // Empty fixed block locations stay visible without drawing old boundary rails.
        m_button.setAlpha(g_idle_insert_affordance_alpha);
        m_button.addMouseListener(this, false);
        addAndMakeVisible(m_button);
    }

    // Applies controller-derived editing availability to this fixed block location.
    void setEditingEnabled(bool is_empty, bool insert_enabled, bool move_enabled)
    {
        m_button.setVisible(is_empty);
        m_button.setEnabled(insert_enabled);
        m_drop_enabled = move_enabled;
        if (!m_drop_enabled && m_is_drag_hovered)
        {
            m_is_drag_hovered = false;
            m_view.clearPluginMovePreview();
            repaint();
        }
        updateButtonAffordance();
    }

    // Reports whether this placeholder can accept the dragged tile into a valid final position.
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

        return canBlockReceiveDrop(
            plugin->source_index,
            m_block_index,
            m_view.m_plugin_block_indices,
            visualBlockCount(m_view.m_state.plugins.size()));
    }

    // Shows the preview for dropping onto this empty fixed block location.
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        updateDropPreview(drag_source_details);
    }

    // Keeps the preview current while JUCE reports movement over the empty block location.
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        updateDropPreview(drag_source_details);
    }

    // Clears this cell's hover feedback when the drag leaves the fixed block location.
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& /*details*/) override
    {
        m_is_drag_hovered = false;
        repaint();
    }

    // Emits the same move intent used by tile-level drop paths.
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        m_is_drag_hovered = false;
        repaint();

        if (!m_drop_enabled)
        {
            m_view.clearPluginMovePreview();
            return;
        }

        auto [destination_index, block_indices] =
            dropPlacementParts(dropIntent(drag_source_details));
        m_view.completePluginDrop(
            drag_source_details.description, destination_index, std::move(block_indices));
    }

    // Keeps fixed cells above moving tiles during drags without blocking normal tile clicks.
    bool hitTest(int x, int y) override
    {
        juce::DragAndDropContainer* const container =
            juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container != nullptr && container->isDragAndDropActive())
        {
            return true;
        }

        return m_button.isVisible() && m_button.isEnabled() && m_button.getBounds().contains(x, y);
    }

    // Draws only the cell-level drop feedback; the placeholder button owns the subtle plus icon.
    void paint(juce::Graphics& g) override
    {
        if (m_is_drag_hovered)
        {
            const auto area = getLocalBounds().reduced(1).toFloat();
            g.setColour(g_insert_slot_drop_line);
            g.drawRoundedRectangle(area, 6.0f, 1.4f);
        }
    }

    // Keeps the compact placeholder icon centered on the signal path.
    void resized() override
    {
        const int button_size = std::min(g_insert_rail_width, getWidth());
        m_button.setBounds(getLocalBounds().withSizeKeepingCentre(button_size, button_size));
    }

    // Brightens the "+" affordance while the pointer is over an active placeholder.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        updateButtonAffordance();
    }

    // Restores the dim "+" affordance once the pointer leaves the placeholder and its button.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        updateButtonAffordance();
    }

private:
    // Recomputes the "+" opacity from whether the pointer is over this active placeholder.
    void updateButtonAffordance()
    {
        m_button.setAlpha(
            m_button.isEnabled() && isMouseOver(true) ? 1.0f : g_idle_insert_affordance_alpha);
    }

    // Resolves this fixed cell plus the current pointer side into a concrete drop intent.
    [[nodiscard]] std::optional<BlockDropIntent> dropIntent(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) const
    {
        if (!m_drop_enabled)
        {
            return std::nullopt;
        }

        const std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        if (!plugin.has_value())
        {
            return std::nullopt;
        }

        return blockDropIntent(
            plugin->source_index,
            m_block_index,
            drag_source_details.localPosition.x,
            getWidth(),
            m_view.m_plugin_block_indices,
            visualBlockCount(m_view.m_state.plugins.size()));
    }

    // Applies drag feedback for this fixed cell when it represents a valid move.
    void updateDropPreview(const juce::DragAndDropTarget::SourceDetails& drag_source_details)
    {
        const std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        const std::optional<BlockDropIntent> intent = dropIntent(drag_source_details);
        if (!plugin.has_value() || !intent.has_value())
        {
            m_is_drag_hovered = false;
            // Keep the last valid preview while the pointer crosses a blocked side.
            repaint();
            return;
        }

        m_is_drag_hovered = true;
        m_view.previewPluginMove(
            plugin->source_index, intent->destination_index, intent->block_indices);
        repaint();
    }

    // Owning view used to translate drops into move intents.
    SignalChainView& m_view;

    // Stable fixed block location represented by this control.
    std::size_t m_block_index{};

    // Compact insertion command button.
    juce::TextButton m_button;

    // True when plugin tiles may be dropped on this placeholder.
    bool m_drop_enabled{false};

    // True while a compatible tile drag is hovering over this placeholder.
    bool m_is_drag_hovered{false};
};

// Presents one compact plugin block in the horizontal chain strip and emits edit intents for its
// stored instance ID. The name and manufacturer sit below the block as a caption.
class SignalChainView::PluginTileView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    // Creates the tile with a stable plugin snapshot and the parent view listener.
    PluginTileView(
        core::PluginViewState plugin, std::size_t plugin_count, SignalChainView& view,
        Listener& listener)
        : m_view(view)
        , m_listener(listener)
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
        m_remove_button.setWantsKeyboardFocus(false);
        m_remove_button.setMouseClickGrabsKeyboardFocus(false);
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
        if (!m_move_enabled || m_drag_started || !event.mouseWasDraggedSinceMouseDown())
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
        m_view.clearUncommittedPluginMovePreviewAsync();
        if (event.mouseWasClicked())
        {
            m_listener.onOpenPluginPressed(m_plugin.instance_id);
        }
    }

    // Reports whether this occupied block can receive the dragged tile on the hovered side.
    [[nodiscard]] bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        return dropIntent(drag_source_details).has_value();
    }

    // Starts a visual reorder preview as the dragged tile enters this occupied block.
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        updateDropPreview(drag_source_details);
    }

    // Recomputes before/after intent when the pointer crosses the tile midpoint.
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        updateDropPreview(drag_source_details);
    }

    // Leaves the last valid preview active while the drag crosses between block targets.
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& /*details*/) override
    {}

    // Emits a move intent using the same final-index contract as the existing controller path.
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override
    {
        auto [destination_index, block_indices] =
            dropPlacementParts(dropIntent(drag_source_details));
        m_view.completePluginDrop(
            drag_source_details.description, destination_index, std::move(block_indices));
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
    // Resolves the hovered side of this block to a final visual placement.
    [[nodiscard]] std::optional<BlockDropIntent> dropIntent(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) const
    {
        if (!m_move_enabled)
        {
            return std::nullopt;
        }

        const std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        if (!plugin.has_value())
        {
            return std::nullopt;
        }

        if (m_plugin.chain_index >= m_view.m_plugin_block_indices.size())
        {
            return std::nullopt;
        }

        return blockDropIntent(
            plugin->source_index,
            m_view.m_plugin_block_indices[m_plugin.chain_index],
            drag_source_details.localPosition.x,
            getWidth(),
            m_view.m_plugin_block_indices,
            visualBlockCount(m_plugin_count));
    }

    // Applies transient layout so the chain previews where the dragged block would land.
    void updateDropPreview(const juce::DragAndDropTarget::SourceDetails& drag_source_details)
    {
        const std::optional<DraggedPlugin> plugin =
            parsePluginDragDescription(drag_source_details.description);
        if (!plugin.has_value())
        {
            return;
        }

        const std::optional<BlockDropIntent> intent = dropIntent(drag_source_details);
        if (!intent.has_value())
        {
            // Keep the last valid preview while the pointer crosses a blocked side.
            return;
        }

        m_view.previewPluginMove(
            plugin->source_index, intent->destination_index, intent->block_indices);
    }

    // Keeps the block highlight and remove affordance alive while either the tile or the child
    // remove button has the pointer.
    void updateHoverAffordance()
    {
        const bool is_hovered = isMouseOver(true);
        m_is_hovered = is_hovered;
        m_remove_button.setAlpha(is_hovered ? 1.0f : g_idle_remove_affordance_alpha);
        repaint();
    }

    // Owning view used to preview and emit block-location drops.
    SignalChainView& m_view;

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

// Creates the signal-chain controls and routes user intents through the owner.
SignalChainView::SignalChainView(Listener& listener)
    : m_listener(listener)
    , m_input_meter(AudioLevelMeterOrientation::Vertical)
    , m_output_gain_slider_look_and_feel(std::make_unique<OutputGainSliderLookAndFeel>())
    , m_output_meter(AudioLevelMeterOrientation::Vertical)
    , m_chain_content(std::make_unique<SignalPathContent>())
{
    setComponentID("signal_chain_view");

    m_input_meter.setComponentID("input_meter");
    addAndMakeVisible(m_input_meter);
    m_input_calibrate_button.setComponentID("input_calibrate_button");
    m_input_calibrate_button.setButtonText("Calibrate");
    m_input_calibrate_button.setWantsKeyboardFocus(false);
    m_input_calibrate_button.setMouseClickGrabsKeyboardFocus(false);
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
SignalChainView::~SignalChainView()
{
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (const std::unique_ptr<PluginTileView>& tile : m_plugin_tiles)
    {
        if (tile != nullptr)
        {
            animator.cancelAnimation(tile.get(), false);
        }
    }

    m_chain_viewport.setViewedComponent(nullptr, false);
    m_output_gain_slider.setLookAndFeel(nullptr);
}

// Stores the render state and updates controls whose enabledness is derived outside the view.
void SignalChainView::setState(const core::SignalChainViewState& state)
{
    std::vector<std::size_t> next_block_indices =
        reconciledPluginBlockIndices(m_state.plugins, m_plugin_block_indices, state.plugins);
    bool keep_committed_preview = false;
    if (m_pending_insert.has_value())
    {
        std::optional<std::vector<std::size_t>> inserted_block_indices =
            pendingInsertBlockIndicesForState(
                m_state.plugins,
                m_plugin_block_indices,
                state.plugins,
                m_pending_insert->chain_index,
                m_pending_insert->block_index);
        if (inserted_block_indices.has_value())
        {
            next_block_indices = std::move(*inserted_block_indices);
            m_pending_insert.reset();
        }
        else if (!samePluginOrder(m_state.plugins, state.plugins))
        {
            m_pending_insert.reset();
        }
    }

    if (m_drag_preview_committed && m_drag_preview.has_value())
    {
        std::optional<std::vector<std::size_t>> committed_block_indices =
            committedPreviewBlockIndicesForState(
                m_drag_preview->block_indices, m_state.plugins, state.plugins);
        if (committed_block_indices.has_value())
        {
            next_block_indices = std::move(*committed_block_indices);
        }
        else if (
            samePluginOrder(m_state.plugins, state.plugins) &&
            validBlockIndices(
                m_drag_preview->block_indices,
                state.plugins.size(),
                visualBlockCount(state.plugins.size()))
        )
        {
            next_block_indices = m_drag_preview->block_indices;
            keep_committed_preview = true;
        }
    }

    m_state = state;
    m_plugin_block_indices = std::move(next_block_indices);
    m_input_calibrate_button.setEnabled(m_state.input_calibrate_enabled);
    m_output_gain_slider.setEnabled(m_state.output_gain_controls_enabled);
    m_output_gain_slider.setValue(m_state.output_gain_db, juce::dontSendNotification);
    m_chain_viewport.setVisible(m_state.disabled_message.empty());
    m_chain_content->setPluginCount(m_state.plugins.size());
    if (!keep_committed_preview)
    {
        m_drag_preview.reset();
        m_drag_preview_committed = false;
    }
    rebuildPluginTiles();
    resized();
    repaint();
}

// Applies the live-rig meter values without rebuilding plugin tiles or changing controls.
void SignalChainView::setMeterLevels(
    common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level)
{
    m_input_meter.setLevel(input_level);
    m_output_meter.setLevel(output_level);
}

// Draws a compact plugin-chain view with gain labels and an empty-chain placeholder.
void SignalChainView::paint(juce::Graphics& g)
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
void SignalChainView::resized()
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
    layoutSignalPathContent(TileLayoutMotion::Immediate);
}

// Positions placeholders and moves plugin tiles using either immediate or animated bounds.
void SignalChainView::layoutSignalPathContent(TileLayoutMotion motion)
{
    const auto path_area = signalPathArea(m_chain_content->getLocalBounds());
    const std::size_t block_count = visualBlockCount(m_plugin_tiles.size());
    const std::vector<std::size_t>& active_block_indices =
        m_drag_preview.has_value() ? m_drag_preview->block_indices : m_plugin_block_indices;
    auto& animator = juce::Desktop::getInstance().getAnimator();
    const bool has_free_block = m_state.plugins.size() < block_count;

    for (std::size_t index = 0; index < m_insert_slots.size(); ++index)
    {
        const std::unique_ptr<InsertSlotView>& slot = m_insert_slots[index];
        if (slot == nullptr)
        {
            continue;
        }

        slot->setVisible(true);
        const bool is_empty = !pluginIndexAtBlock(active_block_indices, index).has_value();
        slot->setEditingEnabled(
            is_empty,
            m_state.insert_plugin_enabled && has_free_block && is_empty,
            m_state.move_plugins_enabled);
        slot->setBounds(blockCellBounds(path_area, index, block_count));
    }

    for (std::size_t index = 0; index < m_plugin_tiles.size(); ++index)
    {
        const std::unique_ptr<PluginTileView>& tile = m_plugin_tiles[index];
        if (tile == nullptr)
        {
            continue;
        }

        tile->setVisible(true);
        std::size_t block_index = index;
        if (index < active_block_indices.size() && active_block_indices[index] < block_count)
        {
            block_index = active_block_indices[index];
        }

        const juce::Rectangle<int> target_bounds =
            pluginBlockBounds(path_area, block_index, block_count);
        if (motion == TileLayoutMotion::Animated)
        {
            if (animator.getComponentDestination(tile.get()) != target_bounds)
            {
                animator.animateComponent(
                    tile.get(),
                    target_bounds,
                    1.0f,
                    g_signal_preview_animation_ms,
                    false,
                    g_signal_preview_animation_start_speed,
                    g_signal_preview_animation_end_speed);
            }
        }
        else
        {
            animator.cancelAnimation(tile.get(), false);
            tile->setAlpha(1.0f);
            tile->setBounds(target_bounds);
        }
    }
}

// Converts an empty fixed block location into the matching linear insertion index.
void SignalChainView::insertPluginAtBlockLocation(std::size_t block_index)
{
    const std::size_t block_count = visualBlockCount(m_state.plugins.size());
    if (!m_state.insert_plugin_enabled || block_index >= block_count ||
        pluginIndexAtBlock(m_plugin_block_indices, block_index).has_value())
    {
        return;
    }

    const std::size_t chain_index =
        chainIndexForBlockInsertion(m_plugin_block_indices, block_index);
    m_pending_insert = PendingInsert{
        .chain_index = chain_index,
        .block_index = block_index,
    };
    m_listener.onInsertPluginPressed(chain_index);
}

// Converts a tile drop on a fixed block location into the existing move-plugin intent.
void SignalChainView::movePluginToBlockLocation(
    std::string instance_id, std::size_t source_index, std::size_t destination_index)
{
    if (!m_state.move_plugins_enabled)
    {
        return;
    }

    if (source_index >= m_state.plugins.size() || destination_index >= m_state.plugins.size() ||
        source_index == destination_index)
    {
        return;
    }

    m_listener.onMovePluginPressed(std::move(instance_id), destination_index);
}

// Centralizes drop finalization so every target preserves preview and no-op placement behavior.
void SignalChainView::completePluginDrop(
    const juce::var& drag_description, std::optional<std::size_t> destination_index,
    std::vector<std::size_t> block_indices)
{
    std::optional<DraggedPlugin> plugin = parsePluginDragDescription(drag_description);
    if (!plugin.has_value())
    {
        clearPluginMovePreview();
        return;
    }

    if (!destination_index.has_value())
    {
        if (!dropCurrentPluginMovePreview(std::move(plugin->instance_id), plugin->source_index))
        {
            clearPluginMovePreview();
        }
        return;
    }

    previewPluginMove(plugin->source_index, *destination_index, block_indices);
    if (*destination_index == plugin->source_index)
    {
        applyPluginBlockIndices(std::move(block_indices));
        return;
    }

    commitPluginMovePreview();
    movePluginToBlockLocation(
        std::move(plugin->instance_id), plugin->source_index, *destination_index);
}

// Stores a drag-hover preview and relayouts the chain if the preview target changed.
void SignalChainView::previewPluginMove(
    std::size_t source_index, std::size_t destination_index, std::vector<std::size_t> block_indices)
{
    const std::size_t block_count = visualBlockCount(m_state.plugins.size());
    if (source_index >= m_state.plugins.size() || destination_index >= m_state.plugins.size() ||
        !validBlockIndices(block_indices, m_state.plugins.size(), block_count))
    {
        clearPluginMovePreview();
        return;
    }

    const DragPreview next_preview{
        .source_index = source_index,
        .destination_index = destination_index,
        .block_indices = std::move(block_indices),
    };
    if (m_drag_preview.has_value() && m_drag_preview->source_index == next_preview.source_index &&
        m_drag_preview->destination_index == next_preview.destination_index &&
        m_drag_preview->block_indices == next_preview.block_indices)
    {
        return;
    }

    m_drag_preview = next_preview;
    m_drag_preview_committed = false;
    layoutSignalPathContent(TileLayoutMotion::Animated);
    m_chain_content->repaint();
}

// Commits the current preview so release and same-order refreshes keep the fixed-slot layout.
void SignalChainView::commitPluginMovePreview()
{
    if (m_drag_preview.has_value())
    {
        m_plugin_block_indices = m_drag_preview->block_indices;
        m_drag_preview_committed = true;
    }
}

// Applies the current preview when release lands on a side that cannot produce a new preview.
bool SignalChainView::dropCurrentPluginMovePreview(
    std::string instance_id, std::size_t source_index)
{
    if (!m_drag_preview.has_value() || m_drag_preview->source_index != source_index)
    {
        return false;
    }

    const DragPreview preview = *m_drag_preview;
    if (preview.destination_index == source_index)
    {
        applyPluginBlockIndices(preview.block_indices);
        return true;
    }

    commitPluginMovePreview();
    movePluginToBlockLocation(std::move(instance_id), source_index, preview.destination_index);
    return true;
}

// Applies a fixed-slot placement that leaves the plugin chain's linear order unchanged.
void SignalChainView::applyPluginBlockIndices(std::vector<std::size_t> block_indices)
{
    if (!validBlockIndices(
            block_indices, m_state.plugins.size(), visualBlockCount(m_state.plugins.size())))
    {
        clearPluginMovePreview();
        return;
    }

    m_plugin_block_indices = std::move(block_indices);
    m_drag_preview.reset();
    m_drag_preview_committed = false;
    layoutSignalPathContent(TileLayoutMotion::Animated);
    m_chain_content->repaint();
}

// Removes any drag-hover preview and restores authoritative chain layout.
void SignalChainView::clearPluginMovePreview()
{
    m_drag_preview_committed = false;
    if (!m_drag_preview.has_value())
    {
        return;
    }

    m_drag_preview.reset();
    layoutSignalPathContent(TileLayoutMotion::Animated);
    m_chain_content->repaint();
}

// Leaves committed previews alone so source mouse-up does not snap back a valid drop.
void SignalChainView::clearUncommittedPluginMovePreview()
{
    if (m_drag_preview_committed)
    {
        return;
    }

    clearPluginMovePreview();
}

// Lets JUCE deliver the target drop callback before source mouse-up can clear the preview.
void SignalChainView::clearUncommittedPluginMovePreviewAsync()
{
    juce::Component::SafePointer<SignalChainView> safe_this{this};
    (void)juce::MessageManager::callAsync([safe_this] {
        if (safe_this != nullptr)
        {
            safe_this->clearUncommittedPluginMovePreview();
        }
    });
}

// Recreates child tiles from the latest controller state so each control carries a stable ID.
void SignalChainView::rebuildPluginTiles()
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
            juce::Desktop::getInstance().getAnimator().cancelAnimation(tile.get(), false);
            m_chain_content->removeChildComponent(tile.get());
        }
    }

    m_insert_slots.clear();
    m_plugin_tiles.clear();
    if (!m_state.disabled_message.empty())
    {
        return;
    }

    m_plugin_tiles.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto tile =
            std::make_unique<PluginTileView>(plugin, m_state.plugins.size(), *this, m_listener);
        tile->setEditEnabled(m_state.move_plugins_enabled, m_state.remove_plugins_enabled);
        m_chain_content->addAndMakeVisible(*tile);
        m_plugin_tiles.push_back(std::move(tile));
    }

    const std::size_t block_count = visualBlockCount(m_state.plugins.size());
    m_insert_slots.reserve(block_count);
    const bool has_free_block = m_state.plugins.size() < block_count;
    for (std::size_t index = 0; index < block_count; ++index)
    {
        auto slot = std::make_unique<InsertSlotView>(index, *this);
        const bool is_empty = !pluginIndexAtBlock(m_plugin_block_indices, index).has_value();
        slot->setEditingEnabled(
            is_empty,
            m_state.insert_plugin_enabled && has_free_block && is_empty,
            m_state.move_plugins_enabled);
        m_chain_content->addAndMakeVisible(*slot);
        m_insert_slots.push_back(std::move(slot));
    }
}

} // namespace rock_hero::editor::ui
