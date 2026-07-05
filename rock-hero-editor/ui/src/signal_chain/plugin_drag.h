/*!
\file plugin_drag.h
\brief Drag-payload codec shared by the signal-chain view and its drag-and-drop children.
*/

#pragma once

#include <cstddef>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/core/signal_chain/plugin_view_state.h>
#include <string>

namespace rock_hero::editor::ui
{

/*! \brief Decoded plugin-tile drag payload. */
struct DraggedPlugin
{
    /*! \brief Opaque plugin instance ID carried by the drag. */
    std::string instance_id;

    /*! \brief User-visible chain index the drag started from. */
    std::size_t source_index{};
};

/*!
\brief Encodes enough tile state for slot drop targets to compute final move destinations.
\param plugin Plugin snapshot represented by the dragged tile.
\return Drag description string for juce::DragAndDropContainer::startDragging().
*/
[[nodiscard]] juce::String makePluginDragDescription(const core::PluginViewState& plugin);

/*!
\brief Decodes a plugin-tile drag payload while rejecting unrelated JUCE drag operations.
\param description Drag description delivered by a JUCE drag-and-drop callback.
\return Decoded payload, or empty when the description is not a plugin-tile drag.
*/
[[nodiscard]] std::optional<DraggedPlugin> parsePluginDragDescription(const juce::var& description);

} // namespace rock_hero::editor::ui
