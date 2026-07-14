#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <rock_hero/common/core/package/package_description.h>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/game/core/library/i_album_art_generator.h>
#include <rock_hero/game/core/library/i_library_directory_lister.h>
#include <rock_hero/game/core/library/i_library_package_describer.h>
#include <rock_hero/game/core/library/library_entry_projection.h>
#include <rock_hero/game/core/library/library_scan_engine.h>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// Lister fake: returns the facts configured per root and records which roots were listed.
class FakeDirectoryLister final : public ILibraryDirectoryLister
{
public:
    [[nodiscard]] std::vector<PackageFileFacts> listPackages(
        const std::filesystem::path& scan_root) override
    {
        listed_roots.push_back(scan_root);
        if (const auto found = files_by_root.find(scan_root); found != files_by_root.end())
        {
            return found->second;
        }
        return {};
    }

    std::map<std::filesystem::path, std::vector<PackageFileFacts>> files_by_root;
    std::vector<std::filesystem::path> listed_roots;
};

// Describer fake: returns a configured description or failure per path; empty description default.
class FakePackageDescriber final : public ILibraryPackageDescriber
{
public:
    [[nodiscard]] std::expected<common::core::PackageDescription, common::core::SongPackageError>
    describe(const std::filesystem::path& package_path) override
    {
        described_paths.push_back(package_path);
        if (const auto failure = failures.find(package_path); failure != failures.end())
        {
            return std::unexpected{failure->second};
        }
        if (const auto description = descriptions.find(package_path);
            description != descriptions.end())
        {
            return description->second;
        }
        return common::core::PackageDescription{};
    }

    std::map<std::filesystem::path, common::core::PackageDescription> descriptions;
    std::map<std::filesystem::path, common::core::SongPackageError> failures;
    std::vector<std::filesystem::path> described_paths;
};

// Album-art fake: returns a configured image name or failure per path; no art by default.
class FakeAlbumArtGenerator final : public IAlbumArtGenerator
{
public:
    [[nodiscard]] std::expected<AlbumArt, AlbumArtError> generate(
        const std::filesystem::path& package_path) override
    {
        requested_paths.push_back(package_path);
        if (const auto failure = failures.find(package_path); failure != failures.end())
        {
            return std::unexpected{failure->second};
        }
        if (const auto image = images.find(package_path); image != images.end())
        {
            return AlbumArt{.image_file_name = image->second};
        }
        return AlbumArt{};
    }

    std::map<std::filesystem::path, std::string> images;
    std::map<std::filesystem::path, AlbumArtError> failures;
    std::vector<std::filesystem::path> requested_paths;
};

// A previous-index entry carrying identity facts plus a title, to prove Reuse restores it whole.
[[nodiscard]] LibraryEntry priorEntry(
    const std::filesystem::path& path, const std::int64_t size_bytes,
    const std::int64_t modified_milliseconds, const std::string& title)
{
    LibraryEntry entry;
    entry.package_path = path;
    entry.file_size_bytes = size_bytes;
    entry.modification_time_milliseconds = modified_milliseconds;
    entry.metadata.title = title;
    return entry;
}

// One current-file fact.
[[nodiscard]] PackageFileFacts fileFacts(
    const std::filesystem::path& path, const std::int64_t size_bytes,
    const std::int64_t modified_milliseconds)
{
    return PackageFileFacts{
        .path = path,
        .file_size_bytes = size_bytes,
        .modification_time_milliseconds = modified_milliseconds,
    };
}

// A minimal description carrying just a title, for asserting Add/Rescan captured it.
[[nodiscard]] common::core::PackageDescription descriptionWithTitle(const std::string& title)
{
    common::core::PackageDescription description;
    description.metadata.title = title;
    return description;
}

// Pumps the engine to completion, returning the final step (callers may ignore it).
LibraryScanStep runToCompletion(
    LibraryScanEngine& engine, const common::core::CancellationToken& token)
{
    LibraryScanStep last{};
    while (!engine.done())
    {
        last = engine.step(token);
    }
    return last;
}

