#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/signal_chain/signal_chain_block_placement.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Builds a valid placement from explicit block assignments or fails the test setup loudly.
[[nodiscard]] SignalChainBlockPlacement placementOf(
    std::vector<std::size_t> blocks, std::size_t block_count)
{
    std::optional<SignalChainBlockPlacement> built =
        SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
    REQUIRE(built.has_value());
    return *built;
}

} // namespace

// Verifies the identity placement maps plugin i to block i across the requested range.
TEST_CASE("Block placement compact maps plugin to matching block", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = SignalChainBlockPlacement::compact(3, 8);

    CHECK(placement.pluginCount() == 3);
    CHECK(placement.blockCount() == 8);
    CHECK(placement.blocks() == std::vector<std::size_t>{0, 1, 2});
    CHECK(placement.blockForPlugin(1) == std::optional<std::size_t>{1});
    CHECK_FALSE(placement.blockForPlugin(3).has_value());
    CHECK(placement.pluginAtBlock(2) == std::optional<std::size_t>{2});
    CHECK_FALSE(placement.pluginAtBlock(5).has_value());
    CHECK(SignalChainBlockPlacement::compact(10, 10).blockCount() == 10);
    CHECK(SignalChainBlockPlacement::compact(10, 8).blockCount() == 10);
}

// Verifies fromIndices accepts gaps and rejects duplicates and overflow.
TEST_CASE("Block placement validates one block per plugin", "[core][signal-chain]")
{
    CHECK(SignalChainBlockPlacement::fromIndices({0, 2, 4}, 8).has_value());

    CHECK_FALSE(SignalChainBlockPlacement::fromIndices({0, 2, 2}, 8).has_value());
    CHECK_FALSE(SignalChainBlockPlacement::fromIndices({0, 2, 8}, 8).has_value());
    CHECK_FALSE(SignalChainBlockPlacement::fromIndices({0, 1, 2}, 2).has_value());
}

// Verifies free and occupied blocks resolve to their implied linear chain indices.
TEST_CASE("Block placement derives linear indices from blocks", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = placementOf({0, 2, 5}, 8);

    CHECK(placement.insertionIndexForBlock(1) == 1);
    CHECK(placement.insertionIndexForBlock(6) == 3);
    CHECK(placement.chainIndexForPlugin(0) == 0);
    CHECK(placement.chainIndexForPlugin(2) == 2);
}

// Verifies an empty target relocates the dragged plugin and keeps the visual gap.
TEST_CASE("Block placement moves plugin onto empty block", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = SignalChainBlockPlacement::compact(3, 8);

    const std::optional<SignalChainBlockPlacement> moved = placement.withPluginAtBlock(2, 5);

    REQUIRE(moved.has_value());
    if (moved.has_value())
    {
        CHECK(moved->blocks() == std::vector<std::size_t>{0, 1, 5});
        CHECK(moved->chainIndexForPlugin(2) == 2);
    }
}

// Verifies adjacent occupied targets swap with the dragged plugin.
TEST_CASE("Block placement swaps adjacent occupied blocks", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = SignalChainBlockPlacement::compact(3, 8);

    const std::optional<SignalChainBlockPlacement> moved = placement.withPluginAtBlock(2, 1);

    REQUIRE(moved.has_value());
    if (moved.has_value())
    {
        CHECK(moved->blocks() == std::vector<std::size_t>{0, 2, 1});
        CHECK(moved->chainIndexForPlugin(2) == 1);
    }
}

// Verifies longer occupied-target moves shift the run toward the dragged plugin's source gap.
TEST_CASE("Block placement shifts occupied targets from source", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = placementOf({0, 2, 3}, 4);

    const std::optional<SignalChainBlockPlacement> moved = placement.withPluginAtBlock(0, 2);

    REQUIRE(moved.has_value());
    if (moved.has_value())
    {
        CHECK(moved->blocks() == std::vector<std::size_t>{2, 1, 3});
        CHECK(moved->chainIndexForPlugin(0) == 1);
    }
}

// Verifies dropping a plugin on its own block is a valid no-op placement.
TEST_CASE("Block placement accepts source-owned block drops", "[core][signal-chain]")
{
    const SignalChainBlockPlacement placement = placementOf({0, 4, 5}, 8);

    const std::optional<SignalChainBlockPlacement> moved = placement.withPluginAtBlock(1, 4);

    REQUIRE(moved.has_value());
    if (moved.has_value())
    {
        CHECK(moved->blocks() == std::vector<std::size_t>{0, 4, 5});
        CHECK(moved->chainIndexForPlugin(1) == 1);
    }
}

} // namespace rock_hero::editor::core
