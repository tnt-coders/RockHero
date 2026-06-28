#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <rock_hero/common/core/audio_normalization.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/package_id.h>
#include <rock_hero/common/core/rock_song_package.h>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

// Returns the package-relative native arrangement document path for a stable ID.
[[nodiscard]] std::filesystem::path arrangementDocumentPath(std::string_view arrangement_id)
{
    return std::filesystem::path{"arrangements"} / (std::string{arrangement_id} + ".json");
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

// Open-string note names for a standard six-string guitar, used by arrangement-document fixtures.
[[nodiscard]] std::string standardTuningJson()
{
    return R"(["E4", "B3", "G3", "D3", "A2", "E2"])";
}

// Wraps a chart events JSON array body in a minimal valid arrangement document (standard tuning, no
// chord templates), so negative tests can vary only the events.
[[nodiscard]] std::string arrangementDocumentWithEvents(std::string_view events_json)
{
    return R"({"formatVersion":1,"tuning":)" + standardTuningJson() + R"(,"events":)" +
           std::string{events_json} + "}";
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
            .audio_asset = AudioAsset{.path = audio_path, .normalization = std::nullopt},
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .tuning = Tuning{.open_strings = {"E4", "B3", "G3", "D3", "A2", "E2"}},
            .events = {},
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

// Builds a song whose lead arrangement carries a few grid-relative events on common subdivisions: a
// non-sustained single note and a short sustain whose end sits later in the same beat.
[[nodiscard]] Song makeSongWithNotes(const std::filesystem::path& audio_path)
{
    Song song = makeSong(audio_path);
    Arrangement& arrangement = song.arrangements.front();
    arrangement.events = {
        ChartEvent{
            .start = GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}},
            .content = SingleNote{.string_number = 1, .fret = 17},
        },
        ChartEvent{
            .start = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 4}},
            .end = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{3, 8}},
            .content = SingleNote{.string_number = 2, .fret = 5},
        },
    };
    return song;
}

// Writes a minimal package directory fixture that can be edited by negative read tests.
void writeReadablePackageDirectory(const std::filesystem::path& package_directory)
{
    writeAudioFile(package_directory / "audio" / "backing.wav");
    writeTextFile(
        package_directory / arrangementDocumentPath(g_lead_arrangement_id),
        arrangementDocumentWithEvents("[]"));
}

// Writes a package directory whose arrangement document can be varied by negative tests.
void writePackageDirectoryWithArrangementDocument(
    const std::filesystem::path& package_directory, const std::string& arrangement_document)
{
    writeAudioFile(package_directory / "audio" / "backing.wav");
    writeTextFile(
        package_directory / arrangementDocumentPath(g_lead_arrangement_id), arrangement_document);
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
            R"(",
                    "audio": "backing"
                }
            ]
        })");
}

// Writes a readable package whose song.json carries a caller-supplied tempo-map fragment so negative
// tests can vary only the grid. The fragment is the full "tempoMap": { ... }, text spliced ahead of
// audioAssets, matching tempoMapJsonFragment()'s shape; the arrangement document has no notes so only
// the tempo map drives the result.
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
            R"(",
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
            package_directory / arrangementDocumentPath(g_lead_arrangement_id)));

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
    CHECK(
        std::filesystem::is_regular_file(
            package_directory / arrangementDocumentPath(generated_id)));

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
    CHECK_FALSE(
        std::filesystem::exists(
            package_directory / arrangementDocumentPath(g_lead_arrangement_id)));
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
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

// Verifies arrangement tuning and chart events round-trip through native package persistence.
TEST_CASE("Rock song package round-trips arrangement events", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const Song song = makeSongWithNotes(source_audio);
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE(written.has_value());

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    const Arrangement& read_arrangement = read_song->arrangements.front();
    CHECK(read_song->tempo_map == song.tempo_map);
    CHECK(read_arrangement.tuning == song.arrangements.front().tuning);
    CHECK(read_arrangement.events == song.arrangements.front().events);
}

