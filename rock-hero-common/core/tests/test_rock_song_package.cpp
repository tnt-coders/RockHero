#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <rock_hero/common/core/audio_normalization.h>
#include <rock_hero/common/core/package_id.h>
#include <rock_hero/common/core/rock_song_package.h>
#include <string>
#include <string_view>

namespace rock_hero::common::core
{

namespace
{

constexpr std::string_view g_lead_arrangement_id{"4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4"};
constexpr std::string_view g_tone_id{"9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d"};

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

// Returns the package-relative arrangement file path for a stable arrangement ID.
[[nodiscard]] std::filesystem::path arrangementFilePath(std::string_view arrangement_id)
{
    return std::filesystem::path{"arrangements"} / (std::string{arrangement_id} + ".xml");
}

// Returns the package-relative tone document path for a stable tone ID.
[[nodiscard]] std::filesystem::path toneDocumentPath(std::string_view tone_id)
{
    return std::filesystem::path{"tones"} / std::string{tone_id} / "tone.json";
}

// Returns the package-relative tone document reference stored in song.json.
[[nodiscard]] std::string toneDocumentRef()
{
    return toneDocumentPath(g_tone_id).generic_string();
}

// Builds the smallest valid native song for package round-trip tests.
[[nodiscard]] Song makeSong(const std::filesystem::path& audio_path)
{
    Song song;
    song.metadata.title = "Native Song";
    song.metadata.artist = "Native Artist";
    song.arrangements.push_back(
        Arrangement{
            .id = std::string{g_lead_arrangement_id},
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset = AudioAsset{.path = audio_path, .normalization = std::nullopt},
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
    song.arrangements.front().tone_document_ref = toneDocumentRef();
    return song;
}

// Builds a representative normalization record used by round-trip tests.
[[nodiscard]] AudioNormalization makeNormalization()
{
    return AudioNormalization{
        .gain_db = -0.25,
        .validation_sha256 = std::string(64, 'a'),
    };
}

// Builds a song whose backing audio carries persisted normalization metadata.
[[nodiscard]] Song makeSongWithNormalization(const std::filesystem::path& audio_path)
{
    Song song = makeSong(audio_path);
    song.arrangements.front().audio_asset.normalization = makeNormalization();
    return song;
}

// Writes a minimal package directory fixture that can be edited by negative read tests.
void writeReadablePackageDirectory(const std::filesystem::path& package_directory)
{
    writeAudioFile(package_directory / "audio" / "backing.wav");
    writeTextFile(
        package_directory / arrangementFilePath(g_lead_arrangement_id),
        "<Arrangement formatVersion=\"1\" />");
}

} // namespace

// Verifies package IDs use the canonical UUIDv4 spelling persisted in song packages.
TEST_CASE("Package IDs use canonical UUIDv4 text", "[core][rock-song-package]")
{
    const std::string generated_id = generatePackageId();

    CHECK(isCanonicalPackageId(generated_id));
    CHECK(isCanonicalPackageId(g_lead_arrangement_id));
    CHECK(toneDocumentRefForToneId(g_tone_id) == toneDocumentRef());
    CHECK(isCanonicalToneDocumentRef(toneDocumentRef()));
    CHECK_FALSE(isCanonicalPackageId("lead"));
    CHECK_FALSE(isCanonicalPackageId("4f3a1c5e9d2b48a6b1f0c7e8d9a2b3c4"));
    CHECK_FALSE(isCanonicalPackageId("4f3a1c5e-9d2b-58a6-b1f0-c7e8d9a2b3c4"));
    CHECK_FALSE(isCanonicalPackageId("4F3A1C5E-9D2B-48A6-B1F0-C7E8D9A2B3C4"));
    CHECK_FALSE(isCanonicalToneDocumentRef("tones/lead.tone.json"));
}

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
    CHECK(written->front() == std::string{g_lead_arrangement_id});
    CHECK(std::filesystem::is_regular_file(package_directory / "song.json"));
    CHECK(std::filesystem::is_regular_file(package_directory / "audio" / "source.wav"));
    CHECK(
        std::filesystem::is_regular_file(
            package_directory / arrangementFilePath(g_lead_arrangement_id)));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->metadata.title == "Native Song");
    CHECK(read_song->metadata.artist == "Native Artist");
    CHECK(read_song->arrangements.front().id == std::string{g_lead_arrangement_id});
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
    CHECK(read_song->arrangements.front().id == std::string{g_lead_arrangement_id});
    CHECK(
        read_song->arrangements.front().audio_asset.path ==
        extracted_directory / "audio/source.wav");
}

// Verifies empty arrangement IDs are generated as canonical UUIDv4 package IDs.
TEST_CASE("Rock song package directory generates arrangement IDs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.arrangements.front().id.clear();

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE(written.has_value());
    REQUIRE(written->size() == 1);
    const std::string& generated_id = written->front();
    CHECK(isCanonicalPackageId(generated_id));
    CHECK(std::filesystem::is_regular_file(package_directory / arrangementFilePath(generated_id)));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().id == generated_id);
}

