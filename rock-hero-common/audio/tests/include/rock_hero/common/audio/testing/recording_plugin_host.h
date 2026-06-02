/*!
\file recording_plugin_host.h
\brief Recording plugin-host test implementation.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <string>
#include <utility>
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

    /*! \brief Records a plugin insertion request and mutates the in-memory chain. */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> insertPlugin(
        const PluginCandidate& plugin_candidate, std::size_t chain_index) override
    {
        last_inserted_plugin_candidate = plugin_candidate;
        last_insert_index = chain_index;
        insert_call_count += 1;
        if (next_insert_error.has_value())
        {
            return std::unexpected{*next_insert_error};
        }

        if (chain_index > chain.size())
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        PluginChainEntry entry{
            .instance_id = next_instance_id,
            .plugin_id = plugin_candidate.id,
            .name = plugin_candidate.name,
            .manufacturer = plugin_candidate.manufacturer,
            .format_name = plugin_candidate.format_name,
            .chain_index = chain_index,
        };
        chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(chain_index), std::move(entry));
        reindexChain();
        return snapshot();
    }

    /*! \brief Records a plugin move request and mutates the in-memory chain. */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
        const std::string& instance_id, std::size_t destination_index) override
    {
        last_moved_instance_id = instance_id;
        last_move_destination_index = destination_index;
        move_call_count += 1;
        if (next_move_error.has_value())
        {
            return std::unexpected{*next_move_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        if (destination_index >= chain.size())
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        const auto current_index = static_cast<std::size_t>(std::distance(chain.begin(), plugin));
        if (current_index == destination_index)
        {
            return snapshot();
        }

        PluginChainEntry entry = std::move(*plugin);
        chain.erase(plugin);
        chain.insert(
            chain.begin() + static_cast<std::ptrdiff_t>(destination_index), std::move(entry));
        reindexChain();
        return snapshot();
    }

    /*! \brief Records a plugin removal request and mutates the in-memory chain. */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
        const std::string& instance_id) override
    {
        last_removed_instance_id = instance_id;
        remove_call_count += 1;
        if (next_remove_error.has_value())
        {
            return std::unexpected{*next_remove_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        chain.erase(plugin);
        reindexChain();
        return snapshot();
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

    /*! \brief Current ordered in-memory plugin chain. */
    std::vector<PluginChainEntry> chain{};

    /*! \brief Instance ID assigned to the next successful insertion. */
    std::string next_instance_id{"instance-id"};

    /*! \brief Optional catalog scan error returned instead of success. */
    std::optional<PluginHostError> next_catalog_scan_error{};

    /*! \brief Optional insertion error returned instead of a handle. */
    std::optional<PluginHostError> next_insert_error{};

    /*! \brief Optional move error returned instead of a snapshot. */
    std::optional<PluginHostError> next_move_error{};

    /*! \brief Optional removal error returned instead of success. */
    std::optional<PluginHostError> next_remove_error{};

    /*! \brief Optional open-window error returned instead of success. */
    std::optional<PluginHostError> next_open_error{};

    /*! \brief Last roots passed to scanPluginLocations(). */
    std::vector<std::filesystem::path> last_scan_roots{};

    /*! \brief Last candidate passed to insertPlugin(). */
    std::optional<PluginCandidate> last_inserted_plugin_candidate{};

    /*! \brief Last chain index passed to insertPlugin(). */
    std::optional<std::size_t> last_insert_index{};

    /*! \brief Last instance ID passed to movePlugin(). */
    std::optional<std::string> last_moved_instance_id{};

    /*! \brief Last destination index passed to movePlugin(). */
    std::optional<std::size_t> last_move_destination_index{};

    /*! \brief Last instance ID passed to removePlugin(). */
    std::optional<std::string> last_removed_instance_id{};

    /*! \brief Last instance ID passed to openPluginWindow(). */
    std::optional<std::string> last_opened_instance_id{};

    /*! \brief Number of catalog scan calls received. */
    int catalog_scan_call_count{0};

    /*! \brief Number of known-catalog reads received. */
    mutable int known_candidates_call_count{0};

    /*! \brief Number of insertion calls received. */
    int insert_call_count{0};

    /*! \brief Number of move calls received. */
    int move_call_count{0};

    /*! \brief Number of removal calls received. */
    int remove_call_count{0};

    /*! \brief Number of open-window calls received. */
    int open_call_count{0};

private:
    // Reassigns contiguous chain indices after a successful structural mutation.
    void reindexChain()
    {
        for (std::size_t index = 0; index < chain.size(); ++index)
        {
            chain[index].chain_index = index;
        }
    }

    // Returns an authoritative snapshot of the current fake chain.
    [[nodiscard]] PluginChainSnapshot snapshot() const
    {
        return PluginChainSnapshot{.plugins = chain};
    }

    // Finds one fake chain entry by runtime instance ID.
    [[nodiscard]] std::vector<PluginChainEntry>::iterator findPlugin(const std::string& instance_id)
    {
        return std::ranges::find_if(chain, [&instance_id](const PluginChainEntry& plugin) {
            return plugin.instance_id == instance_id;
        });
    }
};

} // namespace rock_hero::common::audio::testing
