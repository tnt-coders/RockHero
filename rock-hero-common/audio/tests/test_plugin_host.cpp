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
    // Records a default catalog scan and refreshes the known catalog or returns failure.
    std::expected<void, PluginHostError> scanPluginCatalog() override
    {
        ++catalog_scan_call_count;

        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        next_known_candidates = next_catalog_candidates;
        return {};
    }

    // Records catalog roots and returns the configured catalog candidates or failure.
    std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginLocations(
        const std::vector<std::filesystem::path>& roots) override
    {
        last_scan_roots = roots;
        ++catalog_scan_call_count;

        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        return next_catalog_candidates;
    }

    // Returns the configured known catalog without simulating a plugin scan.
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const override
    {
        ++known_candidates_call_count;
        return next_known_candidates;
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

    // Records the opened instance ID and returns the configured open result.
    std::expected<void, PluginHostError> openPluginWindow(const std::string& instance_id) override
    {
        last_opened_instance_id = instance_id;
        ++open_call_count;

        if (next_open_error.has_value())
        {
            return std::unexpected{*next_open_error};
        }

        return {};
    }

    // Candidate list returned by successful catalog scans.
    std::vector<PluginCandidate> next_catalog_candidates{
        PluginCandidate{
            .id = "vst3:catalog-amp",
            .name = "Catalog Amp",
            .manufacturer = "Rock Hero Tests",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"CatalogAmp.vst3"},
        },
    };

    // Candidate list returned by the lightweight known-catalog read.
    std::vector<PluginCandidate> next_known_candidates{
        PluginCandidate{
            .id = "vst3:known-amp",
            .name = "Known Amp",
            .manufacturer = "Rock Hero Tests",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"KnownAmp.vst3"},
        },
    };

    // Optional error returned by the next catalog scan request.
    std::optional<PluginHostError> next_catalog_scan_error{};

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

    // Optional error returned by the next open request.
    std::optional<PluginHostError> next_open_error{};

    // Last roots passed to scanPluginLocations().
    std::vector<std::filesystem::path> last_scan_roots{};

    // Last candidate ID passed to addPlugin().
    std::optional<std::string> last_added_plugin_id{};

    // Last plugin instance ID passed to removePlugin().
    std::optional<std::string> last_removed_instance_id{};

    // Last plugin instance ID passed to openPluginWindow().
    std::optional<std::string> last_opened_instance_id{};

    // Number of catalog scan requests received.
    int catalog_scan_call_count{0};

    // Number of known-catalog reads received.
    mutable int known_candidates_call_count{0};

    // Number of add requests received.
    int add_call_count{0};

    // Number of remove requests received.
    int remove_call_count{0};

    // Number of open-window requests received.
    int open_call_count{0};
};

} // namespace

// Verifies default catalog scans refresh the host-owned known catalog.
TEST_CASE("IPluginHost refreshes default plugin catalog", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;

    const auto scan_result = plugin_host.scanPluginCatalog();
    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(scan_result.has_value());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "vst3:catalog-amp");
    CHECK(plugin_host.last_scan_roots.empty());
    CHECK(plugin_host.catalog_scan_call_count == 1);
    CHECK(plugin_host.known_candidates_call_count == 1);
}

// Verifies catalog scans expose scanned plugin candidates without selecting a single file.
TEST_CASE("IPluginHost scans plugin catalog locations", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;
    const std::vector<std::filesystem::path> roots{std::filesystem::path{"VST3"}};

    const auto candidates = plugin_host.scanPluginLocations(roots);

    REQUIRE(candidates.has_value());
    REQUIRE(candidates->size() == 1);
    CHECK(candidates->front().id == "vst3:catalog-amp");
    CHECK(plugin_host.last_scan_roots == roots);
    CHECK(plugin_host.catalog_scan_call_count == 1);
}

// Verifies known candidates can be displayed without scanning plugin folders.
TEST_CASE("IPluginHost returns known plugin candidates", "[audio][plugin-host]")
{
    const FakePluginHost plugin_host;

    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "vst3:known-amp");
    CHECK(plugin_host.known_candidates_call_count == 1);
    CHECK(plugin_host.catalog_scan_call_count == 0);
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

// Verifies loaded plugin instances expose a message-thread window operation through the port.
TEST_CASE("IPluginHost opens a plugin window", "[audio][plugin-host]")
{
    FakePluginHost plugin_host;

    const auto result = plugin_host.openPluginWindow("instance-1");

    REQUIRE(result.has_value());
    CHECK(plugin_host.last_opened_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.open_call_count == 1);
}

} // namespace rock_hero::common::audio
