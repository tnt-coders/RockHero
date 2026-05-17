#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <rock_hero/common/core/rock_song_package.h>
#include <string>

namespace rock_hero::common::core
{

namespace
{

// Owns a clean temporary directory for native Rock Hero song package tests.
class TemporaryRockSongPackageDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporaryRockSongPackageDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-rock-song-package-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the test directory on a best-effort basis.
    ~TemporaryRockSongPackageDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        catch (...)
        {
            m_path.clear();
        }
    }

    TemporaryRockSongPackageDirectory(const TemporaryRockSongPackageDirectory&) = delete;
    TemporaryRockSongPackageDirectory& operator=(const TemporaryRockSongPackageDirectory&) = delete;
    TemporaryRockSongPackageDirectory(TemporaryRockSongPackageDirectory&&) = delete;
    TemporaryRockSongPackageDirectory& operator=(TemporaryRockSongPackageDirectory&&) = delete;

    // Returns the temporary root used by this test.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Temporary root removed by the destructor after each test.
    std::filesystem::path m_path;
};

// Writes a small fixture file, creating parent directories for package-relative content.
void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    file << contents;
}

// Writes a tiny stand-in audio file because package persistence only needs filesystem bytes.
void writeAudioFile(const std::filesystem::path& path)
{
    writeTextFile(path, "audio");
}

// Builds the smallest valid native song for package round-trip tests.
[[nodiscard]] Song makeSong(const std::filesystem::path& audio_path)
{
    Song song;
    song.metadata.title = "Native Song";
    song.metadata.artist = "Native Artist";
    song.arrangements.push_back(
        Arrangement{
            .id = "lead",
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset = AudioAsset{audio_path},
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });
    return song;
}

// Builds a native song whose arrangement points at a package-relative tone document.
[[nodiscard]] Song makeSongWithToneDocument(const std::filesystem::path& audio_path)
{
    Song song = makeSong(audio_path);
    song.arrangements.front().tone_document_ref = "tones/lead.tone.json";
    return song;
}

// Writes a minimal package directory fixture that can be edited by negative read tests.
void writeReadablePackageDirectory(const std::filesystem::path& package_directory)
{
    writeAudioFile(package_directory / "audio" / "backing.wav");
    writeTextFile(
        package_directory / "arrangements" / "lead.xml", "<Arrangement formatVersion=\"1\" />");
}

} // namespace

// Verifies Rock song package directory writing can be read back as shared Song data.
TEST_CASE("Rock song package directory writes native song data", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, makeSong(source_audio));

    REQUIRE(written.has_value());
    REQUIRE(written->size() == 1);
    CHECK(written->front() == "lead");
    CHECK(std::filesystem::is_regular_file(package_directory / "song.json"));
    CHECK(std::filesystem::is_regular_file(package_directory / "audio" / "source.wav"));
    CHECK(std::filesystem::is_regular_file(package_directory / "arrangements" / "lead.xml"));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->metadata.title == "Native Song");
    CHECK(read_song->metadata.artist == "Native Artist");
    CHECK(read_song->arrangements.front().id == "lead");
    CHECK(
        read_song->arrangements.front().audio_asset.path == package_directory / "audio/source.wav");
}

// Verifies Rock song package archive writing uses a readable native ZIP package.
TEST_CASE("Rock song package archive round-trips native song data", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_archive = temporary_directory.path() / "song.rock";
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written =
        writeRockSongPackage(package_archive, package_directory, makeSong(source_audio));

    REQUIRE(written.has_value());
    CHECK(std::filesystem::is_regular_file(package_archive));

    const std::filesystem::path extracted_directory = temporary_directory.path() / "extracted";
    const auto read_song = readRockSongPackage(package_archive, extracted_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->metadata.title == "Native Song");
    CHECK(read_song->metadata.artist == "Native Artist");
    CHECK(read_song->arrangements.front().id == "lead");
    CHECK(
        read_song->arrangements.front().audio_asset.path ==
        extracted_directory / "audio/source.wav");
}

// Verifies package directory persistence keeps arrangement tone-document references.
TEST_CASE("Rock song package directory preserves tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeTextFile(package_directory / "tones" / "lead.tone.json", "{}");

    const auto written =
        writeRockSongPackageDirectory(package_directory, makeSongWithToneDocument(source_audio));

    REQUIRE(written.has_value());
    CHECK(std::filesystem::is_regular_file(package_directory / "tones" / "lead.tone.json"));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().tone_document_ref == "tones/lead.tone.json");
}

// Verifies published native archives include tone files and preserve the song reference.
TEST_CASE("Rock song package archive preserves tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_archive = temporary_directory.path() / "song.rock";
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeTextFile(package_directory / "tones" / "lead.tone.json", "{}");

    const auto written = writeRockSongPackage(
        package_archive, package_directory, makeSongWithToneDocument(source_audio));

    REQUIRE(written.has_value());

    const std::filesystem::path extracted_directory = temporary_directory.path() / "extracted";
    const auto read_song = readRockSongPackage(package_archive, extracted_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().tone_document_ref == "tones/lead.tone.json");
    CHECK(std::filesystem::is_regular_file(extracted_directory / "tones" / "lead.tone.json"));
}

// Verifies package writing fails instead of saving dangling tone-document references.
TEST_CASE("Rock song package write rejects missing tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written =
        writeRockSongPackageDirectory(package_directory, makeSongWithToneDocument(source_audio));

    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidSongDocument);
    CHECK(written.error().message.find("tone document") != std::string::npos);
}

// Verifies package loading rejects tone-document paths that cannot resolve inside the package.
TEST_CASE("Rock song package rejects unsafe tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeReadablePackageDirectory(package_directory);
    writeTextFile(
        package_directory / "song.json",
        R"({
            "formatVersion": 1,
            "audioAssets": [
                {
                    "id": "backing",
                    "path": "audio/backing.wav"
                }
            ],
            "arrangements": [
                {
                    "id": "lead",
                    "part": "Lead",
                    "file": "arrangements/lead.xml",
                    "audio": "backing",
                    "toneDocument": "../lead.tone.json"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE_FALSE(read_song.has_value());
    CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
    CHECK(read_song.error().message.find("tone document") != std::string::npos);
}

} // namespace rock_hero::common::core
