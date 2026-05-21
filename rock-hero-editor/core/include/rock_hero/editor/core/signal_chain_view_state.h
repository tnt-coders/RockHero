/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain panel.
*/

#pragma once

#include <rock_hero/editor/core/plugin_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief State rendered by the signal-chain panel. */
struct SignalChainViewState
{
    /*! \brief Enables or disables the add-plugin command. */
    bool add_plugin_enabled{false};

    /*! \brief Enables or disables plugin removal commands. */
    bool remove_plugins_enabled{false};

    /*! \brief Current linear plugin chain. */
    std::vector<PluginViewState> plugins;

    /*!
    \brief Compares two signal-chain view states by their stored values.
    \param lhs Left-hand signal-chain view state.
    \param rhs Right-hand signal-chain view state.
    \return True when both signal-chain view states store equal values.
    */
    friend bool operator==(const SignalChainViewState& lhs, const SignalChainViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
