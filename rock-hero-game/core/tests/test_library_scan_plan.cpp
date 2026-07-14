#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/game/core/library/library_scan_plan.h>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// A previous-index entry carrying only the identity facts the planner reads.
[[nodiscard]] LibraryEntry entryWithFacts(
    const std::filesystem::path& path, const std::int64_t size_bytes,
    const std::int64_t modified_milliseconds)
{
    LibraryEntry entry;
    entry.package_path = path;
    entry.file_size_bytes = size_bytes;
    entry.modification_time_milliseconds = modified_milliseconds;
    return entry;
}

} // namespace

// Verifies every diff case in one plan: new file → Add, changed size or mtime → Rescan,
// unchanged → Reuse, vanished → Remove.
TEST_CASE("Library scan planner diffs the index against file facts", "[core][library]")
{
    LibraryIndex previous;
    previous.entries.push_back(entryWithFacts("C:/Songs/unchanged.rock", 100, 1000));
    previous.entries.push_back(entryWithFacts("C:/Songs/resized.rock", 200, 2000));
    previous.entries.push_back(entryWithFacts("C:/Songs/retimed.rock", 300, 3000));
    previous.entries.push_back(entryWithFacts("C:/Songs/vanished.rock", 400, 4000));

    const std::vector<PackageFileFacts> current = {
        {.path = "C:/Songs/unchanged.rock",
         .file_size_bytes = 100,
         .modification_time_milliseconds = 1000},
        {.path = "C:/Songs/resized.rock",
         .file_size_bytes = 250,
         .modification_time_milliseconds = 2000},
        {.path = "C:/Songs/retimed.rock",
         .file_size_bytes = 300,
         .modification_time_milliseconds = 3500},
        {.path = "C:/Songs/brand-new.rock",
         .file_size_bytes = 500,
         .modification_time_milliseconds = 5000},
    };

    const std::vector<LibraryScanAction> plan = planLibraryScan(previous, current);

    REQUIRE(plan.size() == 5);
    // Actions come back sorted by package path.
    CHECK(plan[0].package_path == std::filesystem::path{"C:/Songs/brand-new.rock"});
    CHECK(plan[0].kind == LibraryScanActionKind::Add);
    CHECK(plan[1].package_path == std::filesystem::path{"C:/Songs/resized.rock"});
    CHECK(plan[1].kind == LibraryScanActionKind::Rescan);
    CHECK(plan[2].package_path == std::filesystem::path{"C:/Songs/retimed.rock"});
    CHECK(plan[2].kind == LibraryScanActionKind::Rescan);
    CHECK(plan[3].package_path == std::filesystem::path{"C:/Songs/unchanged.rock"});
    CHECK(plan[3].kind == LibraryScanActionKind::Reuse);
    CHECK(plan[4].package_path == std::filesystem::path{"C:/Songs/vanished.rock"});
    CHECK(plan[4].kind == LibraryScanActionKind::Remove);
}

// Verifies a moved package plans as Remove + Add today; hash-based move detection (which would
// turn this into a single Move that preserves the cached description) activates once
// docs/roadmap/10-format-versioning-and-chart-identity.md supplies package identity hashes.
TEST_CASE("Library scan planner treats a moved package as remove plus add", "[core][library]")
{
    LibraryIndex previous;
    previous.entries.push_back(entryWithFacts("C:/Songs/old-home.rock", 100, 1000));

    const std::vector<PackageFileFacts> current = {
        {.path = "C:/Songs/new-home.rock",
         .file_size_bytes = 100,
         .modification_time_milliseconds = 1000},
    };

    const std::vector<LibraryScanAction> plan = planLibraryScan(previous, current);

    REQUIRE(plan.size() == 2);
    CHECK(plan[0].package_path == std::filesystem::path{"C:/Songs/new-home.rock"});
    CHECK(plan[0].kind == LibraryScanActionKind::Add);
    CHECK(plan[1].package_path == std::filesystem::path{"C:/Songs/old-home.rock"});
    CHECK(plan[1].kind == LibraryScanActionKind::Remove);
}

// Verifies the plan is deterministic regardless of fact order and collapses duplicate paths.
TEST_CASE("Library scan planner is deterministic over unordered input", "[core][library]")
{
    const LibraryIndex previous;
    const std::vector<PackageFileFacts> shuffled = {
        {.path = "C:/Songs/zebra.rock", .file_size_bytes = 3, .modification_time_milliseconds = 30},
        {.path = "C:/Songs/alpha.rock", .file_size_bytes = 1, .modification_time_milliseconds = 10},
        // Duplicate path: the later occurrence wins, producing one action.
        {.path = "C:/Songs/alpha.rock", .file_size_bytes = 2, .modification_time_milliseconds = 20},
    };

    const std::vector<LibraryScanAction> plan = planLibraryScan(previous, shuffled);

    REQUIRE(plan.size() == 2);
    CHECK(plan[0].package_path == std::filesystem::path{"C:/Songs/alpha.rock"});
    CHECK(plan[0].kind == LibraryScanActionKind::Add);
    CHECK(plan[1].package_path == std::filesystem::path{"C:/Songs/zebra.rock"});
    CHECK(plan[1].kind == LibraryScanActionKind::Add);
}

// Verifies an empty previous index (fresh rebuild) plans every current file as Add.
TEST_CASE("Library scan planner adds everything on a fresh rebuild", "[core][library]")
{
    const LibraryIndex previous;
    const std::vector<PackageFileFacts> current = {
        {.path = "C:/Songs/one.rock", .file_size_bytes = 1, .modification_time_milliseconds = 10},
        {.path = "C:/Songs/two.rock", .file_size_bytes = 2, .modification_time_milliseconds = 20},
    };

    const std::vector<LibraryScanAction> plan = planLibraryScan(previous, current);

    REQUIRE(plan.size() == 2);
    CHECK(plan[0].kind == LibraryScanActionKind::Add);
    CHECK(plan[1].kind == LibraryScanActionKind::Add);
}

} // namespace rock_hero::game::core
