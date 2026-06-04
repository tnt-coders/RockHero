#include "signal_chain_workflow.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/audio/plugin_chain_snapshot.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Builds a compact chain entry with only fields needed by workflow tests.
[[nodiscard]] common::audio::PluginChainEntry makeEntry(
    std::string instance_id, std::size_t chain_index)
{
    return common::audio::PluginChainEntry{
        .instance_id = std::move(instance_id),
        .plugin_id = "plugin-" + std::to_string(chain_index),
        .name = "Plugin " + std::to_string(chain_index),
        .manufacturer = "Tests",
        .format_name = "VST3",
        .chain_index = chain_index,
    };
}

// Builds a contiguous plugin chain with stable IDs.
[[nodiscard]] std::vector<common::audio::PluginChainEntry> makeEntries(std::size_t count)
{
    std::vector<common::audio::PluginChainEntry> plugins;
    plugins.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        plugins.push_back(makeEntry("plugin-" + std::to_string(index), index));
    }
    return plugins;
}

} // namespace

// Verifies backend snapshots replace the chain order without local reindexing.
TEST_CASE("SignalChainWorkflow projects authoritative snapshots", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;

    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 4), makeEntry("second", 2)},
        });

    REQUIRE(workflow.plugins().size() == 2);
    CHECK(workflow.plugins()[0].instance_id == "first");
    CHECK(workflow.plugins()[0].chain_index == 4);
    CHECK(workflow.plugins()[1].instance_id == "second");
    CHECK(workflow.plugins()[1].chain_index == 2);
    CHECK(workflow.hasPlugins());
    CHECK(workflow.appendIndex() == 2);
}

// Verifies browser insertion state uses append by default and rejects stale slots.
TEST_CASE("SignalChainWorkflow stores pending insertion slots", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("second", 1)},
        });

    CHECK(workflow.insertionIndexForSelection() == std::optional<std::size_t>{2});
    CHECK(workflow.requestInsertAt(1));
    CHECK(workflow.insertionIndexForSelection() == std::optional<std::size_t>{1});
    CHECK_FALSE(workflow.requestInsertAt(3));
    CHECK(workflow.insertionIndexForSelection() == std::optional<std::size_t>{1});

    workflow.requestAppend();

    CHECK(workflow.insertionIndexForSelection() == std::optional<std::size_t>{2});
}

// Verifies the product plugin cap blocks new browser insertion state.
TEST_CASE("SignalChainWorkflow rejects insertion at capacity", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = makeEntries(common::audio::max_signal_chain_plugins),
        });

    CHECK_FALSE(workflow.hasInsertCapacity());
    CHECK_FALSE(workflow.requestInsertAt(0));
    CHECK_FALSE(workflow.insertionIndexForSelection().has_value());

    workflow.requestAppend();

    CHECK_FALSE(workflow.insertionIndexForSelection().has_value());
}

// Verifies a full authoritative snapshot drops pending browser insertion state.
TEST_CASE("SignalChainWorkflow clears pending insertion at capacity", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = makeEntries(common::audio::max_signal_chain_plugins - 1),
        });
    REQUIRE(workflow.requestInsertAt(1));

    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = makeEntries(common::audio::max_signal_chain_plugins),
        });
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = makeEntries(common::audio::max_signal_chain_plugins - 1),
        });

    CHECK(
        workflow.insertionIndexForSelection() ==
        std::optional<std::size_t>{common::audio::max_signal_chain_plugins - 1});
}

// Verifies stale row requests are rejected before controller code calls the backend.
TEST_CASE("SignalChainWorkflow validates plugin instance IDs", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("second", 1)},
        });

    CHECK(workflow.containsInstance("first"));
    CHECK_FALSE(workflow.containsInstance("missing"));
    CHECK(workflow.chainIndexForInstance("second") == std::optional<std::size_t>{1});
    CHECK_FALSE(workflow.chainIndexForInstance("missing").has_value());
}

// Verifies clearing removes chain rows and any pending browser insertion target.
TEST_CASE("SignalChainWorkflow clears chain editing state", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0)},
        });
    REQUIRE(workflow.requestInsertAt(1));

    workflow.clear();

    CHECK_FALSE(workflow.hasPlugins());
    CHECK(workflow.plugins().empty());
    CHECK(workflow.insertionIndexForSelection() == std::optional<std::size_t>{0});
}

} // namespace rock_hero::editor::core
