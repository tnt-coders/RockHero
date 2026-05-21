/*!
\file plugin_browser_view_state.h
\brief Framework-free state rendered by the plugin browser window.
*/

#pragma once

#include <rock_hero/editor/core/plugin_candidate_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief State rendered by the plugin browser window. */
struct PluginBrowserViewState
{
    /*! \brief Reports whether the browser window should be visible. */
    bool visible{false};

    /*! \brief Enables or disables catalog rescanning. */
    bool scan_enabled{false};

    /*! \brief Enables or disables adding a selected browser plugin. */
    bool add_enabled{false};

    /*! \brief Plugins currently shown by the browser. */
    std::vector<PluginCandidateState> plugins;

    /*!
    \brief Compares two plugin browser states by their stored values.
    \param lhs Left-hand browser state.
    \param rhs Right-hand browser state.
    \return True when both browser states store equal values.
    */
    friend bool operator==(const PluginBrowserViewState& lhs, const PluginBrowserViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