// Locks the writer's readable row layout for time signatures, anchors, and chart events.
TEST_CASE(
    "Rock song package writer formats scan-heavy arrays one object per line",
    "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSongWithNotes(source_audio);
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
    const std::string arrangement_document =
        readTextFile(package_directory / arrangementDocumentPath(g_lead_arrangement_id));
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
    const std::string expected_events =
        R"(    { "start": "1:2+1/2", "string": 1, "fret": 17, "note": "A5" },
    { "start": "2:1+1/4", "end": "2:1+3/8", "string": 2, "fret": 5, "note": "E4" })";

    CHECK(song_document.find(expected_time_signatures) != std::string::npos);
    CHECK(song_document.find(expected_anchors) != std::string::npos);
    CHECK(arrangement_document.find(expected_events) != std::string::npos);
}

// Verifies save-time validation rejects Fraction values mutated into an invalid raw state.
TEST_CASE(
    "Rock song package write rejects invalid in-memory note fractions", "[core][rock-song-package]")
{
    const auto check_invalid_fraction = [](bool invalid_start) {
        const TemporaryRockSongPackageDirectory temporary_directory;
        const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
        writeAudioFile(source_audio);

        Song song = makeSongWithNotes(source_audio);
        if (invalid_start)
        {
            song.arrangements.front().events.front().start.offset.denominator = 0;
        }
        else
        {
            song.arrangements.front().events[1].end->offset.denominator = -8;
        }

        const std::filesystem::path package_directory = temporary_directory.path() / "package";
        const auto written = writeRockSongPackageDirectory(package_directory, song);

        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code == SongPackageErrorCode::InvalidArrangement);
        CHECK(written.error().message.find("denominator") != std::string::npos);
        CHECK_FALSE(
            std::filesystem::exists(
                package_directory / arrangementDocumentPath(g_lead_arrangement_id)));
        CHECK_FALSE(std::filesystem::exists(package_directory / "audio" / "source.wav"));
    };

    check_invalid_fraction(true);
    check_invalid_fraction(false);
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
                    "file": ")" +
            arrangementDocumentPath(g_lead_arrangement_id).generic_string() +
            R"(",
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

// Verifies chord templates and chord events, including per-string deviations, round-trip intact.
TEST_CASE("Rock song package round-trips chord templates", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    Arrangement& arrangement = song.arrangements.front();
    arrangement.chord_templates = {
        ChordTemplate{
            .id = "Am",
            .name = "Am",
            .voicing = {
                ChordVoicingString{.string_number = 1, .fret = 0},
                ChordVoicingString{.string_number = 2, .fret = 1, .finger = 1},
                ChordVoicingString{.string_number = 3, .fret = 2, .finger = 3},
            },
        },
    };
    arrangement.events = {
        ChartEvent{
            .start = GridPosition{.measure = 1, .beat = 1},
            .end = GridPosition{.measure = 1, .beat = 3},
            .content = ChordInstance{
                .template_id = "Am",
                .string_deviations = {
                    ChordStringDeviation{
                        .string_number = 2,
                        .end = GridPosition{.measure = 1, .beat = 2},
                        .techniques = Techniques{.vibrato = true},
                    },
                },
            },
        },
    };

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE(written.has_value());

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().chord_templates == arrangement.chord_templates);
    CHECK(read_song->arrangements.front().events == arrangement.events);
}

