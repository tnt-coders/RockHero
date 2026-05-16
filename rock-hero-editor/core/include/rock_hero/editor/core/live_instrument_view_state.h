/*!
\file live_instrument_view_state.h
\brief Framework-free state rendered by the live instrument control panel.
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One plugin currently displayed in the live instrument chain. */
struct LivePluginViewState
{
    /*! \brief Opaque instance ID assigned by the plugin host. */
    std::string instance_id;

    /*! \brief Opaque plugin candidate ID used to create this instance. */
    std::string plugin_id;

    /*! \brief User-facing plugin name. */
    std::string name;

    /*! \brief User-facing plugin manufacturer, when supplied by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief Zero-based position in the current linear live chain. */
    std::size_t chain_index{};

    /*!
    \brief Compares two live plugin view states by their stored values.
    \param lhs Left-hand plugin view state.
    \param rhs Right-hand plugin view state.
    \return True when both plugin view states store equal values.
    */
    friend bool operator==(const LivePluginViewState& lhs, const LivePluginViewState& rhs) =
        default;
};

/*! \brief State rendered by the live instrument panel. */
struct LiveInstrumentViewState
{
    /*! \brief Enables or disables the add-plugin command. */
    bool add_plugin_enabled{false};

    /*! \brief Current linear live instrument plugin chain. */
    std::vector<LivePluginViewState> plugins;

    /*!
    \brief Compares two live instrument view states by their stored values.
    \param lhs Left-hand live instrument view state.
    \param rhs Right-hand live instrument view state.
    \return True when both live instrument view states store equal values.
    */
    friend bool operator==(const LiveInstrumentViewState& lhs, const LiveInstrumentViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
