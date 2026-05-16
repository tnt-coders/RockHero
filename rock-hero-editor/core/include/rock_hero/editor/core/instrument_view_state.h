/*!
\file instrument_view_state.h
\brief Framework-free state rendered by the instrument control panel.
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

    /*! \brief Opaque plugin candidate ID used to create this instance. */
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

/*! \brief State rendered by the instrument panel. */
struct InstrumentViewState
{
    /*! \brief Enables or disables the add-plugin command. */
    bool add_plugin_enabled{false};

    /*! \brief Current linear plugin chain. */
    std::vector<PluginViewState> plugins;

    /*!
    \brief Compares two instrument view states by their stored values.
    \param lhs Left-hand instrument view state.
    \param rhs Right-hand instrument view state.
    \return True when both instrument view states store equal values.
    */
    friend bool operator==(const InstrumentViewState& lhs, const InstrumentViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
