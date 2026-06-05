#include "signal_chain_block_placement.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a valid placement from explicit block assignments or fails the test setup loudly.
[[nodiscard]] BlockPlacement placementOf(std::vector<std::size_t> blocks, std::size_t block_count)
{
    std::optional<BlockPlacement> built =
        BlockPlacement::fromIndices(std::move(blocks), block_count);
    REQUIRE(built.has_value());
    return *built;
}

} // namespace

// Verifies the identity placement maps plugin i to block i across the requested range.
TEST_CASE("Block placement compact maps plugin to matching block", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = BlockPlacement::compact(3, 8);

    CHECK(placement.pluginCount() == 3);
    CHECK(placement.blockCount() == 8);
    CHECK(placement.blocks() == std::vector<std::size_t>{0, 1, 2});
    CHECK(placement.blockForPlugin(1) == std::optional<std::size_t>{1});
    CHECK_FALSE(placement.blockForPlugin(3).has_value());
    CHECK(placement.pluginAtBlock(2) == std::optional<std::size_t>{2});
    CHECK_FALSE(placement.pluginAtBlock(5).has_value());
    CHECK(BlockPlacement::compact(10, 10).blockCount() == 10);
    CHECK(BlockPlacement::compact(10, 8).blockCount() == 10);
}

// Verifies fromIndices accepts bijections and rejects gaps, duplicates, and overflow.
TEST_CASE("Block placement validates one block per plugin", "[ui][signal-chain-layout]")
{
    CHECK(BlockPlacement::fromIndices({0, 2, 4}, 8).has_value());

    CHECK_FALSE(BlockPlacement::fromIndices({0, 2, 2}, 8).has_value());
    CHECK_FALSE(BlockPlacement::fromIndices({0, 2, 8}, 8).has_value());
    CHECK_FALSE(BlockPlacement::fromIndices({0, 1, 2}, 2).has_value());
}

// Verifies free and occupied blocks resolve to their implied linear chain indices.
TEST_CASE("Block placement derives linear indices from blocks", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = placementOf({0, 2, 5}, 8);

    CHECK(placement.insertionIndexForBlock(1) == 1);
    CHECK(placement.insertionIndexForBlock(6) == 3);
    CHECK(placement.chainIndexForPlugin(0) == 0);
    CHECK(placement.chainIndexForPlugin(2) == 2);
}

// Verifies an empty target relocates the dragged plugin and keeps the visual gap.
TEST_CASE("Block placement moves plugin onto empty block", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = BlockPlacement::compact(3, 8);

    const std::optional<BlockPlacement> moved =
        placement.withPluginAtBlock(2, 5, BlockPushDirection::Right);

    REQUIRE(moved.has_value());
    CHECK(moved->blocks() == std::vector<std::size_t>{0, 1, 5});
    CHECK(moved->chainIndexForPlugin(2) == 2);
}

// Verifies an occupied target pushes the run toward the gap on the entry side.
TEST_CASE("Block placement pushes occupied blocks by side", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = placementOf({0, 2, 3}, 4);

    const std::optional<BlockPlacement> moved =
        placement.withPluginAtBlock(0, 2, BlockPushDirection::Left);

    REQUIRE(moved.has_value());
    CHECK(moved->blocks() == std::vector<std::size_t>{2, 1, 3});
    CHECK(moved->chainIndexForPlugin(0) == 1);
}

// Verifies a side with no reachable gap rejects the drop instead of inventing one.
TEST_CASE("Block placement rejects blocked occupied sides", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = placementOf({0, 1, 3}, 4);

    CHECK_FALSE(placement.withPluginAtBlock(2, 1, BlockPushDirection::Left).has_value());
    CHECK(placement.withPluginAtBlock(2, 1, BlockPushDirection::Right).has_value());
}

// Verifies dropping a plugin on its own block is a valid no-op placement.
TEST_CASE("Block placement accepts source-owned block drops", "[ui][signal-chain-layout]")
{
    const BlockPlacement placement = placementOf({0, 4, 5}, 8);

    const std::optional<BlockPlacement> moved =
        placement.withPluginAtBlock(1, 4, BlockPushDirection::Left);

    REQUIRE(moved.has_value());
    CHECK(moved->blocks() == std::vector<std::size_t>{0, 4, 5});
    CHECK(moved->chainIndexForPlugin(1) == 1);
}

} // namespace rock_hero::editor::ui
