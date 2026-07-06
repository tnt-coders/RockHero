#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/song/audio_normalization.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

constexpr std::string_view g_lead_arrangement_id{"4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4"};
constexpr std::string_view g_tone_id{"9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d"};
constexpr std::string_view g_verse_region_id{"5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e"};
constexpr std::string_view g_chorus_region_id{"c9d8e7f6-a5b4-4c3d-9e2f-1a0b9c8d7e6f"};

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

// Reads a written fixture file back as text so tests can assert exact persisted formatting.
[[nodiscard]] std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
}

// Writes a tiny stand-in audio file because package persistence only needs filesystem bytes.
void writeAudioFile(const std::filesystem::path& path)
{
    writeTextFile(path, "audio");
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

// Returns the required native tempo-map document fragment shared by package fixtures.
[[nodiscard]] std::string tempoMapJsonFragment()
{
    return R"(
            "tempoMap": {
                "timeSignatures": [
                    { "measure": 1, "numerator": 4, "denominator": 4 }
                ],
                "anchors": [
                    { "position": "1:1", "seconds": 0.000 },
                    { "position": "3:1", "seconds": 4.000 }
                ]
            },
)";
}

// Builds the smallest valid native song for package round-trip tests.
[[nodiscard]] Song makeSong(const std::filesystem::path& audio_path)
{
    Song song;
    song.metadata.title = "Native Song";
    song.metadata.artist = "Native Artist";
    song.tempo_map = TempoMap::defaultMap(TimeDuration{4.0});
    song.arrangements.push_back(
        Arrangement{
            .id = std::string{g_lead_arrangement_id},
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset =
                AudioAsset{.path = audio_path, .normalization = std::nullopt, .start_offset = {}},
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
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
}

// Writes a readable package whose song.json carries a caller-supplied tempo-map fragment so negative
// tests can vary only the grid. The fragment is the full "tempoMap": { ... } text spliced ahead of
// audioAssets, matching tempoMapJsonFragment()'s shape.
void writePackageDirectoryWithTempoMap(
    const std::filesystem::path& package_directory, const std::string& tempo_map_fragment)
{
    writeReadablePackageDirectory(package_directory);
    writeTextFile(
        package_directory / "song.json",
        R"({
            "formatVersion": 1,)" +
            tempo_map_fragment +
            R"(
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
                    "audio": "backing"
                }
            ]
        })");
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
    CHECK(isCanonicalChartDocumentRef("charts/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d.chart.json"));
    CHECK_FALSE(isCanonicalChartDocumentRef("charts/lead.chart.json"));
    CHECK_FALSE(
        isCanonicalChartDocumentRef("chart/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d.chart.json"));
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

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->metadata.title == "Native Song");
    CHECK(read_song->metadata.artist == "Native Artist");
    CHECK(read_song->tempo_map == TempoMap::defaultMap(TimeDuration{4.0}));
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
    CHECK_FALSE(std::filesystem::exists(package_directory / "audio" / "source.wav"));
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
    CHECK_FALSE(std::filesystem::exists(package_directory / "audio" / "source.wav"));
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
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
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

// Verifies a backing audio start offset (a recording that begins after the score's first beat)
// round-trips through the package format.
TEST_CASE("Rock song package round-trips audio start offset", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.arrangements.front().audio_asset.start_offset = TimeDuration{0.75};

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    REQUIRE(writeRockSongPackageDirectory(package_directory, song).has_value());

    const auto read_song = readRockSongPackageDirectory(package_directory);
    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().audio_asset.start_offset.seconds == 0.75);
}

// Verifies the persisted "startOffset" key loads, and that packages omitting it (every package
// written before the field existed) default the offset to zero.
TEST_CASE("Rock song package reads an explicit audio start offset", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writeReadablePackageDirectory(package_directory);
    writeTextFile(
        package_directory / "song.json",
        R"({
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
            "audioAssets": [
                {
                    "id": "backing",
                    "path": "audio/backing.wav",
                    "startOffset": 0.5
                }
            ],
            "arrangements": [
                {
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().audio_asset.start_offset.seconds == 0.5);
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
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
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
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
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
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
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
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK_FALSE(read_song->arrangements.front().audio_asset.normalization.has_value());
}

// Locks the writer's readable row layout for time signatures and anchors.
TEST_CASE(
    "Rock song package writer formats scan-heavy arrays one object per line",
    "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.tempo_map = TempoMap{
        std::vector{
            TimeSignatureChange{
                .measure = 1,
                .numerator = 4,
                .denominator = 4,
            },
            TimeSignatureChange{
                .measure = 12,
                .numerator = 7,
                .denominator = 8,
            },
        },
        std::vector{
            BeatAnchor{
                .measure = 1,
                .beat = 1,
                .seconds = 0.0,
            },
            BeatAnchor{
                .measure = 3,
                .beat = 1,
                .seconds = 4.0,
            },
            BeatAnchor{
                .measure = 13,
                .beat = 1,
                .seconds = 19.25,
            },
        },
    };

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE(written.has_value());

    const std::string song_document = readTextFile(package_directory / "song.json");
    const std::string expected_time_signatures =
        R"(    "timeSignatures": [
      { "measure": 1, "numerator": 4, "denominator": 4 },
      { "measure": 12, "numerator": 7, "denominator": 8 }
    ])";
    const std::string expected_anchors =
        R"(    "anchors": [
      { "position": "1:1", "seconds": 0.000 },
      { "position": "3:1", "seconds": 4.000 },
      { "position": "13:1", "seconds": 19.250 }
    ])";

    CHECK(song_document.find(expected_time_signatures) != std::string::npos);
    CHECK(song_document.find(expected_anchors) != std::string::npos);
    CHECK(song_document.find(R"("file")") == std::string::npos);
}

