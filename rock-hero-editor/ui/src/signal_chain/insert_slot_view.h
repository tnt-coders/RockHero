/*!
\file insert_slot_view.h
\brief Fixed block placeholder on the signal path that accepts plugin-tile drops.

Defines the SignalChainView nested member class out of line so the component has its own
translation unit while keeping the nested-member access it needs to the view's drag-preview
state. Only signal-chain view translation units include this header.
*/

#pragma once

#include "signal_chain/signal_chain_view.h"

#include <cstddef>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace rock_hero::editor::ui
{

/*! \brief Presents one fixed block placeholder on the path and accepts plugin-tile drops. */
class SignalChainView::InsertSlotView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    /*!
    \brief Creates the placeholder control for a stable fixed block index.
    \param block_index Stable fixed block location represented by this control.
    \param view Owning view used to translate drops into move intents.
    */
    InsertSlotView(std::size_t block_index, SignalChainView& view);

    /*!
    \brief Applies controller-derived editing availability to this fixed block location.
    \param is_empty True when no plugin occupies this block.
    \param insert_enabled True when the insertion button should accept clicks.
    \param move_enabled True when plugin tiles may be dropped on this placeholder.
    */
    void setEditingEnabled(bool is_empty, bool insert_enabled, bool move_enabled);

    /*!
    \brief Reports whether this placeholder can accept the dragged tile into a valid position.
    \param drag_source_details Drag payload delivered by JUCE.
    \return True when the drop would produce a valid final placement.
    */
    [[nodiscard]] bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*!
    \brief Shows the preview for dropping onto this fixed block location.
    \param drag_source_details Drag payload delivered by JUCE.
    */
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*!
    \brief Keeps the preview current while JUCE reports movement over the location.
    \param drag_source_details Drag payload delivered by JUCE.
    */
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*!
    \brief Clears this cell's hover feedback when the drag leaves the fixed location.
    \param details Drag payload delivered by JUCE.
    */
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& details) override;

    /*!
    \brief Emits the same move intent used by tile-level drop paths.
    \param drag_source_details Drag payload delivered by JUCE.
    */
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*!
    \brief Keeps fixed cells above moving tiles during drags without blocking tile clicks.
    \param x Pointer x position in local coordinates.
    \param y Pointer y position in local coordinates.
    \return True when this placeholder should receive the pointer event.
    */
    bool hitTest(int x, int y) override;

    /*!
    \brief Draws only the cell-level drop feedback; the button owns the plus icon.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Keeps the compact placeholder icon centered on the signal path. */
    void resized() override;

    /*!
    \brief Brightens the "+" affordance while the pointer is over an active placeholder.
    \param event Pointer event delivered by JUCE.
    */
    void mouseEnter(const juce::MouseEvent& event) override;

    /*!
    \brief Restores the dim "+" affordance once the pointer leaves the placeholder.
    \param event Pointer event delivered by JUCE.
    */
    void mouseExit(const juce::MouseEvent& event) override;

private:
    void updateButtonAffordance();

    [[nodiscard]] std::optional<SignalChainBlockLayout::DropIntent> dropIntent(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) const;

    void updateDropPreview(const juce::DragAndDropTarget::SourceDetails& drag_source_details);

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

} // namespace rock_hero::editor::ui
