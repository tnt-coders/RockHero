#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Test double that records plugin-host requests without loading real plugins.
class FakePluginHost final : public IPluginHost
{
public:
    // Records the scan path and returns the configured candidate list or scan failure.
    std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginFile(
        const std::filesystem::path& plugin_path) override
    {
        last_scan_path = plugin_path;
        ++scan_call_count;

        if (next_scan_error.has_value())
        {
            return std::unexpected{*next_scan_error};
        }

        return next_scan_candidates;
    }

    // Records the selected candidate ID and returns the configured handle or add failure.
    std::expected<PluginHandle, PluginHostError> addPlugin(const std::string& plugin_id) override
    {
        last_added_plugin_id = plugin_id;
        ++add_call_count;

        if (next_add_error.has_value())
        {
            return std::unexpected{*next_add_error};
        }

        return next_handle;
    }

    // Records the removed instance ID and returns the configured removal result.
    std::expected<void, PluginHostError> removePlugin(const std::string& instance_id) override
    {
        last_removed_instance_id = instance_id;
        ++remove_call_count;

        if (next_remove_error.has_value())
        {
            return std::unexpected{*next_remove_error};
        }

        return {};
    }

    // Candidate list returned by successful scans.
    std::vector<PluginCandidate> next_scan_candidates{
        PluginCandidate{
            .id = "vst3:amp",
            .name = "Amp",
            .manufacturer = "Rock Hero Tests",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"Amp.vst3"},
        },
    };

    // Optional error returned by the next scan request.
    std::optional<PluginHostError> next_scan_error{};

    // Handle returned by successful add requests.
    PluginHandle next_handle{
        .instance_id = "1",
        .plugin_id = "vst3:amp",
        .chain_index = 0,
    };

    // Optional error returned by the next add request.
    std::optional<PluginHostError> next_add_error{};

    // Optional error returned by the next remove request.
    std::optional<PluginHostError> next_remove_error{};

    // Last path passed to scanPluginFile().
    std::optional<std::filesystem::path> last_scan_path{};

    // Last candidate ID passed to addPlugin().
    std::optional<std::string> last_added_plugin_id{};

    // Last plugin instance ID passed to removePlugin().
    std::optional<std::string> last_removed_instance_id{};

    // Number of scan requests received.
    int scan_call_count{0};

    // Number of add requests received.
    int add_call_count{0};

    // Number of remove requests received.
    int remove_call_count{0};
};

} // namespace

// Verifies the plugin-host port can return project-owned plugin candidates.
TEST_CASE("IPluginHost scans plugin candidates", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;

    const auto candidates = plugin_host.scanPluginFile(std::filesystem::path{"Amp.vst3"});

    REQUIRE(candidates.has_value());
    REQUIRE(candidates->size() == 1);
    CHECK(candidates->front().id == "vst3:amp");
    CHECK(candidates->front().name == "Amp");
    CHECK(candidates->front().format_name == "VST3");
    CHECK(plugin_host.last_scan_path == std::optional{std::filesystem::path{"Amp.vst3"}});
    CHECK(plugin_host.scan_call_count == 1);
}

// Verifies selected plugin candidates are appended through an opaque returned instance handle.
TEST_CASE("IPluginHost adds a plugin", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;

    const auto handle = plugin_host.addPlugin("vst3:amp");

    REQUIRE(handle.has_value());
    CHECK(handle->instance_id == "1");
    CHECK(handle->plugin_id == "vst3:amp");
    CHECK(handle->chain_index == 0);
    CHECK(plugin_host.last_added_plugin_id == std::optional<std::string>{"vst3:amp"});
    CHECK(plugin_host.add_call_count == 1);
}

// Verifies plugin-host failures cross the port as typed errors.
TEST_CASE("IPluginHost add can fail with a typed error", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;
    plugin_host.next_add_error = PluginHostError{PluginHostErrorCode::PluginNotFound};

    const auto handle = plugin_host.addPlugin("missing");

    REQUIRE_FALSE(handle.has_value());
    CHECK(handle.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(plugin_host.last_added_plugin_id == std::optional<std::string>{"missing"});
    CHECK(plugin_host.add_call_count == 1);
}

// Verifies loaded plugin instances are removed through opaque instance IDs.
TEST_CASE("IPluginHost removes a plugin instance", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;

    const auto result = plugin_host.removePlugin("instance-1");

    REQUIRE(result.has_value());
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.remove_call_count == 1);
}

} // namespace rock_hero::common::audio
