/*!
\file signal_chain_view_state.h
\brief Framework-free state rendered by the signal-chain panel.
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One plugin currently displayed in the chain. */
struct PluginViewState
{
    /*! \brief Opaque instance ID assigned by the plugin host. */
    std::string instance_id;

    /*! \brief Opaque plugin ID used to create this instance. */
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
    \brief Compares two plugin view states by their stored values.
    \param lhs Left-hand plugin view state.
    \param rhs Right-hand plugin view state.
    \return True when both plugin view states store equal values.
    */
    friend bool operator==(const PluginViewState& lhs, const PluginViewState& rhs) = default;
};

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
