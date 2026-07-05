/*!
\file plugin_catalog_scan_progress.h
\brief Progress payload for plugin catalog discovery.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>

namespace rock_hero::common::audio
{

/*!
\brief Count-based progress for plugin catalog scanning after candidate paths are discovered.

The total is known only after the plugin format has resolved the scan path list. Callers should
treat that earlier filesystem walk as indeterminate and use this payload for the countable scanner
phase.
*/
struct [[nodiscard]] PluginCatalogScanProgress
{
    /*! \brief Number of plugin candidates whose scan step has completed. */
    std::size_t completed_plugins{};

    /*! \brief Total plugin candidates discovered before plugin scanning began. */
    std::size_t total_plugins{};

    /*! \brief Candidate currently being processed, or the next candidate after an update. */
    std::filesystem::path active_plugin_path{};

    /*!
    \brief Compares two progress payloads by their stored values.
    \param lhs Left-hand progress payload.
    \param rhs Right-hand progress payload.
    \return True when both progress payloads store equal values.
    */
    friend bool operator==(
        const PluginCatalogScanProgress& lhs, const PluginCatalogScanProgress& rhs) = default;
};

/*! \brief Receives plugin catalog scan progress from a worker-thread scan. */
using PluginCatalogScanProgressCallback =
    std::function<void(const PluginCatalogScanProgress& progress)>;

} // namespace rock_hero::common::audio
