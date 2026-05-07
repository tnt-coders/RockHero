#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <fstream>
#include <rock_hero/core/project.h>
#include <string>
#include <utility>
#include <vector>
#include <zip.h>

namespace rock_hero::core
{

namespace
{

struct PackageEntry
{
    std::string path;
    std::string contents;
};

// Owns a temporary directory for project package test archives.
class TemporaryPackageDirectory final
{
public:
    // Creates a clean temp directory for one test case.
    TemporaryPackageDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{"rock-hero-project-test"})
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the temp directory without failing tests on best-effort cleanup errors.
    ~TemporaryPackageDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryPackageDirectory(const TemporaryPackageDirectory&) = delete;
    TemporaryPackageDirectory& operator=(const TemporaryPackageDirectory&) = delete;
    TemporaryPackageDirectory(TemporaryPackageDirectory&&) = delete;
    TemporaryPackageDirectory& operator=(TemporaryPackageDirectory&&) = delete;

    // Returns the temp directory path used by the current test.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

// Opens a package archive for writing through libzip.
[[nodiscard]] zip_t* openPackageForWriting(const std::filesystem::path& path)
{
    int error_code{};
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    REQUIRE(archive != nullptr);
    return archive;
}

// Writes a zip-backed .rhp archive from the supplied entries.
void writePackage(const std::filesystem::path& path, const std::vector<PackageEntry>& entries)
{
    zip_t* archive = openPackageForWriting(path);
    for (const PackageEntry& entry : entries)
    {
        zip_source_t* source =
            zip_source_buffer(archive, entry.contents.data(), entry.contents.size(), 0);
        REQUIRE(source != nullptr);

        if (zip_file_add(archive, entry.path.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
        {
            zip_source_free(source);
            FAIL("Could not add test package entry");
        }
    }

    REQUIRE(zip_close(archive) == 0);
}

// Returns the minimal song document shared by project package tests.
[[nodiscard]] PackageEntry minimalSongDocumentEntry()
{
    return PackageEntry{
        .path = "song.json",
        .contents =
            R"({
                "formatVersion": 1,
                "metadata": {
                    "title": "Monument",
                    "artist": "A Day To Remember"
                },
                "audioAssets": [
                    {
                        "id": "backing",
                        "path": "audio/backing.wav"
                    }
                ],
                "arrangements": [
                    {
                        "part": "Lead",
                        "file": "arrangements/lead.xml",
                        "audio": "backing"
                    }
                ]
            })",
    };
}

// Writes a minimal valid .rhp package for project tests.
void writeMinimalPackage(const std::filesystem::path& path)
{
    writePackage(
        path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            minimalSongDocumentEntry(),
        });
}

// Writes a valid package with two arrangements so song-document ordering can be verified.
void writeTwoArrangementPackage(
    const std::filesystem::path& path, const std::string& song_document_name = "song.json")
{
    writePackage(
        path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = "arrangements/bass.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = song_document_name,
                .contents = std::string{
                    R"({
                            "formatVersion": 1,
                            "metadata": {
                                "title": "Monument",
                                "artist": "A Day To Remember",
                                "album": "",
                                "year": 0
                            },
                            "audioAssets": [
                                {
                                    "id": "backing",
                                    "path": "audio/backing.wav"
                                }
                            ],
                            "arrangements": [
                                {
                                    "part": "Lead",
                                    "file": "arrangements/lead.xml",
                                    "audio": "backing"
                                },
                                {
                                    "part": "Bass",
                                    "file": "arrangements/bass.xml",
                                    "audio": "backing"
                                }
                            ]
                        })"
                },
            },
        });
}

// Writes a package whose song-document asset path should be rejected as unsafe.
void writeUnsafeAssetPackage(
    const std::filesystem::path& path, const std::string& unsafe_path = "../outside.wav")
{
    writePackage(
        path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = "song.json",
                .contents =
                    R"({
                        "formatVersion": 1,
                        "audioAssets": [
                            {
                                "id": "backing",
                                "path": ")" +
                    unsafe_path +
                    R"("
                            }
                        ],
                        "arrangements": [
                            {
                                "part": "Lead",
                                "file": "arrangements/lead.xml",
                                "audio": "backing"
                            }
                        ]
                    })"
            },
        });
}

// Creates a song that can be saved into a package from an external audio file.
[[nodiscard]] Song makeSaveableSong(const std::filesystem::path& audio_path)
{
    Song song;
    song.metadata.title = "Imported Song";
    song.metadata.artist = "Imported Artist";
    song.chart.arrangements.push_back(
        Arrangement{
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

// Verifies a valid .rhp archive extracts to workspace and loads through Project.
TEST_CASE("Project loads a minimal RHP package", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->chart.arrangements.size() == 1);
    CHECK(result->chart.arrangements.front().part == Part::Lead);
    CHECK(project.path() == path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));
}

// Verifies package loading enforces song.json at the package root.
TEST_CASE("Project rejects package wrapped in one root directory", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writePackage(
        path,
        std::vector{
            PackageEntry{
                .path = "wrapped/audio/backing.wav",
                .contents = "audio",
            },
            PackageEntry{
                .path = "wrapped/arrangements/lead.xml",
                .contents = "<Arrangement formatVersion=\"1\" />",
            },
            PackageEntry{
                .path = "wrapped/song.json",
                .contents = minimalSongDocumentEntry().contents,
            },
        });

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("song.json") != std::string::npos);
}