// Finds a scanned entry by package path.
[[nodiscard]] const LibraryEntry* findEntry(
    const LibraryIndex& index, const std::filesystem::path& path)
{
    for (const LibraryEntry& entry : index.entries)
    {
        if (entry.package_path == path)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

// Verifies the pure projection maps a description's fields into a library entry.
TEST_CASE("makeLibraryEntry maps a description into an entry", "[core][library]")
{
    const PackageFileFacts facts = fileFacts("C:/Songs/a.rock", 10, 20);
    common::core::PackageDescription description;
    description.metadata = {.title = "A", .artist = "B", .album = "C", .year = 2020};
    description.warnings = {"a peek warning"};
    description.arrangements.push_back(
        common::core::ArrangementDescription{
            .id = "arr-1",
            .part = common::core::Part::Lead,
            .chart_ref = "charts/x.chart.json",
            .tuning =
                common::core::ArrangementTuningDescription{
                    .strings = {"E2"}, .capo = 0, .cent_offset = 0.0
                },
            .audio_asset_present = true,
        });

    const std::expected<common::core::PackageDescription, common::core::SongPackageError> ok =
        description;
    const LibraryEntry entry = makeLibraryEntry(facts, ok, "a.png");

    CHECK(entry.package_path == facts.path);
    CHECK(entry.file_size_bytes == 10);
    CHECK(entry.modification_time_milliseconds == 20);
    CHECK(entry.album_art_file_name == "a.png");
    CHECK(entry.metadata.title == "A");
    CHECK(entry.metadata.year == 2020);
    REQUIRE(entry.arrangements.size() == 1);
    CHECK(entry.arrangements.front().id == "arr-1");
    CHECK(entry.arrangements.front().part == std::optional{common::core::Part::Lead});
    CHECK(entry.arrangements.front().tuning.has_value());
    CHECK_FALSE(entry.arrangements.front().intensity.has_value());
    REQUIRE(entry.warnings.size() == 1);
    CHECK(entry.warnings.front() == "a peek warning");
}

// Verifies a read failure still produces an entry that keeps its identity facts and warns.
TEST_CASE("makeLibraryEntry warns on a read failure but keeps identity", "[core][library]")
{
    const PackageFileFacts facts = fileFacts("C:/Songs/bad.rock", 7, 8);
    const std::expected<common::core::PackageDescription, common::core::SongPackageError> failed =
        std::unexpected{common::core::SongPackageError{
            common::core::SongPackageErrorCode::CouldNotExtractPackage, "corrupt archive"
        }};

    const LibraryEntry entry = makeLibraryEntry(facts, failed, "");

    CHECK(entry.package_path == facts.path);
    CHECK(entry.file_size_bytes == 7);
    CHECK(entry.metadata.title.empty());
    CHECK(entry.arrangements.empty());
    REQUIRE(entry.warnings.size() == 1);
    CHECK(entry.warnings.front().find("corrupt archive") != std::string::npos);
}

// Verifies a mixed plan (add, rescan, reuse, remove, one malformed) yields the correct final
// index: reuse restores the cached entry, rescan re-describes and replaces it, and only
// added/rescanned packages are described.
TEST_CASE("Scan engine applies a mixed plan into the final index", "[core][library]")
{
    FakeDirectoryLister lister;
    FakePackageDescriber describer;
    FakeAlbumArtGenerator album_art_generator;

    LibraryIndex prior;
    prior.entries.push_back(priorEntry("C:/Songs/keep.rock", 100, 1000, "Kept Song"));
    prior.entries.push_back(priorEntry("C:/Songs/changed.rock", 99, 999, "Stale Title"));
    prior.entries.push_back(priorEntry("C:/Songs/gone.rock", 200, 2000, "Gone Song"));

    lister.files_by_root["C:/Songs"] = {
        fileFacts("C:/Songs/keep.rock", 100, 1000),    // unchanged -> Reuse
        fileFacts("C:/Songs/changed.rock", 100, 1000), // size changed -> Rescan
        fileFacts("C:/Songs/new.rock", 300, 3000),     // -> Add
        fileFacts("C:/Songs/bad.rock", 400, 4000),     // -> Add, describe fails
    };
    describer.descriptions["C:/Songs/changed.rock"] = descriptionWithTitle("Rescanned Title");
    describer.descriptions["C:/Songs/new.rock"] = descriptionWithTitle("New Song");
    describer.failures.emplace(
        "C:/Songs/bad.rock",
        common::core::SongPackageError{
            common::core::SongPackageErrorCode::CouldNotExtractPackage, "corrupt"
        });

    LibraryScanEngine engine{lister, describer, album_art_generator};
    const std::vector<std::filesystem::path> roots = {"C:/Songs"};
    engine.begin(std::move(prior), roots);

    const common::core::CancellationToken token;
    const LibraryScanStep final_step = runToCompletion(engine, token);

    CHECK(final_step.phase == LibraryScanPhase::Complete);
    CHECK(final_step.progress.completed_actions == 5);
    CHECK(final_step.progress.total_actions == 5);

    const LibraryIndex& index = engine.index();
    REQUIRE(index.entries.size() == 4);
    CHECK(findEntry(index, "C:/Songs/gone.rock") == nullptr);

    const LibraryEntry* const kept = findEntry(index, "C:/Songs/keep.rock");
    REQUIRE(kept != nullptr);
    CHECK(kept->metadata.title == "Kept Song"); // reused whole, never re-described

    const LibraryEntry* const rescanned = findEntry(index, "C:/Songs/changed.rock");
    REQUIRE(rescanned != nullptr);
    CHECK(
        rescanned->metadata.title == "Rescanned Title"); // re-described, not the stale prior title
    CHECK(rescanned->file_size_bytes == 100);            // identity facts updated to current

    const LibraryEntry* const added = findEntry(index, "C:/Songs/new.rock");
    REQUIRE(added != nullptr);
    CHECK(added->metadata.title == "New Song");

    const LibraryEntry* const bad = findEntry(index, "C:/Songs/bad.rock");
    REQUIRE(bad != nullptr);
    CHECK(bad->hasWarnings());

    // Only the added and rescanned packages were described; reuse and remove never open an archive.
    CHECK(describer.described_paths.size() == 3);
}

// Verifies cancellation stops the scan promptly and leaves a loadable, partial index, and that
// progress reports the completed count and current package along the way.
TEST_CASE("Scan engine cancellation leaves a partial index", "[core][library]")
{
    FakeDirectoryLister lister;
    FakePackageDescriber describer;
    FakeAlbumArtGenerator album_art_generator;

    lister.files_by_root["C:/Songs"] = {
        fileFacts("C:/Songs/a.rock", 1, 1),
        fileFacts("C:/Songs/b.rock", 2, 2),
        fileFacts("C:/Songs/c.rock", 3, 3),
    };

    LibraryScanEngine engine{lister, describer, album_art_generator};
    const std::vector<std::filesystem::path> roots = {"C:/Songs"};
    engine.begin(LibraryIndex{}, roots);

    common::core::CancellationToken token;
    const LibraryScanStep first = engine.step(token);
    CHECK(first.phase == LibraryScanPhase::Scanning);
    CHECK(first.commit_checkpoint);
    CHECK(first.progress.completed_actions == 1);
    CHECK(first.progress.total_actions == 3);
    CHECK(
        first.progress.current_package == std::filesystem::path{"C:/Songs/a.rock"}); // path-sorted
    CHECK(engine.index().entries.size() == 1);

    token.cancel();
    const LibraryScanStep cancelled = engine.step(token);
    CHECK(cancelled.phase == LibraryScanPhase::Cancelled);
    CHECK(cancelled.commit_checkpoint); // the partial index is durable to persist
    CHECK(engine.done());
    CHECK(engine.index().entries.size() == 1); // only the pre-cancel entry, still loadable
}

// Verifies album-art results fill the entry, and an album-art failure warns without dropping it.
TEST_CASE("Scan engine records album art and warns on generation failure", "[core][library]")
{
    FakeDirectoryLister lister;
    FakePackageDescriber describer;
    FakeAlbumArtGenerator album_art_generator;

    lister.files_by_root["C:/Songs"] = {
        fileFacts("C:/Songs/art.rock", 1, 1),
        fileFacts("C:/Songs/noart.rock", 2, 2),
        fileFacts("C:/Songs/badart.rock", 3, 3),
    };
    album_art_generator.images["C:/Songs/art.rock"] = "art.png";
    album_art_generator.failures.emplace(
        "C:/Songs/badart.rock",
        AlbumArtError{AlbumArtErrorCode::GenerationFailed, "decode failed"});

    LibraryScanEngine engine{lister, describer, album_art_generator};
    const std::vector<std::filesystem::path> roots = {"C:/Songs"};
    engine.begin(LibraryIndex{}, roots);

    const common::core::CancellationToken token;
    runToCompletion(engine, token);

    const LibraryIndex& index = engine.index();
    REQUIRE(index.entries.size() == 3);

    const LibraryEntry* const art = findEntry(index, "C:/Songs/art.rock");
    REQUIRE(art != nullptr);
    CHECK(art->album_art_file_name == "art.png");

    const LibraryEntry* const no_art = findEntry(index, "C:/Songs/noart.rock");
    REQUIRE(no_art != nullptr);
    CHECK(no_art->album_art_file_name.empty());
    CHECK_FALSE(no_art->hasWarnings());

    const LibraryEntry* const bad_art = findEntry(index, "C:/Songs/badart.rock");
    REQUIRE(bad_art != nullptr);
    CHECK(bad_art->album_art_file_name.empty());
    REQUIRE(bad_art->hasWarnings());
    CHECK(bad_art->warnings.front().find("decode failed") != std::string::npos);
}

// Verifies a scan spanning several roots lists every root and indexes packages from all of them.
TEST_CASE("Scan engine aggregates packages across multiple roots", "[core][library]")
{
    FakeDirectoryLister lister;
    FakePackageDescriber describer;
    FakeAlbumArtGenerator album_art_generator;

    lister.files_by_root["C:/RootA"] = {fileFacts("C:/RootA/one.rock", 1, 1)};
    lister.files_by_root["C:/RootB"] = {fileFacts("C:/RootB/two.rock", 2, 2)};

    LibraryScanEngine engine{lister, describer, album_art_generator};
    const std::vector<std::filesystem::path> roots = {"C:/RootA", "C:/RootB"};
    engine.begin(LibraryIndex{}, roots);

    const common::core::CancellationToken token;
    runToCompletion(engine, token);

    CHECK(lister.listed_roots.size() == 2);
    const LibraryIndex& index = engine.index();
    REQUIRE(index.entries.size() == 2);
    CHECK(findEntry(index, "C:/RootA/one.rock") != nullptr);
    CHECK(findEntry(index, "C:/RootB/two.rock") != nullptr);
}

// Verifies an empty scan completes immediately with an empty index.
TEST_CASE("Scan engine completes immediately with nothing to scan", "[core][library]")
{
    FakeDirectoryLister lister;
    FakePackageDescriber describer;
    FakeAlbumArtGenerator album_art_generator;

    LibraryScanEngine engine{lister, describer, album_art_generator};
    const std::vector<std::filesystem::path> roots = {"C:/Empty"};
    engine.begin(LibraryIndex{}, roots);

    CHECK(engine.done());
    CHECK(engine.phase() == LibraryScanPhase::Complete);
    CHECK(engine.index().entries.empty());
    CHECK(lister.listed_roots.size() == 1); // the empty root was still listed
}

} // namespace rock_hero::game::core
