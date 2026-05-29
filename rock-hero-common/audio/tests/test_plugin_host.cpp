#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <optional>
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

// Verifies selected plugin candidates are appended through an opaque returned instance handle.
TEST_CASE("IPluginHost adds a plugin", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const PluginCandidate selected_candidate{
        .id = "vst3:amp",
        .name = "Amp",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Amp.vst3"},
    };

    const auto handle = plugin_host.addPlugin(selected_candidate);

    REQUIRE(handle.has_value());
    CHECK(handle->instance_id == "instance-id");
    CHECK(handle->plugin_id == "vst3:amp");
    CHECK(handle->chain_index == 0);
    CHECK(plugin_host.last_added_plugin_candidate == std::optional{selected_candidate});
    CHECK(plugin_host.add_call_count == 1);
}

// Verifies plugin-host failures cross the port as typed errors.
TEST_CASE("IPluginHost add can fail with a typed error", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.next_add_error = PluginHostError{PluginHostErrorCode::PluginNotFound};
    const PluginCandidate selected_candidate{
        .id = "missing",
        .name = "Missing",
        .manufacturer = {},
        .format_name = "VST3",
        .file_path = {},
    };

    const auto handle = plugin_host.addPlugin(selected_candidate);

    REQUIRE_FALSE(handle.has_value());
    CHECK(handle.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(plugin_host.last_added_plugin_candidate == std::optional{selected_candidate});
    CHECK(plugin_host.add_call_count == 1);
}

// Verifies loaded plugin instances are removed through opaque instance IDs.
TEST_CASE("IPluginHost removes a plugin instance", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;

    const auto result = plugin_host.removePlugin("instance-1");

    REQUIRE(result.has_value());
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