// Verifies format version 1 still requires the native tempo-map object.
TEST_CASE("Rock song package requires tempo map", "[core][rock-song-package]")
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
                    "audio": "backing"
                }
            ]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE_FALSE(read_song.has_value());
    CHECK(read_song.error().code == SongPackageErrorCode::InvalidSongDocument);
    CHECK(read_song.error().message.find("tempoMap") != std::string::npos);
}

// Verifies the reader rejects tempo maps that break grid invariants. Each fragment parses and passes
// the per-entry field reads, so these exercise validateTempoMap's structural rules: meter coverage
// and ordering, the required start and terminal anchors, downbeat terminals, and anchor ordering.
TEST_CASE("Rock song package rejects malformed tempo maps", "[core][rock-song-package]")
{
    const std::vector<std::string> invalid_tempo_maps{
        // timeSignatures must contain at least one meter.
        R"(
            "tempoMap": { "timeSignatures": [], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.000 } ] },
)",
        // timeSignatures must start at measure 1.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 2, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.000 } ] },
)",
        // Denominators must be a power of two.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 3 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.000 } ] },
)",
        // timeSignatures measures must be strictly increasing.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 }, { "measure": 1, "numerator": 3, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.000 } ] },
)",
        // anchors must contain both a start and a terminal anchor.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 } ] },
)",
        // The first anchor must address measure 1 beat 1.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:2", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.000 } ] },
)",
        // The terminal anchor must land on a downbeat.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "2:2", "seconds": 4.000 } ] },
)",
        // anchor seconds must be strictly increasing.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 0.000 } ] },
)",
        // anchor seconds must already be on the persisted three-decimal grid.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "3:1", "seconds": 4.0004 } ] },
)",
        // An anchor beat must fit inside its measure's active meter.
        R"(
            "tempoMap": { "timeSignatures": [ { "measure": 1, "numerator": 4, "denominator": 4 } ], "anchors": [ { "position": "1:1", "seconds": 0.000 }, { "position": "1:5", "seconds": 2.000 } ] },
)",
    };

    for (const std::string& tempo_map_fragment : invalid_tempo_maps)
    {
        const TemporaryRockSongPackageDirectory temporary_directory;
        const std::filesystem::path package_directory = temporary_directory.path() / "package";
        writePackageDirectoryWithTempoMap(package_directory, tempo_map_fragment);

        const auto read_song = readRockSongPackageDirectory(package_directory);

        REQUIRE_FALSE(read_song.has_value());
        CHECK(read_song.error().code == SongPackageErrorCode::InvalidSongDocument);
        CHECK(read_song.error().message.find("tempoMap") != std::string::npos);
    }
}

TEST_CASE("Rock song package round-trips authored tone regions", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");

    Song song = makeSongWithToneDocument(source_audio);
    song.arrangements.front().tone_track.regions = {
        ToneRegion{
            .id = std::string{g_verse_region_id},
            .name = "Clean Verse",
            .start = ToneGridPosition{.measure = 1, .beat = 1},
            .end = ToneGridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = toneDocumentRef(),
        },
        ToneRegion{
            .id = std::string{g_chorus_region_id},
            .name = std::string{},
            .start = ToneGridPosition{.measure = 2, .beat = 3},
            .end = ToneGridPosition{.measure = 3, .beat = 1},
            .tone_document_ref = toneDocumentRef(),
        },
    };

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE(written.has_value());

    const auto loaded = readRockSongPackageDirectory(package_directory);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->arrangements.size() == 1);
    CHECK(loaded->arrangements.front().tone_track == song.arrangements.front().tone_track);
}

