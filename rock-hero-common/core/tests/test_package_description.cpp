#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/package/archive_io.h>
#include <rock_hero/common/core/package/package_description.h>
#include <string>
#include <system_error>

namespace rock_hero::common::core
{

namespace
{

// Canonical ids the fixture song document uses (package validation requires UUID shapes).
constexpr const char* g_arrangement_id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";
constexpr const char* g_chart_ref = "charts/7b2d9e10-3c4f-45a8-9d21-e5f6a7b8c9d0.chart.json";

// Test-local temp directory owning one test case's staged package content and archive.
class TemporaryPackageDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporaryPackageDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-package-description-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the test directory on a best-effort basis.
    ~TemporaryPackageDirectory() noexcept
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_path, cleanup_error);
    }

    TemporaryPackageDirectory(const TemporaryPackageDirectory&) = delete;
    TemporaryPackageDirectory(TemporaryPackageDirectory&&) = delete;
    TemporaryPackageDirectory& operator=(const TemporaryPackageDirectory&) = delete;
    TemporaryPackageDirectory& operator=(TemporaryPackageDirectory&&) = delete;

    // Root the test stages package content and builds archives under.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

// Writes one text file, creating parent directories, for staging package entries.
void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary};
    stream << contents;
}

// The peeked fields of a well-formed v1 song document: metadata, one audio asset, and one
// arrangement referencing the chart fixture.
[[nodiscard]] std::string fixtureSongDocument()
{
    return std::string{R"({
  "formatVersion": 1,
  "metadata": { "title": "Peek Song", "artist": "Peek Artist", "album": "Peeks", "year": 2026 },
  "audioAssets": [ { "id": "main", "path": "audio/backing.flac" } ],
  "arrangements": [
    { "id": ")"} +
           g_arrangement_id + R"(", "part": "Lead", "audio": "main", "chart": ")" + g_chart_ref +
           R"(" }
  ]
})";
}

// Renders a real chart document with a distinctive tuning so the peek's summary is assertable.
[[nodiscard]] std::string fixtureChartDocument()
{
    Chart chart;
    chart.tuning.strings = {"D2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    chart.tuning.cent_offset = -6.0;
    return chartDocumentText(chart);
}

// Stages the given entries under a content dir and zips them into a .rock archive.
[[nodiscard]] std::filesystem::path buildArchive(
    const TemporaryPackageDirectory& directory, const std::string& song_document_text,
    const std::string& chart_text, const bool include_audio)
{
    const std::filesystem::path content = directory.path() / "content";
    writeTextFile(content / "song.json", song_document_text);
    if (!chart_text.empty())
    {
        writeTextFile(content / g_chart_ref, chart_text);
    }
    if (include_audio)
    {
        writeTextFile(content / "audio" / "backing.flac", "audio");
    }

    const std::filesystem::path archive_path = directory.path() / "peek.rock";
    REQUIRE(writeWorkspaceToArchive(content, archive_path).has_value());
    return archive_path;
}

} // namespace

// Verifies the happy path: every description field matches the staged package, no extraction.
TEST_CASE("Package description peeks fields from the archive", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path archive =
        buildArchive(directory, fixtureSongDocument(), fixtureChartDocument(), true);

    const auto description = readRockSongPackageDescription(archive);

    REQUIRE(description.has_value());
    CHECK(description->format_version == 1);
    CHECK(description->metadata.title == "Peek Song");
    CHECK(description->metadata.artist == "Peek Artist");
    CHECK(description->metadata.year == 2026);
    CHECK(description->warnings.empty());
    REQUIRE(description->arrangements.size() == 1);
    const ArrangementDescription& arrangement = description->arrangements.front();
    CHECK(arrangement.id == g_arrangement_id);
    CHECK(arrangement.part == std::optional{Part::Lead});
    CHECK(arrangement.chart_ref == g_chart_ref);
    CHECK(arrangement.audio_asset_present);
    REQUIRE(arrangement.tuning.has_value());
    if (arrangement.tuning.has_value())
    {
        CHECK(arrangement.tuning->strings.size() == 6);
        CHECK(arrangement.tuning->strings.front() == "D2");
        CHECK(arrangement.tuning->capo == 2);
        CHECK(arrangement.tuning->cent_offset == Catch::Approx(-6.0));
    }
}

