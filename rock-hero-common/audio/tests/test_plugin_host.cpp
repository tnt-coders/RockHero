#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/audio/testing/recording_plugin_host.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

// Verifies default catalog scans refresh the host-owned known catalog.
TEST_CASE("IPluginHost refreshes default plugin catalog", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;

    const auto scan_result = plugin_host.scanPluginCatalog();
    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(scan_result.has_value());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "catalog-plugin-id");
    CHECK(plugin_host.last_scan_roots.empty());
    CHECK(plugin_host.catalog_scan_call_count == 1);
    CHECK(plugin_host.known_candidates_call_count == 1);
}

// Verifies catalog scans expose scanned plugin candidates without selecting a single file.
TEST_CASE("IPluginHost scans plugin catalog locations", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const std::vector<std::filesystem::path> roots{std::filesystem::path{"VST3"}};

    const auto candidates = plugin_host.scanPluginLocations(roots);

    REQUIRE(candidates.has_value());
    REQUIRE(candidates->size() == 1);
    CHECK(candidates->front().id == "catalog-plugin-id");
    CHECK(plugin_host.last_scan_roots == roots);
    CHECK(plugin_host.catalog_scan_call_count == 1);
}

// Verifies known candidates can be displayed without scanning plugin folders.
TEST_CASE("IPluginHost returns known plugin candidates", "[audio][plugin-host]")
{
    const testing::RecordingPluginHost plugin_host;

    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "catalog-plugin-id");
    CHECK(plugin_host.known_candidates_call_count == 1);
    CHECK(plugin_host.catalog_scan_call_count == 0);
}

// Verifies selected plugin candidates can be inserted at any visible chain slot.
TEST_CASE("IPluginHost inserts plugins at visible chain positions", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const PluginCandidate amp_candidate{
        .id = "vst3:amp",
        .name = "Amp",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Amp.vst3"},
    };
    const PluginCandidate cab_candidate{
        .id = "vst3:cab",
        .name = "Cab",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Cab.vst3"},
    };
    const PluginCandidate drive_candidate{
        .id = "vst3:drive",
        .name = "Drive",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Drive.vst3"},
    };

    plugin_host.next_instance_id = "amp-instance";
    const auto first_snapshot = plugin_host.insertPlugin(amp_candidate, 0);
    plugin_host.next_instance_id = "cab-instance";
    const auto append_snapshot = plugin_host.insertPlugin(cab_candidate, 1);
    plugin_host.next_instance_id = "drive-instance";
    const auto middle_snapshot = plugin_host.insertPlugin(drive_candidate, 1);

    REQUIRE(first_snapshot.has_value());
    REQUIRE(append_snapshot.has_value());
    REQUIRE(middle_snapshot.has_value());
    REQUIRE(middle_snapshot->plugins.size() == 3);
    CHECK(middle_snapshot->plugins[0].instance_id == "amp-instance");
    CHECK(middle_snapshot->plugins[0].chain_index == 0);
    CHECK(middle_snapshot->plugins[1].instance_id == "drive-instance");
    CHECK(middle_snapshot->plugins[1].plugin_id == "vst3:drive");
    CHECK(middle_snapshot->plugins[1].chain_index == 1);
    CHECK(middle_snapshot->plugins[2].instance_id == "cab-instance");
    CHECK(middle_snapshot->plugins[2].chain_index == 2);
    CHECK(plugin_host.last_inserted_plugin_candidate == std::optional{drive_candidate});
    CHECK(plugin_host.last_insert_index == std::optional<std::size_t>{1});
    CHECK(plugin_host.insert_call_count == 3);
}

// Verifies plugin-host failures cross the port as typed errors.
TEST_CASE("IPluginHost insert can fail with a typed error", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.next_insert_error = PluginHostError{PluginHostErrorCode::PluginNotFound};
    const PluginCandidate selected_candidate{
        .id = "missing",
        .name = "Missing",
        .manufacturer = {},
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, 0);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(plugin_host.last_inserted_plugin_candidate == std::optional{selected_candidate});
    CHECK(plugin_host.insert_call_count == 1);
}

