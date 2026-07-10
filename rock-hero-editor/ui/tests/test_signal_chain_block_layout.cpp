#include "signal_chain/signal_chain_block_layout.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

using core::SignalChainBlockPlacement;

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
[[nodiscard]] SignalChainBlockPlacement placementOf(
    std::vector<std::size_t> blocks, std::size_t block_count)
{
    std::optional<SignalChainBlockPlacement> built =
        SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
    REQUIRE(built.has_value());
    if (built.has_value())
    {
        return std::move(*built);
    }
    // Unreachable fallback: the REQUIRE above aborts the test when the indices are invalid.
    return SignalChainBlockPlacement::compact(0, block_count);
}

// Starts a test from a gapped layout through the public drop path: a drop whose destination equals
// its source keeps linear order and only fixes the visual blocks.
void installPlacement(
    SignalChainBlockLayout& layout, std::vector<std::size_t> blocks, std::size_t block_count)
{
    SignalChainBlockPlacement placement = placementOf(std::move(blocks), block_count);
    const std::vector<std::size_t> expected_blocks = placement.blocks();
    (void)layout.completeDrop(
        0,
        SignalChainBlockLayout::DropIntent{
            .destination_index = 0,
            .placement = std::move(placement),
        });
    REQUIRE(layout.cachedPlacement().blocks() == expected_blocks);
}

// Applies a hover preview in tests through the same intent path used by the view.
void previewDrop(
    SignalChainBlockLayout& layout, std::size_t source_index, std::size_t target_block_index,
    std::vector<std::size_t> expected_blocks)
{
    std::optional<SignalChainBlockLayout::DropIntent> intent =
        layout.dropIntent(source_index, target_block_index);
    REQUIRE(intent.has_value());
    if (intent.has_value())
    {
        REQUIRE(layout.previewMove(source_index, std::move(*intent)));
    }
    CHECK(layout.activePlacement().blocks() == expected_blocks);
}

} // namespace

// Verifies the configured minimum block count remains visible for shorter chains.
TEST_CASE("Block layout keeps configured fixed block count", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "cab"}));

    CHECK(layout.blockCount() == 8);
    CHECK(layout.cachedPlacement().blocks() == std::vector<std::size_t>{0, 1});
}

// Verifies controller-authored placement is adopted as the cached render layout.
TEST_CASE("Block layout adopts authored block placement", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    std::vector<core::PluginViewState> plugins = makePlugins({"amp", "cab"});
    plugins[0].block_index = 1;
    plugins[1].block_index = 4;

    layout.applyPlugins(plugins);

    CHECK(layout.cachedPlacement().blocks() == std::vector<std::size_t>{1, 4});
}

// Verifies invalid controller placement falls back to a compact render layout.
TEST_CASE("Block layout compacts invalid block placement", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    std::vector<core::PluginViewState> plugins = makePlugins({"amp", "cab"});
    plugins[0].block_index = 3;
    plugins[1].block_index = 3;

    layout.applyPlugins(plugins);

    CHECK(layout.cachedPlacement().blocks() == std::vector<std::size_t>{0, 1});
}

// Verifies empty fixed blocks map to the insertion index implied by visible placement.
TEST_CASE("Block layout maps empty blocks to insert slots", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    std::vector<core::PluginViewState> plugins = makePlugins({"amp", "cab"});
    plugins[0].block_index = 0;
    plugins[1].block_index = 4;
    layout.applyPlugins(plugins);

    CHECK(layout.insertionIndexForBlock(2) == std::optional<std::size_t>{1});
    CHECK_FALSE(layout.insertionIndexForBlock(4).has_value());
}

