/*!
\file plugin_tile_view.h
\brief One compact plugin block in the horizontal chain strip.

Defines the SignalChainView nested member class out of line so the component has its own
translation unit while keeping the nested-member access it needs to the view's drag-preview
state. Only signal-chain view translation units include this header.
*/

#pragma once

#include "signal_chain/signal_chain_view.h"

#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/core/signal_chain/plugin_view_state.h>

namespace rock_hero::editor::ui
{

/*! \brief Icon vocabulary painted inside plugin tiles. */
enum class PluginIconType : std::uint8_t
{
    /*! \brief Neutral waveform icon for unknown plugin kinds. */
    Generic,

    /*! \brief Amplifier head icon. */
    Amp,

    /*! \brief Speaker cabinet icon. */
    Cab,

    /*! \brief Drive/distortion pedal icon. */
    Drive,

    /*! \brief Repeating echo icon. */
    Delay,

    /*! \brief Reverb cloud icon. */
    Reverb,

    /*! \brief Modulation wave icon. */
    Modulation,

    /*! \brief Compressor bar icon. */
    Dynamics,

    /*! \brief Equalizer band icon. */
    Eq,

    /*! \brief Noise-gate threshold icon. */
    Gate,

    /*! \brief Pitch arrow icon. */
    Pitch,

    /*! \brief Wah pedal icon. */
    Wah,
};

/*!
\brief Presents one compact plugin block in the chain strip and emits edit intents.

The name and manufacturer sit below the block as a caption.
*/
class SignalChainView::PluginTileView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    /*!
    \brief Creates the tile with a stable plugin snapshot and the parent view listener.
    \param plugin Stable plugin snapshot represented by this tile.
    \param view Owning view used to preview and emit block-location drops.
    \param listener Listener that receives this tile's remove, open, and move intents.
    */
    PluginTileView(core::PluginViewState plugin, SignalChainView& view, Listener& listener);

    /*!
    \brief Applies controller-derived edit availability.

    The move gate governs drag-to-reorder rather than discrete buttons, so it toggles no child
    control.

    \param move_enabled True when the tile can initiate drag-based reordering.
    \param remove_enabled True when the remove button should accept clicks.
    */
    void setEditEnabled(bool move_enabled, bool remove_enabled);

    /*! \brief Draws the icon-only block, primary name, and secondary maker line. */
    void paint(juce::Graphics& g) override;

    /*! \brief Pins the remove button to the tile's top-right corner. */
    void resized() override;

    /*! \brief Resets drag-start state at the beginning of each pointer sequence. */
    void mouseDown(const juce::MouseEvent& event) override;

    /*! \brief Starts a JUCE drag operation for reorderable plugin tiles. */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*! \brief Treats a tile click as an editor-window request while ignoring drag releases. */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Reports whether this occupied block can receive the dragged tile.
    \param drag_source_details Drag payload delivered by JUCE.
    \return True when the drop would produce a valid final placement.
    */
    [[nodiscard]] bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*! \brief Starts a visual reorder preview as the dragged tile enters this block. */
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*! \brief Keeps the preview current while JUCE reports movement over the tile. */
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*! \brief Leaves the last valid preview active while the drag crosses between targets. */
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& details) override;

    /*! \brief Emits a move intent using the same final-index contract as the controller path. */
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& drag_source_details) override;

    /*! \brief Highlights the tile and reveals its remove "X" while the pointer is over it. */
    void mouseEnter(const juce::MouseEvent& event) override;

    /*! \brief Clears the tile affordances when the pointer leaves the plugin tile. */
    void mouseExit(const juce::MouseEvent& event) override;

private:
    [[nodiscard]] std::optional<SignalChainBlockLayout::DropIntent> dropIntent(
        const juce::DragAndDropTarget::SourceDetails& drag_source_details) const;

    void updateDropPreview(const juce::DragAndDropTarget::SourceDetails& drag_source_details);

    void showDisplayTypeMenu();

    void handleDisplayTypeMenuSelection(int selected_id);

    void updateHoverAffordance();

    // Owning view used to preview and emit block-location drops.
    SignalChainView& m_view;

    // Listener that receives this tile's remove, open, and move intents.
    Listener& m_listener;

    // Stable plugin snapshot represented by this tile.
    core::PluginViewState m_plugin;

    // Core-derived display type used to draw the tile icon.
    PluginIconType m_icon_type{PluginIconType::Generic};

    // Accent color paired with the core-derived display type.
    juce::Colour m_accent{};

    // Button that emits a remove intent for this tile's plugin instance.
    juce::TextButton m_remove_button;

    // True while the pointer is over the tile, driving the hover accent and remove reveal.
    bool m_is_hovered{false};

    // True when the tile can initiate drag-based reordering.
    bool m_move_enabled{false};

    // Prevents repeated startDragging() calls during one mouse drag sequence.
    bool m_drag_started{false};
};

} // namespace rock_hero::editor::ui
