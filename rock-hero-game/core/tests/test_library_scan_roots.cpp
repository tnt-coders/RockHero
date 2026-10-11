#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/game/core/library/library_scan_roots.h>
#include <vector>

namespace rock_hero::game::core
{

// Verifies the default song folder always leads and the custom roots follow in order.
TEST_CASE("resolveLibraryScanRoots leads with the default song folder", "[core][library]")
{
    const std::filesystem::path songs = "C:/Users/p/AppData/Roaming/Rock Hero/songs";
    const std::vector<std::filesystem::path> custom = {"D:/Extra Songs", "E:/More Songs"};

    const std::vector<std::filesystem::path> roots = resolveLibraryScanRoots(songs, custom);

    REQUIRE(roots.size() == 3);
    CHECK(roots[0] == songs);
    CHECK(roots[1] == std::filesystem::path{"D:/Extra Songs"});
    CHECK(roots[2] == std::filesystem::path{"E:/More Songs"});
}

// Verifies an empty custom list resolves to just the default song folder.
TEST_CASE(
    "resolveLibraryScanRoots yields the default folder alone with no custom roots",
    "[core][library]")
{
    const std::filesystem::path songs = "C:/data/Rock Hero/songs";

    const std::vector<std::filesystem::path> roots = resolveLibraryScanRoots(songs, {});

    REQUIRE(roots.size() == 1);
    CHECK(roots.front() == songs);
}

// Verifies a custom root equal to the default folder (or repeated) is not scanned twice.
TEST_CASE("resolveLibraryScanRoots deduplicates repeated roots", "[core][library]")
{
    const std::filesystem::path songs = "C:/data/Rock Hero/songs";
    const std::vector<std::filesystem::path> custom = {
        songs,                     // same as the default -> dropped
        "D:/Extra Songs",          //
        "D:/Extra/../Extra Songs", // normalizes to the previous custom -> dropped
        "D:/Extra Songs",          // literal duplicate -> dropped
    };

    const std::vector<std::filesystem::path> roots = resolveLibraryScanRoots(songs, custom);

    REQUIRE(roots.size() == 2);
    CHECK(roots[0] == songs);
    CHECK(roots[1] == std::filesystem::path{"D:/Extra Songs"});
}

} // namespace rock_hero::game::core