// Verifies invalid insertion slots are rejected without changing the visible chain.
TEST_CASE("IPluginHost rejects invalid insert positions", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const PluginCandidate selected_candidate{
        .id = "vst3:amp",
        .name = "Amp",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, 1);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::InvalidChainIndex);
    CHECK(plugin_host.chain.empty());
}

// Verifies the hosted chain rejects insertions once the product plugin cap is reached.
TEST_CASE("IPluginHost rejects inserts at plugin limit", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    for (std::size_t index = 0; index < max_signal_chain_plugins; ++index)
    {
        plugin_host.chain.push_back(
            PluginChainEntry{
                .instance_id = "instance-" + std::to_string(index),
                .plugin_id = "plugin-" + std::to_string(index),
                .name = "Plugin " + std::to_string(index),
                .chain_index = index,
            });
    }
    const PluginCandidate selected_candidate{
        .id = "vst3:extra",
        .name = "Extra",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, max_signal_chain_plugins);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::PluginChainLimitExceeded);
    CHECK(plugin_host.chain.size() == max_signal_chain_plugins);
}

// Verifies loaded plugin instances can move up, down, and no-op in the visible chain.
TEST_CASE("IPluginHost moves plugin instances", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
        PluginChainEntry{
            .instance_id = "drive-instance",
            .plugin_id = "drive",
            .name = "Drive",
            .chain_index = 1,
        },
        PluginChainEntry{
            .instance_id = "cab-instance",
            .plugin_id = "cab",
            .name = "Cab",
            .chain_index = 2,
        },
    };

    const auto move_down_snapshot = plugin_host.movePlugin("amp-instance", 2);
    const auto move_up_snapshot = plugin_host.movePlugin("cab-instance", 0);
    const auto noop_snapshot = plugin_host.movePlugin("cab-instance", 0);

    REQUIRE(move_down_snapshot.has_value());
    REQUIRE(move_up_snapshot.has_value());
    REQUIRE(noop_snapshot.has_value());
    REQUIRE(noop_snapshot->plugins.size() == 3);
    CHECK(noop_snapshot->plugins[0].instance_id == "cab-instance");
    CHECK(noop_snapshot->plugins[0].chain_index == 0);
    CHECK(noop_snapshot->plugins[1].instance_id == "drive-instance");
    CHECK(noop_snapshot->plugins[1].chain_index == 1);
    CHECK(noop_snapshot->plugins[2].instance_id == "amp-instance");
    CHECK(noop_snapshot->plugins[2].chain_index == 2);
    CHECK(plugin_host.last_moved_instance_id == std::optional<std::string>{"cab-instance"});
    CHECK(plugin_host.last_move_destination_index == std::optional<std::size_t>{0});
    CHECK(plugin_host.move_call_count == 3);
}

// Verifies invalid move requests return typed errors before changing the visible chain.
TEST_CASE("IPluginHost rejects invalid plugin moves", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
    };

    const auto missing = plugin_host.movePlugin("missing-instance", 0);
    const auto invalid_index = plugin_host.movePlugin("amp-instance", 1);

    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == PluginHostErrorCode::PluginInstanceNotFound);
    REQUIRE_FALSE(invalid_index.has_value());
    CHECK(invalid_index.error().code == PluginHostErrorCode::InvalidChainIndex);
    REQUIRE(plugin_host.chain.size() == 1);
    CHECK(plugin_host.chain[0].instance_id == "amp-instance");
}

// Verifies loaded plugin instances are removed through opaque instance IDs.
TEST_CASE("IPluginHost removes a plugin instance", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "instance-1",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
        PluginChainEntry{
            .instance_id = "instance-2",
            .plugin_id = "cab",
            .name = "Cab",
            .chain_index = 1,
        },
    };

    const auto result = plugin_host.removePlugin("instance-1");

    REQUIRE(result.has_value());
    REQUIRE(result->plugins.size() == 1);
    CHECK(result->plugins[0].instance_id == "instance-2");
    CHECK(result->plugins[0].chain_index == 0);
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.remove_call_count == 1);
}

// Verifies loaded plugin instances expose a message-thread window operation through the port.
TEST_CASE("IPluginHost opens a plugin window", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;

    const auto result = plugin_host.openPluginWindow("instance-1");

    REQUIRE(result.has_value());
    CHECK(plugin_host.last_opened_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.open_call_count == 1);
}

} // namespace rock_hero::common::audio
