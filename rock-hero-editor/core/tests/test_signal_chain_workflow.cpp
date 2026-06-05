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

// Verifies the authored block placement round-trips through the snapshot and the view report.
TEST_CASE("SignalChainWorkflow carries authored block placement", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;

    common::audio::PluginChainEntry first = makeEntry("first", 0);
    first.block_index = 1;
    common::audio::PluginChainEntry second = makeEntry("second", 1);
    second.block_index = 4;
    workflow.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = {first, second}});

    // A loaded snapshot's authored blocks reach the view rows and the capture vector.
    REQUIRE(workflow.plugins().size() == 2);
    CHECK(workflow.plugins()[0].block_index == 1);
    CHECK(workflow.plugins()[1].block_index == 4);
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{1, 4});

    // A later authoritative snapshot with the same chain can still restore different blocks.
    first.block_index = 2;
    second.block_index = 5;
    workflow.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = {first, second}});
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{2, 5});

    // A view-reported placement overrides the stored blocks for the next capture.
    CHECK(workflow.setBlockPlacement({3, 6}));
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{3, 6});

    // A stale report whose size no longer matches the chain is ignored.
    CHECK_FALSE(workflow.setBlockPlacement({7}));
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{3, 6});
}

// Verifies the editor owns placement validity: an opaque, invalid snapshot placement compacts.
TEST_CASE("SignalChainWorkflow compacts an invalid block placement", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;

    common::audio::PluginChainEntry first = makeEntry("first", 0);
    first.block_index = 3;
    common::audio::PluginChainEntry second = makeEntry("second", 1);
    second.block_index = 3;
    workflow.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = {first, second}});

    // Duplicate blocks are not a valid layout, so the workflow falls back to a gapless one.
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{0, 1});
}

// Verifies malformed view reports are normalized before they can be captured.
TEST_CASE("SignalChainWorkflow compacts invalid reported placement", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("second", 1)},
        });

    CHECK_FALSE(workflow.setBlockPlacement({3, 3}));
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{0, 1});

    CHECK(workflow.setBlockPlacement({2, 5}));
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{2, 5});

    CHECK(workflow.setBlockPlacement({0, common::audio::max_signal_chain_plugins}));
    CHECK(workflow.blockIndices() == std::vector<std::size_t>{0, 1});
}

// Verifies runtime removal snapshots preserve authored block gaps for surviving instances.
TEST_CASE("SignalChainWorkflow preserves blocks when plugins are removed", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;

    common::audio::PluginChainEntry first = makeEntry("first", 0);
    first.block_index = 2;
    common::audio::PluginChainEntry second = makeEntry("second", 1);
    second.block_index = 4;
    common::audio::PluginChainEntry third = makeEntry("third", 2);
    third.block_index = 7;
    workflow.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = {first, second, third}});

    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("third", 1)},
        });

    CHECK(workflow.blockIndices() == std::vector<std::size_t>{2, 7});

    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("third", 0)},
        });

    CHECK(workflow.blockIndices() == std::vector<std::size_t>{7});
}

// Verifies an insertion snapshot places the new plugin at the chosen block, keeping survivor gaps.
TEST_CASE("SignalChainWorkflow inserts a plugin at the chosen block", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;

    common::audio::PluginChainEntry first = makeEntry("first", 0);
    first.block_index = 0;
    common::audio::PluginChainEntry second = makeEntry("second", 1);
    second.block_index = 4;
    workflow.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = {first, second}});

    // Request an insert at chain slot 1 / visual block 2 (an empty gap between the two plugins).
    REQUIRE(workflow.requestInsertAt(1));
    workflow.setPendingInsertBlock(2);

    // The backend insertion snapshot carries default blocks; the new plugin must land on block 2
    // while the survivors keep blocks 0 and 4.
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("inserted", 1), makeEntry("second", 2)},
        });

    CHECK(workflow.blockIndices() == std::vector<std::size_t>{0, 2, 4});
}

// Verifies an append insertion (no chosen block) falls back to a gapless layout.
TEST_CASE("SignalChainWorkflow appends without a chosen block", "[core][signal-chain]")
{
    SignalChainWorkflow workflow;
    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("second", 1)},
        });

    workflow.requestAppend();

    workflow.replaceSnapshot(
        common::audio::PluginChainSnapshot{
            .plugins = {makeEntry("first", 0), makeEntry("second", 1), makeEntry("third", 2)},
        });

    CHECK(workflow.blockIndices() == std::vector<std::size_t>{0, 1, 2});
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
