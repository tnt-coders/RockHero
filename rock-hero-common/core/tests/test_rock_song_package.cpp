#include <catch2/catch_test_macros.hpp>
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
              std::filesystem::path{"rock-hero-rock-song-package-test"})
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
    std::filesystem::path m_path;
};

// Writes a tiny stand-in audio file because package persistence only needs filesystem bytes.
void writeAudioFile(const std::filesystem::path& path)
{
    std::ofstream audio_file{path, std::ios::binary};
    REQUIRE(audio_file.is_open());
    audio_file << "audio";
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
            .tone_timeline_ref = {},
            .note_events = {},
        });
    return song;
}

} // namespace

// Verifies Rock song package directory writing can be read back as shared Song data.
TEST_CASE("Rock song package directory writes native song data", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const std::expected<RockSongPackageWriteResult, std::string> written =
        writeRockSongPackageDirectory(package_directory, makeSong(source_audio));

    REQUIRE(written.has_value());
    REQUIRE(written->arrangement_ids.size() == 1);
    CHECK(written->arrangement_ids.front() == "lead");
    CHECK(std::filesystem::is_regular_file(package_directory / "song.json"));
    CHECK(std::filesystem::is_regular_file(package_directory / "audio" / "source.wav"));
    CHECK(std::filesystem::is_regular_file(package_directory / "arrangements" / "lead.xml"));

    const std::expected<Song, std::string> read_song =
        readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->metadata.title == "Native Song");
    CHECK(read_song->metadata.artist == "Native Artist");
    CHECK(read_song->arrangements.front().id == "lead");
    CHECK(
        read_song->arrangements.front().audio_asset.path == package_directory / "audio/source.wav");
}

} // namespace rock_hero::common::core
