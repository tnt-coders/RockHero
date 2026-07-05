#include "plugin_tile_view.h"

#include "signal_chain/plugin_drag.h"
#include "signal_chain/signal_chain_view_metrics.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_tile_remove_button_size{20};
constexpr int g_tile_remove_button_inset{3};
constexpr int g_tile_inset{6};
// Tile removal stays invisible at rest so plugin blocks read as icon-only until hovered.
constexpr float g_idle_remove_affordance_alpha{0.0f};
const juce::Colour g_plugin_tile_background{juce::Colour{0xff1b2027}};
const juce::Colour g_plugin_tile_hover_background{juce::Colour{0xff222b35}};
const juce::Colour g_plugin_tile_border{juce::Colours::black.withAlpha(0.6f)};
const juce::Colour g_plugin_tile_hover_border{juce::Colours::white.withAlpha(0.7f)};
constexpr int g_use_default_display_type_id{1};
constexpr int g_first_display_type_id{2};
constexpr std::array<core::PluginDisplayType, 13> g_display_type_menu_values{
    core::PluginDisplayType::Amp,
    core::PluginDisplayType::Cab,
    core::PluginDisplayType::Distortion,
    core::PluginDisplayType::Delay,
    core::PluginDisplayType::Reverb,
    core::PluginDisplayType::Modulation,
    core::PluginDisplayType::Dynamics,
    core::PluginDisplayType::Eq,
    core::PluginDisplayType::Gate,
    core::PluginDisplayType::Pitch,
    core::PluginDisplayType::Filter,
    core::PluginDisplayType::Instrument,
    core::PluginDisplayType::Uncategorized,
};

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

// Converts the core-owned display type into this view's painted icon vocabulary.
[[nodiscard]] PluginIconType pluginIconTypeFor(core::PluginDisplayType display_type)
{
    switch (display_type)
    {
        case core::PluginDisplayType::Amp:
        {
            return PluginIconType::Amp;
        }
        case core::PluginDisplayType::Cab:
        {
            return PluginIconType::Cab;
        }
        case core::PluginDisplayType::Distortion:
        {
            return PluginIconType::Drive;
        }
        case core::PluginDisplayType::Delay:
        {
            return PluginIconType::Delay;
        }
        case core::PluginDisplayType::Reverb:
        {
            return PluginIconType::Reverb;
        }
        case core::PluginDisplayType::Modulation:
        {
            return PluginIconType::Modulation;
        }
        case core::PluginDisplayType::Dynamics:
        {
            return PluginIconType::Dynamics;
        }
        case core::PluginDisplayType::Eq:
        {
            return PluginIconType::Eq;
        }
        case core::PluginDisplayType::Gate:
        {
            return PluginIconType::Gate;
        }
        case core::PluginDisplayType::Pitch:
        {
            return PluginIconType::Pitch;
        }
        case core::PluginDisplayType::Filter:
        {
            return PluginIconType::Wah;
        }
        case core::PluginDisplayType::Instrument:
        case core::PluginDisplayType::Uncategorized:
        {
            return PluginIconType::Generic;
        }
    }

    return PluginIconType::Generic;
}

// Reports whether a display type appears in a core-owned display type set.
[[nodiscard]] bool containsDisplayType(
    const std::vector<core::PluginDisplayType>& values, core::PluginDisplayType value)
{
    return std::ranges::find(values, value) != values.end();
}

// Labels scanner-recognized menu types so manual-only overrides stay visually distinct.
[[nodiscard]] juce::String displayTypeMenuLabel(
    const core::PluginViewState& plugin, core::PluginDisplayType display_type)
{
    juce::String label{core::pluginDisplayTypeLabel(display_type)};
    if (containsDisplayType(plugin.scanned_display_types, display_type))
    {
        label += " (scanned)";
    }

    return label;
}

// Assigns a restrained accent so unknown plugins still fit the path while known types differ.
[[nodiscard]] juce::Colour iconAccentColor(PluginIconType icon_type)
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

// Draws a compact type hint inside a tile without requiring plugin-host-specific artwork.
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

} // namespace

SignalChainView::PluginTileView::PluginTileView(
    core::PluginViewState plugin, SignalChainView& view, Listener& listener)
    : m_view(view)
    , m_listener(listener)
    , m_plugin(std::move(plugin))
    , m_icon_type(pluginIconTypeFor(m_plugin.primary_display_type))
    , m_accent(iconAccentColor(m_icon_type))
{
    setComponentID(juce::String{"plugin_tile_"} + juce::String{m_plugin.instance_id});
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    m_remove_button.setComponentID(
        juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
    // Keep the remove affordance compact in the tile corner.
    m_remove_button.setButtonText("X");
    m_remove_button.setWantsKeyboardFocus(false);
    m_remove_button.setMouseClickGrabsKeyboardFocus(false);
    m_remove_button.onClick = [this] { m_listener.onRemovePluginPressed(m_plugin.instance_id); };
    // Child-button hover can hide the tile's own hover, so keep the shared affordance in sync
    // with descendant pointer state as well as direct tile pointer state.
    m_remove_button.onStateChange = [this] { updateHoverAffordance(); };
    // The "X" stays hidden until hover, so a resting tile reads as one clean icon target.
    m_remove_button.setAlpha(g_idle_remove_affordance_alpha);
    addAndMakeVisible(m_remove_button);
}

void SignalChainView::PluginTileView::setEditEnabled(bool move_enabled, bool remove_enabled)
{
    m_move_enabled = move_enabled;
    m_remove_button.setEnabled(remove_enabled);
}

void SignalChainView::PluginTileView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto tile_area = pluginTileArea(bounds);
    auto label_area =
        bounds.withTrimmedTop((tile_area.getBottom() - bounds.getY()) + g_signal_block_label_gap);
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

void SignalChainView::PluginTileView::resized()
{
    m_remove_button.setBounds(pluginTileArea(getLocalBounds())
                                  .reduced(g_tile_remove_button_inset)
                                  .removeFromTop(g_tile_remove_button_size)
                                  .removeFromRight(g_tile_remove_button_size));
}

void SignalChainView::PluginTileView::mouseDown(const juce::MouseEvent& /*event*/)
{
    m_drag_started = false;
}

void SignalChainView::PluginTileView::mouseDrag(const juce::MouseEvent& event)
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

void SignalChainView::PluginTileView::mouseUp(const juce::MouseEvent& event)
{
    m_drag_started = false;
    m_view.clearPluginMovePreviewAsync();
    if (!event.mouseWasClicked())
    {
        return;
    }

    if (event.mods.isPopupMenu())
    {
        showDisplayTypeMenu();
        return;
    }

    m_listener.onOpenPluginPressed(m_plugin.instance_id);
}

bool SignalChainView::PluginTileView::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    return dropIntent(drag_source_details).has_value();
}