// Verifies a corrupt chart entry degrades to a warning, never a hard failure.
TEST_CASE("Package description warns on a corrupt chart", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path archive =
        buildArchive(directory, fixtureSongDocument(), "this is not a chart document", true);

    const auto description = readRockSongPackageDescription(archive);

    REQUIRE(description.has_value());
    REQUIRE(description->arrangements.size() == 1);
    CHECK_FALSE(description->arrangements.front().tuning.has_value());
    REQUIRE_FALSE(description->warnings.empty());
    CHECK(description->warnings.front().find(g_chart_ref) != std::string::npos);
}

// Verifies a missing audio entry reads as absent with a warning, not a failure.
TEST_CASE("Package description reports missing audio entries", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path archive =
        buildArchive(directory, fixtureSongDocument(), fixtureChartDocument(), false);

    const auto description = readRockSongPackageDescription(archive);

    REQUIRE(description.has_value());
    REQUIRE(description->arrangements.size() == 1);
    CHECK_FALSE(description->arrangements.front().audio_asset_present);
    CHECK_FALSE(description->warnings.empty());
}

// Verifies a ZIP without song.json is a typed error, not an empty description.
TEST_CASE("Package description rejects non-package archives", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path content = directory.path() / "content";
    writeTextFile(content / "readme.txt", "not a song package");
    const std::filesystem::path archive_path = directory.path() / "not-a-package.rock";
    REQUIRE(writeWorkspaceToArchive(content, archive_path).has_value());

    const auto description = readRockSongPackageDescription(archive_path);

    REQUIRE_FALSE(description.has_value());
    CHECK(description.error().code == SongPackageErrorCode::MissingSongDocument);
}

// Verifies the single named version gate rejects unsupported format versions.
TEST_CASE("Package description rejects unsupported versions", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::string v2_document = R"({ "formatVersion": 2, "arrangements": [] })";
    const std::filesystem::path archive = buildArchive(directory, v2_document, "", false);

    const auto description = readRockSongPackageDescription(archive);

    REQUIRE_FALSE(description.has_value());
    CHECK(description.error().code == SongPackageErrorCode::InvalidSongDocument);
}

// Verifies a file that is not an archive at all is a typed error.
TEST_CASE("Package description rejects non-archives", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path not_an_archive = directory.path() / "plain.rock";
    writeTextFile(not_an_archive, "just text");

    const auto description = readRockSongPackageDescription(not_an_archive);

    REQUIRE_FALSE(description.has_value());
    CHECK(description.error().code == SongPackageErrorCode::CouldNotExtractPackage);
}

// Verifies an unsupported part token degrades to an absent part with a warning, never a failure.
TEST_CASE("Package description warns on an unsupported part", "[core][package-description]")
{
    const TemporaryPackageDirectory directory;
    const std::string document = std::string{R"({
  "formatVersion": 1,
  "metadata": { "title": "Peek Song" },
  "audioAssets": [ { "id": "main", "path": "audio/backing.flac" } ],
  "arrangements": [
    { "id": ")"} + g_arrangement_id +
                                 R"(", "part": "Keytar", "audio": "main", "chart": ")" +
                                 g_chart_ref + R"(" }
  ]
})";
    const std::filesystem::path archive =
        buildArchive(directory, document, fixtureChartDocument(), true);

    const auto description = readRockSongPackageDescription(archive);

    REQUIRE(description.has_value());
    REQUIRE(description->arrangements.size() == 1);
    CHECK_FALSE(description->arrangements.front().part.has_value());
    const bool warned_about_part =
        std::ranges::any_of(description->warnings, [](const std::string& warning) {
            return warning.find("Keytar") != std::string::npos;
        });
    CHECK(warned_about_part);
}

} // namespace rock_hero::common::core