// Verifies the song document reader loads metadata, assets, and arrangements in file order.
TEST_CASE("Project loads arrangements from song.json", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->chart.arrangements.size() == 2);
    CHECK(result->chart.arrangements[0].part == Part::Lead);
    const Arrangement& bass_arrangement = result->chart.arrangements[1];
    CHECK(bass_arrangement.part == Part::Bass);
    REQUIRE(bass_arrangement.audio_asset.has_value());
    if (bass_arrangement.audio_asset.has_value())
    {
        CHECK(
            bass_arrangement.audio_asset.value().path ==
            (project.workspaceDirectory() / "audio/backing.wav").lexically_normal());
    }
    CHECK_FALSE(bass_arrangement.hasAudio());
}

// Verifies project loading does not accept the old project.json song-document name.
TEST_CASE("Project rejects project.json", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementPackage(path, "project.json");

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("song.json") != std::string::npos);
}

// Verifies project loading rejects path traversal before extracting archive entries.
TEST_CASE("Project rejects unsafe RHP entries", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe.rhp";
    writePackage(path, std::vector{PackageEntry{.path = "../outside.txt", .contents = "bad"}});

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot escape the extracted project directory.
TEST_CASE("Project rejects unsafe asset paths", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot use Windows drive or stream syntax.
TEST_CASE("Project rejects colon-separated asset paths", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetPackage(path, "audio/backing.wav:stream");

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies save persists the session-owned song through the open project package.
TEST_CASE("Project saves session song metadata", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalPackage(path);

    Project project;
    auto song = project.load(path);
    REQUIRE(song.has_value());
    song->metadata.title = "Updated Title";
    song->metadata.artist = "Updated Artist";
    song->metadata.album = "Updated Album";
    song->metadata.year = 2026;

    const std::expected<void, std::string> saved = project.save(*song);
    REQUIRE(saved.has_value());

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(path);
    REQUIRE(reloaded_song.has_value());
    CHECK(reloaded_song->metadata.title == "Updated Title");
    CHECK(reloaded_song->metadata.artist == "Updated Artist");
    CHECK(reloaded_song->metadata.album == "Updated Album");
    CHECK(reloaded_song->metadata.year == 2026);
}

// Verifies save can import a session audio path that lives outside the extracted workspace.
TEST_CASE("Project save imports external arrangement audio", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    const std::filesystem::path external_audio_path = directory.path() / "replacement.wav";
    writeMinimalPackage(path);
    {
        std::ofstream external_audio{external_audio_path, std::ios::binary};
        external_audio << "replacement bytes";
    }

    Project project;
    auto song = project.load(path);
    REQUIRE(song.has_value());
    REQUIRE(song->chart.arrangements.size() == 1);
    song->chart.arrangements.front().audio_asset = AudioAsset{external_audio_path};

    const std::expected<void, std::string> saved = project.save(*song);
    REQUIRE(saved.has_value());

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(path);
    REQUIRE(reloaded_song.has_value());
    REQUIRE(reloaded_song->chart.arrangements.size() == 1);
    REQUIRE(reloaded_song->chart.arrangements.front().audio_asset.has_value());
    if (reloaded_song->chart.arrangements.front().audio_asset.has_value())
    {
        const std::filesystem::path reloaded_audio_path =
            reloaded_song->chart.arrangements.front().audio_asset.value().path;
        CHECK(reloaded_audio_path.parent_path().filename() == "audio");
        CHECK(std::filesystem::is_regular_file(reloaded_audio_path));
    }
}

// Verifies saveAs creates a native project package even before a package path exists.
TEST_CASE("Project saveAs writes an unopened project", "[core][project]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "saved.rhp";
    const std::filesystem::path audio_path = directory.path() / "backing.wav";
    {
        std::ofstream audio{audio_path, std::ios::binary};
        audio << "audio bytes";
    }

    Project project;
    const Song song = makeSaveableSong(audio_path);
    const std::expected<void, std::string> saved = project.saveAs(package_path, song);

    REQUIRE(saved.has_value());
    CHECK(project.path() == package_path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(package_path);
    REQUIRE(reloaded_song.has_value());
    CHECK(reloaded_song->metadata.title == "Imported Song");
    CHECK(reloaded_song->metadata.artist == "Imported Artist");
    REQUIRE(reloaded_song->chart.arrangements.size() == 1);
    REQUIRE(reloaded_song->chart.arrangements.front().audio_asset.has_value());
    if (reloaded_song->chart.arrangements.front().audio_asset.has_value())
    {
        CHECK(
            std::filesystem::is_regular_file(
                reloaded_song->chart.arrangements.front().audio_asset.value().path));
    }
}

// Verifies save has a clear failure when no package has been opened yet.
TEST_CASE("Project save rejects unopened projects", "[core][project]")
{
    Project project;
    const std::expected<void, std::string> saved = project.save(Song{});

    CHECK_FALSE(saved.has_value());
    CHECK(saved.error().find("path") != std::string::npos);
}

} // namespace rock_hero::core