void SignalChainView::PluginTileView::itemDragEnter(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    updateDropPreview(drag_source_details);
}

void SignalChainView::PluginTileView::itemDragMove(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    updateDropPreview(drag_source_details);
}

void SignalChainView::PluginTileView::itemDragExit(
    const juce::DragAndDropTarget::SourceDetails& /*details*/)
{}

void SignalChainView::PluginTileView::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    std::optional<SignalChainBlockLayout::DropIntent> intent = dropIntent(drag_source_details);
    m_view.completePluginDrop(drag_source_details.description, std::move(intent));
}

void SignalChainView::PluginTileView::mouseEnter(const juce::MouseEvent& /*event*/)
{
    updateHoverAffordance();
}

void SignalChainView::PluginTileView::mouseExit(const juce::MouseEvent& /*event*/)
{
    updateHoverAffordance();
}

// Resolves this occupied block to a final visual placement.
std::optional<SignalChainBlockLayout::DropIntent> SignalChainView::PluginTileView::dropIntent(
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

    const std::optional<std::size_t> target_block_index =
        m_view.m_block_layout.blockForPlugin(m_plugin.chain_index);
    if (!target_block_index.has_value())
    {
        return std::nullopt;
    }

    return m_view.m_block_layout.dropIntent(plugin->source_index, *target_block_index);
}

// Applies transient layout so the chain previews where the dragged block would land.
void SignalChainView::PluginTileView::updateDropPreview(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    const std::optional<DraggedPlugin> plugin =
        parsePluginDragDescription(drag_source_details.description);
    if (!plugin.has_value())
    {
        return;
    }

    std::optional<SignalChainBlockLayout::DropIntent> intent = dropIntent(drag_source_details);
    if (!intent.has_value())
    {
        // Keep the last valid preview while the pointer crosses an invalid target.
        return;
    }

    m_view.previewPluginMove(plugin->source_index, std::move(*intent));
}

// Opens the display type override menu for this plugin tile.
void SignalChainView::PluginTileView::showDisplayTypeMenu()
{
    juce::PopupMenu menu;
    const juce::String default_label =
        juce::String{"Use default ("} +
        juce::String{core::pluginDisplayTypeLabel(m_plugin.automatic_display_type)} + ")";
    menu.addItem(
        g_use_default_display_type_id,
        default_label,
        true,
        !m_plugin.display_type_override.has_value());
    menu.addSeparator();

    for (std::size_t index = 0; index < g_display_type_menu_values.size(); ++index)
    {
        const core::PluginDisplayType display_type = g_display_type_menu_values.at(index);
        menu.addItem(
            g_first_display_type_id + static_cast<int>(index),
            displayTypeMenuLabel(m_plugin, display_type),
            true,
            m_plugin.display_type_override == std::optional{display_type});
    }

    const juce::Component::SafePointer<PluginTileView> safe_this{this};
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(this), [safe_this](int selected_id) {
            if (safe_this != nullptr)
            {
                safe_this->handleDisplayTypeMenuSelection(selected_id);
            }
        });
}

// Applies one selected popup menu item to the listener contract.
void SignalChainView::PluginTileView::handleDisplayTypeMenuSelection(int selected_id)
{
    if (selected_id == g_use_default_display_type_id)
    {
        m_listener.onPluginDisplayTypeOverrideChanged(m_plugin.instance_id, std::nullopt);
        return;
    }

    const int type_index = selected_id - g_first_display_type_id;
    if (type_index < 0 || static_cast<std::size_t>(type_index) >= g_display_type_menu_values.size())
    {
        return;
    }

    m_listener.onPluginDisplayTypeOverrideChanged(
        m_plugin.instance_id, g_display_type_menu_values.at(static_cast<std::size_t>(type_index)));
}

// Keeps the block highlight and remove affordance alive while either the tile or the child
// remove button has the pointer.
void SignalChainView::PluginTileView::updateHoverAffordance()
{
    const bool is_hovered = isMouseOver(true);
    m_is_hovered = is_hovered;
    m_remove_button.setAlpha(is_hovered ? 1.0f : g_idle_remove_affordance_alpha);
    repaint();
}

} // namespace rock_hero::editor::ui