// Verifies package writing normalizes chord ids from names and remaps chord-event references.
TEST_CASE("Rock song package write regenerates chord template ids", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path source_audio = temporary_directory.path() / "source.wav";
    writeAudioFile(source_audio);

    Song song = makeSong(source_audio);
    Arrangement& arrangement = song.arrangements.front();
    arrangement.chord_templates = {
        ChordTemplate{
            .id = "internal-first",
            .name = "Am",
            .voicing =
                {
                    ChordVoicingString{.string_number = 1, .fret = 0},
                    ChordVoicingString{.string_number = 2, .fret = 1, .finger = 1},
                },
        },
        ChordTemplate{
            .id = "internal-second",
            .name = "Am",
            .voicing = {
                ChordVoicingString{.string_number = 2, .fret = 1, .finger = 2},
                ChordVoicingString{.string_number = 3, .fret = 2, .finger = 3},
            },
        },
    };
    arrangement.events = {
        ChartEvent{
            .start = GridPosition{.measure = 1, .beat = 1},
            .content = ChordInstance{.template_id = "internal-second"},
        },
        ChartEvent{
            .start = GridPosition{.measure = 1, .beat = 2},
            .content = ChordInstance{.template_id = "internal-first"},
        },
    };

    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    const auto written = writeRockSongPackageDirectory(package_directory, song);

    REQUIRE(written.has_value());

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    const Arrangement& read_arrangement = read_song->arrangements.front();
    REQUIRE(read_arrangement.chord_templates.size() == 2);
    REQUIRE(read_arrangement.events.size() == 2);
    CHECK(read_arrangement.chord_templates[0].id == "Am-2");
    CHECK(read_arrangement.chord_templates[1].id == "Am-1");
    CHECK(std::get<ChordInstance>(read_arrangement.events[0].content).template_id == "Am-1");
    CHECK(std::get<ChordInstance>(read_arrangement.events[1].content).template_id == "Am-2");
}

// Verifies an event that is neither a single note nor a chord is rejected with a typed error.
TEST_CASE("Rock song package rejects malformed arrangement events", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writePackageDirectoryWithArrangementDocument(
        package_directory, arrangementDocumentWithEvents(R"([{"start":"1:2","fret":17}])"));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE_FALSE(read_song.has_value());
    CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
}

// Verifies malformed start/end position tokens are rejected at the read boundary.
TEST_CASE("Rock song package rejects malformed position tokens", "[core][rock-song-package]")
{
    const std::vector<std::string> invalid_event_arrays{
        // Start is not a grid-position token at all.
        R"([{"start":"bad","string":1,"fret":5}])",
        // A zero denominator is not a valid sub-beat fraction.
        R"([{"start":"1:2+1/0","string":1,"fret":5}])",
        // A trailing fraction component leaves unparsed characters.
        R"([{"start":"1:2+1/2/3","string":1,"fret":5}])",
        // End must also be a grid-position token when present.
        R"([{"start":"1:1","end":"nope","string":1,"fret":5}])",
    };

    for (const std::string& events : invalid_event_arrays)
    {
        const TemporaryRockSongPackageDirectory temporary_directory;
        const std::filesystem::path package_directory = temporary_directory.path() / "package";
        writePackageDirectoryWithArrangementDocument(
            package_directory, arrangementDocumentWithEvents(events));

        const auto read_song = readRockSongPackageDirectory(package_directory);

        REQUIRE_FALSE(read_song.has_value());
        CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
    }
}

// Verifies two single notes may share an onset as long as each is on a different string.
TEST_CASE(
    "Rock song package allows same-onset notes on different strings", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writePackageDirectoryWithArrangementDocument(
        package_directory,
        arrangementDocumentWithEvents(
            R"([{"start":"1:2","string":1,"fret":5},{"start":"1:2","string":2,"fret":7}])"));

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(read_song->arrangements.front().events.size() == 2);
}

