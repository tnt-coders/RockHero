#include "plugin_catalog_workflow.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] common::audio::PluginCandidate makeCandidate(
    std::string id, std::string name, std::string manufacturer,
    std::string category = "Fx|Distortion")
{
    return common::audio::PluginCandidate{
        .id = std::move(id),
        .name = std::move(name),
        .manufacturer = std::move(manufacturer),
        .format_name = "VST3",
        .category = std::move(category),
        .file_path = std::filesystem::path{"plugin.vst3"},
    };
}

} // namespace

// Opening the browser stores a sorted catalog snapshot for presentation.
TEST_CASE("PluginCatalogWorkflow opens sorted catalog", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;

    workflow.open({
        makeCandidate("z", "Tremolo", "Tone Shop"),
        makeCandidate("b", "Amp", "Beta Audio"),
        makeCandidate("a", "Amp", "Alpha Audio"),
    });

    const PluginBrowserViewState state = workflow.viewState(true, true);
    CHECK(state.visible);
    CHECK(state.scan_enabled);
    CHECK(state.add_enabled);
    REQUIRE(state.plugins.size() == 3);
    CHECK(state.plugins[0].id == "a");
    CHECK(state.plugins[1].id == "b");
    CHECK(state.plugins[2].id == "z");
    CHECK(state.plugins[0].primary_display_type == PluginDisplayType::Distortion);
    CHECK(workflow.hasCandidates());
}

// Browser projection keeps ambiguous scanner categories in the Uncategorized filter bucket.
TEST_CASE("PluginCatalogWorkflow leaves ambiguous types uncategorized", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;

    workflow.open({makeCandidate("multi", "Multi FX", "Example Audio", "Fx|Delay|Reverb")});

    const PluginBrowserViewState state = workflow.viewState(true, true);
    REQUIRE(state.plugins.size() == 1);
    CHECK(state.plugins[0].primary_display_type == PluginDisplayType::Uncategorized);
}

// Closing the browser hides only presentation state and preserves the catalog.
TEST_CASE("PluginCatalogWorkflow closes without clearing catalog", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;
    workflow.open({makeCandidate("amp", "Amp", "Example Audio")});

    const bool closed = workflow.close();

    CHECK(closed);
    const PluginBrowserViewState state = workflow.viewState(false, true);
    CHECK_FALSE(state.visible);
    REQUIRE(state.plugins.size() == 1);
    CHECK(state.plugins[0].id == "amp");
    CHECK(workflow.hasCandidates());
    CHECK_FALSE(workflow.close());
}

// Replacing the catalog keeps the current browser visibility unchanged.
TEST_CASE("PluginCatalogWorkflow replaces catalog", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;
    workflow.open({makeCandidate("old", "Old Amp", "Example Audio")});

    workflow.replaceCatalog({makeCandidate("new", "New Amp", "Example Audio")});

    const PluginBrowserViewState state = workflow.viewState(true, true);
    CHECK(state.visible);
    REQUIRE(state.plugins.size() == 1);
    CHECK(state.plugins[0].id == "new");
}

// Candidate lookup returns a copy of the exact catalog entry selected by opaque ID.
TEST_CASE("PluginCatalogWorkflow selects candidate by ID", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;
    const common::audio::PluginCandidate expected =
        makeCandidate("selected", "Amp", "Example Audio");
    workflow.open({
        makeCandidate("other", "Cab", "Example Audio"),
        expected,
    });

    const std::optional<common::audio::PluginCandidate> selected =
        workflow.candidateForId("selected");

    CHECK(selected == std::optional{expected});
}

// Missing browser selections are ignored without mutating catalog or visibility state.
TEST_CASE("PluginCatalogWorkflow ignores missing selection", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;
    workflow.open({makeCandidate("amp", "Amp", "Example Audio")});

    const std::optional<common::audio::PluginCandidate> selected =
        workflow.candidateForId("missing");

    CHECK_FALSE(selected.has_value());
    const PluginBrowserViewState state = workflow.viewState(true, true);
    CHECK(state.visible);
    REQUIRE(state.plugins.size() == 1);
}

// Successful add flows can hide the browser while preserving the latest catalog snapshot.
TEST_CASE("PluginCatalogWorkflow hides after add", "[core][plugin-catalog]")
{
    PluginCatalogWorkflow workflow;
    workflow.open({makeCandidate("amp", "Amp", "Example Audio")});

    workflow.hide();

    const PluginBrowserViewState state = workflow.viewState(true, true);
    CHECK_FALSE(state.visible);
    REQUIRE(state.plugins.size() == 1);
    CHECK(state.plugins[0].id == "amp");
}

} // namespace rock_hero::editor::core
