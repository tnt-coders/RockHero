#include "insert_slot_view.h"

#include "shared/editor_theme.h"
#include "signal_chain/plugin_drag.h"

#include <algorithm>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_insert_rail_width{28};
// Insert rails stay ghosted until hovered, but legibly so: the old 0.12 idle alpha compounded
// with a 0.16-alpha glyph to roughly 2% white, which was nearly impossible to spot.
constexpr float g_idle_insert_affordance_alpha{0.4f};
const juce::Colour g_insert_slot_placeholder{juce::Colours::white.withAlpha(0.8f)};
const juce::Colour g_insert_slot_drop_line{editorTheme().accent};

} // namespace

SignalChainView::InsertSlotView::InsertSlotView(std::size_t block_index, SignalChainView& view)
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

void SignalChainView::InsertSlotView::setEditingEnabled(
    bool is_empty, bool insert_enabled, bool move_enabled)
{
    m_button.setVisible(is_empty);
    m_button.setEnabled(insert_enabled);
    m_drop_enabled = move_enabled;
    if (!m_drop_enabled)
    {
        if (m_is_drag_hovered)
        {
            m_is_drag_hovered = false;
            m_view.clearPluginMovePreview();
            repaint();
        }
    }
    updateButtonAffordance();
}

bool SignalChainView::InsertSlotView::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
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

    return m_view.m_block_layout.canReceiveDrop(plugin->source_index, m_block_index);
}

void SignalChainView::InsertSlotView::itemDragEnter(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    updateDropPreview(drag_source_details);
}

void SignalChainView::InsertSlotView::itemDragMove(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    updateDropPreview(drag_source_details);
}

void SignalChainView::InsertSlotView::itemDragExit(
    const juce::DragAndDropTarget::SourceDetails& /*details*/)
{
    m_is_drag_hovered = false;
    repaint();
}

void SignalChainView::InsertSlotView::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    m_is_drag_hovered = false;
    repaint();

    if (!m_drop_enabled)
    {
        m_view.clearPluginMovePreview();
        return;
    }

    std::optional<SignalChainBlockLayout::DropIntent> intent = dropIntent(drag_source_details);
    m_view.completePluginDrop(drag_source_details.description, std::move(intent));
}

bool SignalChainView::InsertSlotView::hitTest(int x, int y)
{
    const juce::DragAndDropContainer* const container =
        juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container != nullptr && container->isDragAndDropActive())
    {
        return true;
    }

    return m_button.isVisible() && m_button.isEnabled() && m_button.getBounds().contains(x, y);
}

void SignalChainView::InsertSlotView::paint(juce::Graphics& g)
{
    if (m_is_drag_hovered)
    {
        const auto area = getLocalBounds().reduced(1).toFloat();
        g.setColour(g_insert_slot_drop_line);
        g.drawRoundedRectangle(area, 6.0f, 1.4f);
    }
}

void SignalChainView::InsertSlotView::resized()
{
    const int button_size = std::min(g_insert_rail_width, getWidth());
    m_button.setBounds(getLocalBounds().withSizeKeepingCentre(button_size, button_size));
}

void SignalChainView::InsertSlotView::mouseEnter(const juce::MouseEvent& /*event*/)
{
    updateButtonAffordance();
}

void SignalChainView::InsertSlotView::mouseExit(const juce::MouseEvent& /*event*/)
{
    updateButtonAffordance();
}

// Recomputes the "+" opacity from whether the pointer is over this active placeholder.
void SignalChainView::InsertSlotView::updateButtonAffordance()
{
    m_button.setAlpha(
        m_button.isEnabled() && isMouseOver(true) ? 1.0f : g_idle_insert_affordance_alpha);
}

// Resolves this fixed cell into a concrete drop intent.
std::optional<SignalChainBlockLayout::DropIntent> SignalChainView::InsertSlotView::dropIntent(
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

    return m_view.m_block_layout.dropIntent(plugin->source_index, m_block_index);
}

// Applies drag feedback for this fixed cell when it represents a valid move.
void SignalChainView::InsertSlotView::updateDropPreview(
    const juce::DragAndDropTarget::SourceDetails& drag_source_details)
{
    const std::optional<DraggedPlugin> plugin =
        parsePluginDragDescription(drag_source_details.description);
    std::optional<SignalChainBlockLayout::DropIntent> intent = dropIntent(drag_source_details);
    if (!plugin.has_value() || !intent.has_value())
    {
        m_is_drag_hovered = false;
        // Keep the last valid preview while the pointer crosses an invalid target.
        repaint();
        return;
    }

    m_is_drag_hovered = true;
    m_view.previewPluginMove(plugin->source_index, std::move(*intent));
    repaint();
}

} // namespace rock_hero::editor::ui
