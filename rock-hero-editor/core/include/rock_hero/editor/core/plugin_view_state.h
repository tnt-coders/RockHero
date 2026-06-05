/*!
\file plugin_view_state.h
\brief Framework-free state for one plugin rendered by the signal-chain view.
*/

#pragma once

#include <cstddef>
#include <string>

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

    /*! \brief Zero-based position in the current linear plugin chain. */
    std::size_t chain_index{};

    /*!
    \brief Fixed visual block this plugin occupies, allowing gaps in the editor layout.

    Distinct from chain_index: the chain index is the dense playback position, while the block index
    is the authored editor slot, which may leave empty blocks between plugins.
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

} // namespace rock_hero::editor::core
