#include "signal_chain_block_layout.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a minimal plugin state; layout logic depends only on stable instance identity.
[[nodiscard]] core::PluginViewState makePlugin(std::string instance_id, std::size_t chain_index)
{
    return core::PluginViewState{
        .instance_id = std::move(instance_id),
        .plugin_id = "plugin-" + std::to_string(chain_index),
        .name = "Plugin " + std::to_string(chain_index),
        .manufacturer = "Maker",
        .format_name = "VST3",
        .chain_index = chain_index,
    };
}

// Builds a compact plugin list whose IDs make expected order easy to read in tests.
[[nodiscard]] std::vector<core::PluginViewState> makePlugins(std::vector<std::string> ids)
{
    std::vector<core::PluginViewState> plugins;
    plugins.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        plugins.push_back(makePlugin(std::move(ids[index]), index));
    }

    return plugins;
}

// Builds a valid placement from explicit block assignments or fails the test setup loudly.
[[nodiscard]] BlockPlacement placementOf(std::vector<std::size_t> blocks, std::size_t block_count)
{
    std::optional<BlockPlacement> built =
        BlockPlacement::fromIndices(std::move(blocks), block_count);
    REQUIRE(built.has_value());
    return *built;
}

} // namespace

// Verifies the configured minimum block count remains visible for shorter chains.
TEST_CASE("Block layout keeps configured fixed block count", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "cab"}));

    CHECK(layout.blockCount() == 8);
    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{0, 1});
}

// Verifies pointer side maps to the occupied-block push direction used by drag entry latching.
TEST_CASE("Block layout maps pointer side to push direction", "[ui][signal-chain-layout]")
{
    CHECK(SignalChainBlockLayout::pushDirectionForLocalX(0, 100) == BlockPushDirection::Right);
    CHECK(SignalChainBlockLayout::pushDirectionForLocalX(49, 100) == BlockPushDirection::Right);
    CHECK(SignalChainBlockLayout::pushDirectionForLocalX(50, 100) == BlockPushDirection::Left);
    CHECK(SignalChainBlockLayout::pushDirectionForLocalX(99, 100) == BlockPushDirection::Left);
}

// Verifies metadata refreshes keep visual gaps while real reorder refreshes compact them.
TEST_CASE("Block layout preserves gaps on metadata refresh", "[ui][signal-chain-layout]")
{
    const std::vector<core::PluginViewState> previous_plugins = makePlugins({"amp", "cab"});
    std::vector<core::PluginViewState> renamed_plugins = previous_plugins;
    renamed_plugins[0].name = "Renamed Amp";

    SignalChainBlockLayout layout{8};
    layout.applyPlugins(previous_plugins);
    REQUIRE(layout.applyPlacement(placementOf({1, 4}, 8)));
    layout.applyPlugins(renamed_plugins);

    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{1, 4});

    layout.applyPlugins(makePlugins({"cab", "amp"}));

    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{0, 1});
}

// Verifies the selected empty block is preserved when the inserted plugin reaches state.
TEST_CASE("Block layout maps pending inserts to empty blocks", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "cab"}));
    REQUIRE(layout.applyPlacement(placementOf({0, 4}, 8)));

    const std::optional<std::size_t> chain_index = layout.beginInsertAtBlock(2);
    REQUIRE(chain_index.has_value());
    CHECK(*chain_index == 1);

    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{0, 2, 4});

    CHECK_FALSE(layout.beginInsertAtBlock(4).has_value());
}

// Verifies committed previews are remapped from previous order into backend-confirmed order.
TEST_CASE("Block layout maps committed previews to backend order", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));

    const SignalChainBlockLayout::DropCompletion completion = layout.completeDrop(
        0,
        SignalChainBlockLayout::DropIntent{
            .destination_index = 2,
            .placement = placementOf({4, 1, 2}, 8),
        });
    CHECK(completion.move_destination_index == std::optional<std::size_t>{2});

    layout.applyPlugins(makePlugins({"drive", "cab", "amp"}));
    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{1, 2, 4});
}

// Verifies occupied-block drops use the requested push direction to compute final placement.
TEST_CASE("Block layout drops onto occupied blocks by side", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{4};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    REQUIRE(layout.applyPlacement(placementOf({0, 2, 3}, 4)));

    const std::optional<SignalChainBlockLayout::DropIntent> intent =
        layout.dropIntent(0, 2, BlockPushDirection::Left);

    REQUIRE(intent.has_value());
    CHECK(intent->destination_index == 1);
    CHECK(intent->placement.blocks() == std::vector<std::size_t>{2, 1, 3});
}

// Verifies blocked sides reject drops instead of inventing a compacted placement.
TEST_CASE("Block layout rejects blocked occupied sides", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{4};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    REQUIRE(layout.applyPlacement(placementOf({0, 1, 3}, 4)));

    CHECK_FALSE(layout.dropIntent(2, 1, BlockPushDirection::Left).has_value());
    CHECK(layout.canReceiveDrop(2, 1));
}

// Verifies empty blocks can hold a visual gap while preserving the implied linear order.
TEST_CASE("Block layout drops into empty fixed blocks", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));

    const std::optional<SignalChainBlockLayout::DropIntent> intent =
        layout.dropIntent(2, 5, BlockPushDirection::Right);

    REQUIRE(intent.has_value());
    CHECK(intent->destination_index == 2);
    CHECK(intent->placement.blocks() == std::vector<std::size_t>{0, 1, 5});
}

// Verifies source-owned blocks are valid no-op placements.
TEST_CASE("Block layout accepts source block drops", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    REQUIRE(layout.applyPlacement(placementOf({0, 4, 5}, 8)));

    const std::optional<SignalChainBlockLayout::DropIntent> intent =
        layout.dropIntent(1, 4, BlockPushDirection::Left);

    REQUIRE(intent.has_value());
    CHECK(intent->destination_index == 1);
    CHECK(intent->placement.blocks() == std::vector<std::size_t>{0, 4, 5});
}

// Verifies missing drop intents can use the last valid preview for final placement.
TEST_CASE("Block layout completes drops from current preview", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    REQUIRE(layout.previewMove(
        2,
        SignalChainBlockLayout::DropIntent{
            .destination_index = 2,
            .placement = placementOf({0, 1, 5}, 8),
        }));

    const SignalChainBlockLayout::DropCompletion completion = layout.completeDrop(2, std::nullopt);

    CHECK_FALSE(completion.move_destination_index.has_value());
    CHECK(completion.layout_changed);
    CHECK(layout.committedPlacement().blocks() == std::vector<std::size_t>{0, 1, 5});
}

} // namespace rock_hero::editor::ui
