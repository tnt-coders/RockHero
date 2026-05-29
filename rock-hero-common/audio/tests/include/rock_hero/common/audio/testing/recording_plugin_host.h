/*!
\file recording_plugin_host.h
\brief Recording plugin-host test implementation.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*!
\brief IPluginHost implementation that records catalog, chain, and window operations.

Use this when tests need plugin-host behavior without scanning plugin folders or loading real
plugins. The fake keeps catalog data configurable while preserving the production port's typed
success and error channels.
*/
class RecordingPluginHost final : public IPluginHost
{
public:
    /*! \brief Records a default catalog scan and refreshes the known catalog on success. */
    [[nodiscard]] std::expected<void, PluginHostError> scanPluginCatalog() override
    {
        catalog_scan_call_count += 1;
        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        next_known_candidates = next_catalog_candidates;
        return {};
    }

    /*! \brief Records explicit scan roots and returns the configured catalog candidates. */
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginLocations(
        const std::vector<std::filesystem::path>& roots) override
    {
        last_scan_roots = roots;
        catalog_scan_call_count += 1;
        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        return next_catalog_candidates;
    }

    /*! \brief Returns the configured known catalog without simulating an expensive scan. */
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const override
    {
        known_candidates_call_count += 1;
        return next_known_candidates;
    }

    /*! \brief Records a plugin insertion request and returns the configured handle. */
    [[nodiscard]] std::expected<PluginHandle, PluginHostError> addPlugin(
        const PluginCandidate& plugin_candidate) override
    {
        last_added_plugin_candidate = plugin_candidate;
        add_call_count += 1;
        if (next_add_error.has_value())
        {
            return std::unexpected{*next_add_error};
        }

        PluginHandle handle = next_handle;
        handle.plugin_id = plugin_candidate.id;
        return handle;
    }

    /*! \brief Records a plugin removal request and returns the configured outcome. */
    [[nodiscard]] std::expected<void, PluginHostError> removePlugin(
        const std::string& instance_id) override
    {
        last_removed_instance_id = instance_id;
        remove_call_count += 1;
        if (next_remove_error.has_value())
        {
            return std::unexpected{*next_remove_error};
        }

        return {};
    }

    /*! \brief Records a plugin editor-window request and returns the configured outcome. */
    [[nodiscard]] std::expected<void, PluginHostError> openPluginWindow(
        const std::string& instance_id) override
    {
        last_opened_instance_id = instance_id;
        open_call_count += 1;
        if (next_open_error.has_value())
        {
            return std::unexpected{*next_open_error};
        }

        return {};
    }

    /*! \brief Candidates returned by the next successful catalog scan. */
    std::vector<PluginCandidate> next_catalog_candidates{
        PluginCandidate{
            .id = "catalog-plugin-id",
            .name = "Catalog Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"catalog-amp.vst3"},
        },
    };

    /*! \brief Candidates returned by the lightweight known-catalog read. */
    std::vector<PluginCandidate> next_known_candidates{
        PluginCandidate{
            .id = "catalog-plugin-id",
            .name = "Catalog Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"catalog-amp.vst3"},
        },
    };

    /*! \brief Handle returned by the next successful plugin insertion. */
    PluginHandle next_handle{
        .instance_id = "instance-id",
        .plugin_id = {},
        .chain_index = 0,
    };

    /*! \brief Optional catalog scan error returned instead of success. */
    std::optional<PluginHostError> next_catalog_scan_error{};

    /*! \brief Optional insertion error returned instead of a handle. */
    std::optional<PluginHostError> next_add_error{};

    /*! \brief Optional removal error returned instead of success. */
    std::optional<PluginHostError> next_remove_error{};

    /*! \brief Optional open-window error returned instead of success. */
    std::optional<PluginHostError> next_open_error{};

    /*! \brief Last roots passed to scanPluginLocations(). */
    std::vector<std::filesystem::path> last_scan_roots{};

    /*! \brief Last candidate passed to addPlugin(). */
    std::optional<PluginCandidate> last_added_plugin_candidate{};

    /*! \brief Last instance ID passed to removePlugin(). */
    std::optional<std::string> last_removed_instance_id{};

    /*! \brief Last instance ID passed to openPluginWindow(). */
    std::optional<std::string> last_opened_instance_id{};

    /*! \brief Number of catalog scan calls received. */
    int catalog_scan_call_count{0};

    /*! \brief Number of known-catalog reads received. */
    mutable int known_candidates_call_count{0};

    /*! \brief Number of insertion calls received. */
    int add_call_count{0};

    /*! \brief Number of removal calls received. */
    int remove_call_count{0};

    /*! \brief Number of open-window calls received. */
    int open_call_count{0};
};

} // namespace rock_hero::common::audio::testing
