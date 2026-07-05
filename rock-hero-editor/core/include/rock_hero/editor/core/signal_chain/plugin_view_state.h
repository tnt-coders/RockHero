/*!
\file plugin_view_state.h
\brief Framework-free state for one plugin rendered by the signal-chain view.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One plugin currently displayed in the chain. */
struct PluginViewState
{
    /*! \brief Opaque instance ID assigned by the plugin host. */
    std::string instance_id;

    /*! \brief Opaque backend plugin ID associated with this instance. */
    std::string plugin_id;

    /*! \brief User-facing plugin name. */
    std::string name;

    /*! \brief User-facing plugin manufacturer, when supplied by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief Current display type used by signal-chain icon rendering. */
    PluginDisplayType primary_display_type{PluginDisplayType::Uncategorized};

    /*! \brief Automatic display type derived from scanner metadata. */
    PluginDisplayType automatic_display_type{PluginDisplayType::Uncategorized};

    /*! \brief Display types recognized directly from scanner category metadata. */
    std::vector<PluginDisplayType> scanned_display_types{};

    /*! \brief Manual display type selected for this plugin instance, when authored. */
    std::optional<PluginDisplayType> display_type_override{};

    /*! \brief Zero-based position in the current linear plugin chain. */
    std::size_t chain_index{};

    /*!
    \brief Fixed visual block this plugin occupies, allowing gaps in the editor layout.

    Distinct from chain_index: the chain index is the dense playback position, while the block
    index is the authored editor slot, which may leave empty blocks between plugins.
    */
    std::size_t block_index{};

    /*!
    \brief Compares two plugin view states by their stored values.
    \param lhs Left-hand plugin view state.
    \param rhs Right-hand plugin view state.
    \return True when both plugin view states store equal values.
    */
    friend bool operator==(const PluginViewState& lhs, const PluginViewState& rhs) = default;
};

/*!
\brief Reports whether two plugin lists hold the same instances in the same order.

Compares stable instance IDs only, so display-only changes such as a renamed plugin are not treated
as a structural reorder. Used by signal-chain reconciliation in both the editor core and the view.

\param previous_plugins Earlier plugin list.
\param next_plugins Later plugin list.
\return True when both lists are the same length with matching instance IDs at every position.
*/
[[nodiscard]] inline bool hasSamePluginOrder(
    const std::vector<PluginViewState>& previous_plugins,
    const std::vector<PluginViewState>& next_plugins)
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

} // namespace rock_hero::editor::core