// Verifies canonical accidental tuning labels are accepted without relying on a string-count preset.
TEST_CASE("Rock song package accepts canonical accidental tuning", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writePackageDirectoryWithArrangementDocument(
        package_directory, R"({"formatVersion":1,"tuning":["F#/Gb1"],"events":[]})");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE(read_song.has_value());
    REQUIRE(read_song->arrangements.size() == 1);
    CHECK(
        read_song->arrangements.front().tuning.open_strings == std::vector<std::string>{"F#/Gb1"});
}

// Verifies invalid and non-canonical tuning note names are rejected with typed arrangement errors.
TEST_CASE("Rock song package rejects invalid tuning notes", "[core][rock-song-package]")
{
    const std::vector<std::string> invalid_arrangement_documents{
        R"({"formatVersion":1,"tuning":["E4","H3"],"events":[]})",
        R"({"formatVersion":1,"tuning":["F#1"],"events":[]})",
        R"({"formatVersion":1,"tuning":["Gb1"],"events":[]})",
    };

    for (const std::string& arrangement_document : invalid_arrangement_documents)
    {
        const TemporaryRockSongPackageDirectory temporary_directory;
        const std::filesystem::path package_directory = temporary_directory.path() / "package";
        writePackageDirectoryWithArrangementDocument(package_directory, arrangement_document);

        const auto read_song = readRockSongPackageDirectory(package_directory);

        REQUIRE_FALSE(read_song.has_value());
        CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
    }
}

// Verifies events whose fields parse but break a domain rule are rejected.
TEST_CASE("Rock song package rejects out-of-domain events", "[core][rock-song-package]")
{
    const std::vector<std::string> invalid_event_arrays{
        // Measure must be positive (rejected by the token parser).
        R"([{"start":"0:1","string":1,"fret":5}])",
        // Beat must fit the active meter.
        R"([{"start":"1:5","string":1,"fret":5}])",
        // Offset must stay below a whole beat.
        R"([{"start":"1:2+1","string":1,"fret":5}])",
        // Offset must reduce within the finest stored subdivision.
        R"([{"start":"1:2+1/2048","string":1,"fret":5}])",
        // String must be present in the tuning.
        R"([{"start":"1:2","string":0,"fret":5}])",
        // Fret must be non-negative.
        R"([{"start":"1:2","string":1,"fret":-1}])",
        // End must come after the start.
        R"([{"start":"2:1","end":"1:1","string":1,"fret":5}])",
        // Two events cannot strike the same string at the same onset.
        R"([{"start":"1:2","string":1,"fret":5},{"start":"1:2","string":1,"fret":7}])",
        // A chord must reference a known template.
        R"([{"start":"1:1","chord":"Nope"}])",
        // A display note label must match the single note's derived pitch.
        R"([{"start":"1:1","string":1,"fret":5,"note":"C4"}])",
    };

    for (const std::string& events : invalid_event_arrays)
    {
        const TemporaryRockSongPackageDirectory temporary_directory;
        const std::filesystem::path package_directory = temporary_directory.path() / "package";
        writePackageDirectoryWithArrangementDocument(
            package_directory, arrangementDocumentWithEvents(events));

        const auto read_song = readRockSongPackageDirectory(package_directory);

        REQUIRE_FALSE(read_song.has_value());
        CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
    }
}

// Verifies chord templates cannot name strings beyond the arrangement's declared tuning.
TEST_CASE("Rock song package rejects chord voicings outside tuning", "[core][rock-song-package]")
{
    const TemporaryRockSongPackageDirectory temporary_directory;
    const std::filesystem::path package_directory = temporary_directory.path() / "package";
    writePackageDirectoryWithArrangementDocument(
        package_directory,
        R"({
            "formatVersion": 1,
            "tuning": ["E4"],
            "chordTemplates": [
                {
                    "id": "Am",
                    "name": "Am",
                    "voicing": [{ "string": 2, "fret": 0 }]
                }
            ],
            "events": [{ "start": "1:1", "chord": "Am" }]
        })");

    const auto read_song = readRockSongPackageDirectory(package_directory);

    REQUIRE_FALSE(read_song.has_value());
    CHECK(read_song.error().code == SongPackageErrorCode::InvalidArrangement);
}

} // namespace rock_hero::common::core