// Verifies occupied-block drops use the source gap to compute final placement.
TEST_CASE("Block layout drops onto occupied blocks", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{4};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    installPlacement(layout, {0, 2, 3}, 4);

    const std::optional<SignalChainBlockLayout::DropIntent> intent = layout.dropIntent(0, 2);

    REQUIRE(intent.has_value());
    if (intent.has_value())
    {
        CHECK(intent->destination_index == 1);
        CHECK(intent->placement.blocks() == std::vector<std::size_t>{2, 1, 3});
    }
}

// Verifies adjacent occupied-block drops swap with the target occupant.
TEST_CASE("Block layout swaps adjacent occupied blocks", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));

    const std::optional<SignalChainBlockLayout::DropIntent> intent = layout.dropIntent(2, 1);

    REQUIRE(intent.has_value());
    if (intent.has_value())
    {
        CHECK(intent->destination_index == 1);
        CHECK(intent->placement.blocks() == std::vector<std::size_t>{0, 2, 1});
    }
    CHECK(layout.canReceiveDrop(2, 1));
}

// Verifies reversing a rightward drag steps back through swaps before restoring the source slot.
TEST_CASE("Block layout reverses rightward previews", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));

    previewDrop(layout, 0, 1, {1, 0, 2});
    CHECK(layout.blockForPlugin(1) == std::optional<std::size_t>{0});
    previewDrop(layout, 0, 2, {2, 0, 1});
    previewDrop(layout, 0, 3, {3, 1, 2});
    previewDrop(layout, 0, 2, {2, 1, 3});
    previewDrop(layout, 0, 1, {1, 2, 3});
    previewDrop(layout, 0, 0, {0, 1, 2});
}

// Verifies reversing a leftward drag has the same local-swap behavior.
TEST_CASE("Block layout reverses leftward previews", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    installPlacement(layout, {2, 3, 4}, 8);

    previewDrop(layout, 2, 3, {2, 4, 3});
    previewDrop(layout, 2, 2, {3, 4, 2});
    previewDrop(layout, 2, 1, {2, 3, 1});
    previewDrop(layout, 2, 2, {1, 3, 2});
    previewDrop(layout, 2, 3, {1, 2, 3});
    previewDrop(layout, 2, 4, {2, 3, 4});
}

// Verifies empty blocks can hold a visual gap while preserving the implied linear order.
TEST_CASE("Block layout drops into empty fixed blocks", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));

    const std::optional<SignalChainBlockLayout::DropIntent> intent = layout.dropIntent(2, 5);

    REQUIRE(intent.has_value());
    if (intent.has_value())
    {
        CHECK(intent->destination_index == 2);
        CHECK(intent->placement.blocks() == std::vector<std::size_t>{0, 1, 5});
    }
}

// Verifies source-owned blocks are valid no-op placements.
TEST_CASE("Block layout accepts source block drops", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    installPlacement(layout, {0, 4, 5}, 8);

    const std::optional<SignalChainBlockLayout::DropIntent> intent = layout.dropIntent(1, 4);

    REQUIRE(intent.has_value());
    if (intent.has_value())
    {
        CHECK(intent->destination_index == 1);
        CHECK(intent->placement.blocks() == std::vector<std::size_t>{0, 4, 5});
    }
}

// Verifies missing drop intents can use the last valid preview for final placement.
TEST_CASE("Block layout completes drops from current preview", "[ui][signal-chain-layout]")
{
    SignalChainBlockLayout layout{8};
    (void)layout.applyPlugins(makePlugins({"amp", "drive", "cab"}));
    REQUIRE(layout.previewMove(
        2,
        SignalChainBlockLayout::DropIntent{
            .destination_index = 2,
            .placement = placementOf({0, 1, 5}, 8),
        }));

    const SignalChainBlockLayout::DropCompletion completion = layout.completeDrop(2, std::nullopt);

    CHECK_FALSE(completion.move_destination_index.has_value());
    CHECK(completion.layout_changed);
    CHECK(layout.cachedPlacement().blocks() == std::vector<std::size_t>{0, 1, 5});
}

} // namespace rock_hero::editor::ui