// Verifies package writing rejects legacy or user-facing arrangement names as durable IDs.
TEST_CASE("Rock song package write rejects non-UUID arrangement IDs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.arrangements.front().id = "lead";

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidSongDocument);
    CHECK(written.error().message.find("arrangement id") != std::string::npos);
}

// Verifies package directory persistence keeps arrangement tone-document references.
TEST_CASE("Rock song package directory preserves tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");

    const auto written =
        writeRockSongPackageDirectory(package_directory, makeSongWithToneDocument(source_audio));

    REQUIRE(written.has_value());
    CHECK(std::filesystem::is_regular_file(package_directory / toneDocumentPath(g_tone_id)));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().tone_document_ref == toneDocumentRef());
}

// Verifies published native archives include tone files and preserve the song reference.
TEST_CASE("Rock song package archive preserves tone refs", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_archive = temporary_directory.path() / "song.rock";
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");

    const auto written = writeRockSongPackage(
        package_archive, package_directory, makeSongWithToneDocument(source_audio));

    REQUIRE(written.has_value());

    const std::filesystem::path extracted_directory = temporary_directory.path() / "extracted";
    const auto read_song = readRockSongPackage(package_archive, extracted_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().tone_document_ref == toneDocumentRef());
    CHECK(std::filesystem::is_regular_file(extracted_directory / toneDocumentPath(g_tone_id)));
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
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "file": ")" +
            arrangementFilePath(g_lead_arrangement_id).generic_string() +
            R"(",
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

// Verifies songs whose backing audio carries normalization metadata round-trip every persisted
// field.
TEST_CASE("Rock song package round-trips normalization metadata", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written =
        writeRockSongPackageDirectory(package_directory, makeSongWithNormalization(source_audio));

    REQUIRE(written.has_value());

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    const auto& normalization = read_song->arrangements.front().audio_asset.normalization;
    REQUIRE(normalization.has_value());
    CHECK(*normalization == makeNormalization());
}

// Verifies older packages whose audio entries omit normalization still load with empty optional.
TEST_CASE("Rock song package without normalization still loads", "[core][rock-song-package]")
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
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "file": ")" +
            arrangementFilePath(g_lead_arrangement_id).generic_string() +
            R"(",
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK_FALSE(read_song->arrangements.front().audio_asset.normalization.has_value());
}

// Verifies an explicit JSON null normalization field loads identically to an omitted field.
TEST_CASE(
    "Rock song package treats explicit null normalization as absent", "[core][rock-song-package]")
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
                    "path": "audio/backing.wav",
                    "normalization": null
                }
            ],
            "arrangements": [
                {
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "file": ")" +
            arrangementFilePath(g_lead_arrangement_id).generic_string() +
            R"(",
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK_FALSE(read_song->arrangements.front().audio_asset.normalization.has_value());
}

// Verifies incomplete normalization records load as absent so open-time analysis can repair them.
TEST_CASE(
    "Rock song package treats incomplete normalization as absent", "[core][rock-song-package]")
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
                    "path": "audio/backing.wav",
                    "normalization": {
                        "gainDb": -4.0
                    }
                }
            ],
            "arrangements": [
                {
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "file": ")" +
            arrangementFilePath(g_lead_arrangement_id).generic_string() +
            R"(",
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK_FALSE(read_song->arrangements.front().audio_asset.normalization.has_value());
}

} // namespace rock_hero::common::core
