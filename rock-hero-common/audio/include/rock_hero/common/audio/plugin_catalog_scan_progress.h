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
\brief Count-based progress for metadata work after plugin catalog paths are discovered.

The total is known only after the catalog roots have been walked and duplicate VST3 paths have
been removed. Callers should treat the earlier filesystem walk as indeterminate and use this
payload for the countable metadata/cache phase.
*/
struct [[nodiscard]] PluginCatalogScanProgress
{
    /*! \brief Number of plugin candidates whose metadata processing has completed. */
    std::size_t completed_plugins{};

    /*! \brief Total plugin candidates discovered before metadata processing began. */
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
