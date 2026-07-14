#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <juce_events/juce_events.h>
#include <rock_hero/game/core/library/library_index_store.h>
#include <string>

namespace rock_hero::game::core
{

namespace
{

// Test-local temp directory owning one test case's index file.
class TemporaryIndexDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporaryIndexDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-library-index-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the test directory on a best-effort basis.
    ~TemporaryIndexDirectory() noexcept
    {
        try
        {
            std::filesystem::remove_all(m_path);
        }
        catch (...)
        {
            // Best-effort cleanup; a straggling temp directory cannot affect other tests.
        }
    }

    TemporaryIndexDirectory(const TemporaryIndexDirectory&) = delete;
    TemporaryIndexDirectory(TemporaryIndexDirectory&&) = delete;
    TemporaryIndexDirectory& operator=(const TemporaryIndexDirectory&) = delete;
    TemporaryIndexDirectory& operator=(TemporaryIndexDirectory&&) = delete;

    // The isolated index file path for the test's load/save calls.
    [[nodiscard]] std::filesystem::path indexFile() const
    {
        return m_path / "library-index.json";
    }

private:
    std::filesystem::path m_path;
};

// Writes raw text to the index path so corruption cases exercise the load tolerance policy.
void writeIndexText(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream stream{path, std::ios::binary};
    stream << contents;
}

// A fully populated entry plus a minimal one, covering every optional field's presence and
// absence in the same round trip.
[[nodiscard]] LibraryIndex fixtureIndex()
{
    LibraryEntry full;
    full.package_path = std::filesystem::path{"C:/Songs/peek song.rock"};
    full.file_size_bytes = 123456;
    full.modification_time_milliseconds = 1720900000000;
    full.package_hash = "abc123";
    full.metadata = {.title = "Peek Song", .artist = "Peek Artist", .album = "Peeks", .year = 2026};
    full.thumbnail_file_name = "abc123.png";
    full.warnings = {"arrangement 2 backing audio entry is missing"};

    LibraryArrangementSummary lead;
    lead.id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";
    lead.part = common::core::Part::Lead;
    lead.tuning = common::core::ArrangementTuningDescription{
        .strings = {"D2", "A2", "D3", "G3", "B3", "E4"},
        .capo = 2,
        .cent_offset = -6.0,
    };
    lead.intensity = ArrangementIntensity{.value = 7.5, .calculator_version = 3};
    full.arrangements.push_back(lead);

    // A bare arrangement: no part, no tuning, no intensity (the "Unknown" difficulty bucket).
    full.arrangements.push_back(LibraryArrangementSummary{});

    LibraryEntry minimal;
    minimal.package_path = std::filesystem::path{"C:/Songs/minimal.rock"};
    minimal.file_size_bytes = 1;
    minimal.modification_time_milliseconds = 2;

    return LibraryIndex{.entries = {full, minimal}};
}

} // namespace

// Verifies a saved index loads back field-for-field, including absent optionals and warnings.
TEST_CASE("Library index round-trips through its JSON document", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;
    const LibraryIndex written = fixtureIndex();

    REQUIRE(saveLibraryIndex(written, directory.indexFile()).has_value());
    const LibraryIndexLoadResult loaded = loadLibraryIndex(directory.indexFile());

    REQUIRE_FALSE(loaded.rebuild_required);
    REQUIRE(loaded.index.entries.size() == 2);

    const LibraryEntry& full = loaded.index.entries.front();
    CHECK(full.package_path == written.entries.front().package_path);
    CHECK(full.file_size_bytes == 123456);
    CHECK(full.modification_time_milliseconds == 1720900000000);
    CHECK(full.package_hash == "abc123");
    CHECK(full.metadata.title == "Peek Song");
    CHECK(full.metadata.artist == "Peek Artist");
    CHECK(full.metadata.album == "Peeks");
    CHECK(full.metadata.year == 2026);
    CHECK(full.thumbnail_file_name == "abc123.png");
    CHECK(full.hasWarnings());
    REQUIRE(full.warnings.size() == 1);

    REQUIRE(full.arrangements.size() == 2);
    const LibraryArrangementSummary& lead = full.arrangements.front();
    CHECK(lead.id == "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4");
    CHECK(lead.part == std::optional{common::core::Part::Lead});
    REQUIRE(lead.tuning.has_value());
    CHECK(lead.tuning->strings.size() == 6);
    CHECK(lead.tuning->strings.front() == "D2");
    CHECK(lead.tuning->capo == 2);
    CHECK(lead.tuning->cent_offset == Catch::Approx(-6.0));
    REQUIRE(lead.intensity.has_value());
    CHECK(lead.intensity->value == Catch::Approx(7.5));
    CHECK(lead.intensity->calculator_version == 3);

    const LibraryArrangementSummary& bare = full.arrangements.back();
    CHECK_FALSE(bare.part.has_value());
    CHECK_FALSE(bare.tuning.has_value());
    CHECK_FALSE(bare.intensity.has_value());

    const LibraryEntry& minimal = loaded.index.entries.back();
    CHECK(minimal.package_hash.empty());
    CHECK(minimal.thumbnail_file_name.empty());
    CHECK_FALSE(minimal.hasWarnings());
    CHECK(minimal.arrangements.empty());
}

// Verifies a missing index file schedules a rebuild instead of failing (fresh install path).
TEST_CASE("Library index load schedules a rebuild when no file exists", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;

    const LibraryIndexLoadResult loaded = loadLibraryIndex(directory.indexFile());

    CHECK(loaded.rebuild_required);
    CHECK(loaded.index.entries.empty());
    CHECK_FALSE(loaded.rebuild_reason.empty());
}

// Verifies a corrupt index file schedules a rebuild — a broken cache must never block startup.
TEST_CASE("Library index load schedules a rebuild on corrupt content", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;
    writeIndexText(directory.indexFile(), "{ this is not json");

    const LibraryIndexLoadResult loaded = loadLibraryIndex(directory.indexFile());

    CHECK(loaded.rebuild_required);
    CHECK(loaded.index.entries.empty());
}

// Verifies an unsupported index version schedules a rebuild (no migration ladder by design).
TEST_CASE("Library index load schedules a rebuild on a version mismatch", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;
    writeIndexText(directory.indexFile(), R"({ "indexFormatVersion": 999, "entries": [] })");

    const LibraryIndexLoadResult loaded = loadLibraryIndex(directory.indexFile());

    CHECK(loaded.rebuild_required);
}

// Verifies an entry stripped of its identity facts poisons the cache into a rebuild — without
// path/size/mtime the entry cannot participate in change detection.
TEST_CASE("Library index load schedules a rebuild on entries without identity", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;
    writeIndexText(
        directory.indexFile(),
        R"({ "indexFormatVersion": 1, "entries": [ { "metadata": { "title": "No Path" } } ] })");

    const LibraryIndexLoadResult loaded = loadLibraryIndex(directory.indexFile());

    CHECK(loaded.rebuild_required);
    CHECK(loaded.index.entries.empty());
}

// Verifies saving creates missing parent directories (first save on a fresh install).
TEST_CASE("Library index save creates its parent directories", "[core][library]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporaryIndexDirectory directory;
    const std::filesystem::path nested =
        directory.indexFile().parent_path() / "nested" / "index.json";

    REQUIRE(saveLibraryIndex(LibraryIndex{}, nested).has_value());
    const LibraryIndexLoadResult loaded = loadLibraryIndex(nested);

    CHECK_FALSE(loaded.rebuild_required);
    CHECK(loaded.index.entries.empty());
}

} // namespace rock_hero::game::core
