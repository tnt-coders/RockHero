/*!
\file plugin_catalog_workflow.h
\brief Headless workflow state for plugin-browser catalog presentation.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/editor/core/plugin_browser_view_state.h>
#include <rock_hero/editor/core/plugin_display_type.h>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Owns plugin-browser catalog state without touching the plugin host.

The root controller supplies candidates already read from the plugin-host boundary and executes
all scanning, loading, signal-chain mutation, and error reporting. This workflow only keeps the
sorted browser catalog, browser visibility, candidate lookup, and view-state projection.
*/
class PluginCatalogWorkflow final
{
public:
    /*! \brief Creates empty plugin catalog workflow state. */
    PluginCatalogWorkflow() = default;

    /*!
    \brief Opens the browser over a fresh known-catalog snapshot.
    \param candidates Candidates already read from the plugin-host boundary.
    */
    void open(std::vector<common::audio::PluginCandidate> candidates);

    /*!
    \brief Replaces the known catalog without changing browser visibility.
    \param candidates Candidates already read from the plugin-host boundary.
    */
    void replaceCatalog(std::vector<common::audio::PluginCandidate> candidates);

    /*!
    \brief Closes the browser without discarding the current catalog.
    \return True when the browser was visible and is now closed.
    */
    [[nodiscard]] bool close() noexcept;

    /*! \brief Hides the browser without reporting whether visibility changed. */
    void hide() noexcept;

    /*!
    \brief Reports whether any catalog candidates are available for selection.
    \return True when the current catalog contains at least one candidate.
    */
    [[nodiscard]] bool hasCandidates() const noexcept;

    /*!
    \brief Finds a candidate by the opaque browser selection ID.
    \param plugin_id Opaque candidate ID emitted by the plugin browser.
    \return Matching candidate, or empty when the catalog no longer contains that ID.
    */
    [[nodiscard]] std::optional<common::audio::PluginCandidate> candidateForId(
        std::string_view plugin_id) const;

    /*!
    \brief Builds plugin-browser view state from catalog state and controller action gates.
    \param scan_enabled Controller-computed scan action availability.
    \param add_enabled Controller-computed add action availability.
    \return Plugin-browser view state for the current catalog and visibility state.
    */
    [[nodiscard]] PluginBrowserViewState viewState(bool scan_enabled, bool add_enabled) const;

private:
    std::vector<common::audio::PluginCandidate> m_candidates;
    bool m_visible{false};
};

} // namespace rock_hero::editor::core