TEST_CASE("Rock song package write rejects overlapping tone regions", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");

    Song song = makeSongWithToneDocument(source_audio);
    song.arrangements.front().tone_track.regions = {
        ToneRegion{
            .id = std::string{g_verse_region_id},
            .name = "Clean Verse",
            .start = ToneGridPosition{.measure = 1, .beat = 1},
            .end = ToneGridPosition{.measure = 2, .beat = 3},
            .tone_document_ref = toneDocumentRef(),
        },
        ToneRegion{
            .id = std::string{g_chorus_region_id},
            .name = "Crunch Chorus",
            .start = ToneGridPosition{.measure = 2, .beat = 1},
            .end = ToneGridPosition{.measure = 3, .beat = 1},
            .tone_document_ref = toneDocumentRef(),
        },
    };

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidArrangement);
}

TEST_CASE(
    "Rock song package write rejects tone regions past the terminal anchor",
    "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");

    Song song = makeSongWithToneDocument(source_audio);
    song.arrangements.front().tone_track.regions = {
        ToneRegion{
            .id = std::string{g_verse_region_id},
            .name = "Clean Verse",
            .start = ToneGridPosition{.measure = 1, .beat = 1},
            .end = ToneGridPosition{.measure = 9, .beat = 1},
            .tone_document_ref = toneDocumentRef(),
        },
    };

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidArrangement);
}

TEST_CASE(
    "Rock song package read rejects malformed tone region tokens", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    writeReadablePackageDirectory(package_directory);
    writeTextFile(package_directory / toneDocumentPath(g_tone_id), "{}");
    writeTextFile(
        package_directory / "song.json",
        R"({
            "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
            "audioAssets": [
                { "id": "backing", "path": "audio/backing.wav" }
            ],
            "arrangements": [
                {
                    "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                    "part": "Lead",
                    "audio": "backing",
                    "toneTrack": { "regions": [
                        { "id": ")" +
            std::string{g_verse_region_id} +
            R"(", "start": "1:1+1/2", "end": "2:1", "toneDocument": ")" + toneDocumentRef() +
            R"(" }
                    ] }
                }
            ]
        })");

    const auto loaded = readRockSongPackageDirectory(package_directory);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == SongPackageErrorCode::InvalidArrangement);
}

TEST_CASE("Rock song package round-trips a chart reference", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);

    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 3,
            .fret = 5,
            .sustain = Fraction{1, 2},
            .bend = {},
            .slides = {},
        },
    };
    const std::string chart_ref = "charts/" + std::string{g_lead_arrangement_id} + ".chart.json";
    REQUIRE(writeChartDocument(package_directory / chart_ref, chart).has_value());

    Song song = makeSong(source_audio);
    song.arrangements.front().chart_ref = chart_ref;

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE(written.has_value());

    const auto loaded = readRockSongPackageDirectory(package_directory);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->arrangements.size() == 1);
    CHECK(loaded->arrangements.front().chart_ref == chart_ref);
    REQUIRE(loaded->arrangements.front().chart.has_value());
    if (loaded->arrangements.front().chart.has_value())
    {
        CHECK(*loaded->arrangements.front().chart == chart);
    }
}

TEST_CASE("Rock song package write rejects missing chart documents", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.arrangements.front().chart_ref =
        "charts/" + std::string{g_lead_arrangement_id} + ".chart.json";

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidSongDocument);
}

TEST_CASE(
    "Rock song package read rejects charts that violate chart rules", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);

    // Beat 9 does not exist in the fixture's 4/4 grid.
    Chart invalid_chart;
    invalid_chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    invalid_chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 9},
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
    };
    const std::string chart_ref = "charts/" + std::string{g_lead_arrangement_id} + ".chart.json";

    Song song = makeSong(source_audio);
    song.arrangements.front().chart_ref = chart_ref;
    // Write the package first with a valid chart, then corrupt the chart file in place so the
    // failure exercises the read-side rule validation.
    Chart valid_chart = invalid_chart;
    valid_chart.notes[0].position.beat = 1;
    REQUIRE(writeChartDocument(package_directory / chart_ref, valid_chart).has_value());
    REQUIRE(writeRockSongPackageDirectory(package_directory, song).has_value());
    REQUIRE(writeChartDocument(package_directory / chart_ref, invalid_chart).has_value());

    const auto loaded = readRockSongPackageDirectory(package_directory);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == SongPackageErrorCode::InvalidArrangement);
}

TEST_CASE(
    "Rock song package write rejects missing tone region documents", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temp;
    const std::filesystem::path package_directory = temp.path() / "package";
    const std::filesystem::path source_audio = package_directory / "audio" / "backing.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    song.arrangements.front().tone_track.regions = {
        ToneRegion{
            .id = std::string{g_verse_region_id},
            .name = "Clean Verse",
            .start = ToneGridPosition{.measure = 1, .beat = 1},
            .end = ToneGridPosition{.measure = 2, .beat = 1},
            .tone_document_ref = toneDocumentRef(),
        },
    };

    const auto written = writeRockSongPackageDirectory(package_directory, song);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == SongPackageErrorCode::InvalidSongDocument);
}

} // namespace rock_hero::common::core
