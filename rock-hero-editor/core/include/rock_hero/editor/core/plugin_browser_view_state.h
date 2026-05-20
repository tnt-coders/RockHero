/*!
\file plugin_browser_view_state.h
\brief Framework-free state rendered by the plugin browser window.
*/

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One scanned plugin candidate shown in the plugin browser. */
struct PluginBrowserCandidateViewState
{
    /*! \brief Opaque candidate ID passed back to the plugin host when adding this plugin. */
    std::string id;

    /*! \brief User-facing plugin name reported by the scanner. */
    std::string name;

    /*! \brief User-facing manufacturer name reported by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief File or bundle path that produced this candidate. */
    std::filesystem::path file_path;

    /*!
    \brief Compares two browser candidates by their stored values.
    \param lhs Left-hand browser candidate.
    \param rhs Right-hand browser candidate.
    \return True when both candidates store equal values.
    */
    friend bool operator==(
        const PluginBrowserCandidateViewState& lhs,
        const PluginBrowserCandidateViewState& rhs) = default;
};

/*! \brief State rendered by the plugin browser window. */
struct PluginBrowserViewState
{
    /*! \brief Reports whether the browser window should be visible. */
    bool visible{false};

    /*! \brief Enables or disables catalog rescanning. */
    bool scan_enabled{false};

    /*! \brief Enables or disables adding a selected catalog candidate. */
    bool add_enabled{false};

    /*! \brief Scanned plugin candidates currently shown by the browser. */
    std::vector<PluginBrowserCandidateViewState> candidates;

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
