#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <rock_hero/editor/core/project.h>
#include <rock_hero/editor/core/rock_song_importer.h>
#include <string>
#include <utility>
#include <vector>
#include <zip.h>

namespace rock_hero::editor::core
{

using common::core::Arrangement;
using common::core::AudioAsset;
using common::core::DifficultyRating;
using common::core::Part;
using common::core::Song;
using common::core::TimeDuration;
using common::core::TimePosition;

namespace
{

// Describes one in-memory archive entry written to a zip package fixture.
struct ArchiveEntry
{
    // Path stored inside the test zip archive.
    std::string path;

    // Text payload written to the archive entry.
    std::string contents;
};

// Owns a temporary directory for project and song package test archives.
class TemporaryArchiveDirectory final
{
public:
    // Creates a clean temp directory for one test case.
    TemporaryArchiveDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{"rock-hero-project-test"})
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the temp directory without failing tests on best-effort cleanup errors.
    ~TemporaryArchiveDirectory() noexcept
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

    TemporaryArchiveDirectory(const TemporaryArchiveDirectory&) = delete;
    TemporaryArchiveDirectory& operator=(const TemporaryArchiveDirectory&) = delete;
    TemporaryArchiveDirectory(TemporaryArchiveDirectory&&) = delete;
    TemporaryArchiveDirectory& operator=(TemporaryArchiveDirectory&&) = delete;

    // Returns the temp directory path used by the current test.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Temporary root removed by the destructor after each project package test.
    std::filesystem::path m_path;
};

// Writes a zip-backed test archive from the supplied entries.
void writeArchive(const std::filesystem::path& path, const std::vector<ArchiveEntry>& entries)
{
    int error_code{};
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    REQUIRE(archive != nullptr);

    for (const ArchiveEntry& entry : entries)
    {
        zip_source_t* source =
            zip_source_buffer(archive, entry.contents.data(), entry.contents.size(), 0);
        REQUIRE(source != nullptr);

        if (zip_file_add(archive, entry.path.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
        {
            zip_source_free(source);
            FAIL("Could not add test archive entry");
        }
    }

    REQUIRE(zip_close(archive) == 0);
}

// Reads archive entry names so archive-layout tests can verify the public file contract.
[[nodiscard]] std::vector<std::string> archiveEntryNames(const std::filesystem::path& path)
{
    int error_code{};
    zip_t* archive = zip_open(path.string().c_str(), ZIP_RDONLY, &error_code);
    REQUIRE(archive != nullptr);

    const zip_int64_t entry_count = zip_get_num_entries(archive, 0);
    REQUIRE(entry_count >= 0);

    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(entry_count));
    for (zip_uint64_t index = 0; std::cmp_less(index, entry_count); ++index)
    {
        zip_stat_t stat{};
        REQUIRE(zip_stat_index(archive, index, 0, &stat) == 0);
        REQUIRE(stat.name != nullptr);
        names.emplace_back(stat.name);
    }

    REQUIRE(zip_close(archive) == 0);
    return names;
}

// Reads one text archive entry for project and song serialization assertions.
[[nodiscard]] std::string archiveEntryContents(
    const std::filesystem::path& path, const std::string& entry_name)
{
    int error_code{};
    zip_t* archive = zip_open(path.string().c_str(), ZIP_RDONLY, &error_code);
    REQUIRE(archive != nullptr);

    zip_stat_t stat{};
    REQUIRE(zip_stat(archive, entry_name.c_str(), 0, &stat) == 0);
    std::string contents;
    contents.resize(static_cast<std::size_t>(stat.size));

    zip_file_t* file = zip_fopen(archive, entry_name.c_str(), 0);
    REQUIRE(file != nullptr);
    const zip_int64_t bytes_read = zip_fread(file, contents.data(), contents.size());
    REQUIRE(std::cmp_equal(bytes_read, contents.size()));
    REQUIRE(zip_fclose(file) == 0);
    REQUIRE(zip_close(archive) == 0);
    return contents;
}

// Returns the minimal nested song document shared by project package tests.
[[nodiscard]] ArchiveEntry minimalSongDocumentEntry()
{
    return ArchiveEntry{
        .path = "song/song.json",
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
                        "id": "lead",
                        "part": "Lead",
                        "file": "arrangements/lead.xml",
                        "audio": "backing"
                    }
                ]
            })",
    };
}

// Returns the editor project document shared by project package tests.
[[nodiscard]] ArchiveEntry projectDocumentEntry(
    const std::string& selected_arrangement = "lead", double cursor_position = 0.0)
{
    std::string selected_arrangement_field;
    if (!selected_arrangement.empty())
    {
        selected_arrangement_field =
            R"(,
                    "selectedArrangement": ")" +
            selected_arrangement + R"(")";
    }

    return ArchiveEntry{
        .path = "project.json",
        .contents =
            R"({
                "formatVersion": 1,
                "editorState": {
                    "cursorPosition": )" +
            std::to_string(cursor_position) + selected_arrangement_field +
            R"(
                }
            })",
    };
}

// Returns the flat native song document used by .rock package import tests.
[[nodiscard]] ArchiveEntry minimalNativeSongDocumentEntry()
{
    ArchiveEntry entry = minimalSongDocumentEntry();
    entry.path = "song.json";
    return entry;
}

// Writes a minimal valid .rhp package for project tests.
void writeMinimalProjectPackage(const std::filesystem::path& path)
{
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "song/audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = "song/arrangements/lead.xml",
                .contents = "<Arrangement formatVersion=\"1\" />"
            },
            projectDocumentEntry(),
            minimalSongDocumentEntry(),
        });
}

// Writes a minimal valid .rock package with native song content at the archive root.
void writeMinimalRockSongPackage(const std::filesystem::path& path)
{
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            minimalNativeSongDocumentEntry(),
        });
}

// Writes a valid project package with two arrangements so song ordering can be verified.
void writeTwoArrangementProjectPackage(
    const std::filesystem::path& path, const std::string& song_document_name = "song/song.json",
    const std::string& selected_arrangement = "lead")
{
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "song/audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = "song/arrangements/lead.xml",
                .contents = "<Arrangement formatVersion=\"1\" />"
            },
            ArchiveEntry{
                .path = "song/arrangements/bass.xml",
                .contents = "<Arrangement formatVersion=\"1\" />"
            },
            projectDocumentEntry(selected_arrangement),
            ArchiveEntry{
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
                                    "id": "lead",
                                    "part": "Lead",
                                    "file": "arrangements/lead.xml",
                                    "audio": "backing"
                                },
                                {
                                    "id": "bass",
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
void writeUnsafeAssetProjectPackage(
    const std::filesystem::path& path, const std::string& unsafe_path = "../outside.wav")
{
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "song/audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = "song/arrangements/lead.xml",
                .contents = "<Arrangement formatVersion=\"1\" />"
            },
            projectDocumentEntry(),
            ArchiveEntry{
                .path = "song/song.json",
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
                                "id": "lead",
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

// Verifies a valid .rhp archive extracts to workspace and loads through Project.
TEST_CASE("Project loads a minimal RHP package", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->arrangements.size() == 1);
    CHECK(result->arrangements.front().id == "lead");
    CHECK(result->arrangements.front().part == Part::Lead);
    CHECK(project.editorState().selected_arrangement == std::optional<std::string>{"lead"});
    CHECK(project.editorState().cursor_position == TimePosition{0.0});
    CHECK(project.path() == path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));
}

// Verifies .rock native song packages import into an unsaved editor project workspace.
TEST_CASE("Project imports a native song package", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rock";
    writeMinimalRockSongPackage(path);

    Project project;
    RockSongImporter importer;
    const std::expected<Song, std::string> result = project.import(path, importer);

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->arrangements.size() == 1);
    const Arrangement& arrangement = result->arrangements.front();
    CHECK(arrangement.id == "lead");
    CHECK(arrangement.part == Part::Lead);
    CHECK(
        arrangement.audio_asset.path ==
        (project.workspaceDirectory() / "song" / "audio" / "backing.wav").lexically_normal());
    CHECK(std::filesystem::is_regular_file(arrangement.audio_asset.path));
    CHECK(project.path().empty());
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));
}

// Verifies explicit close reports cleanup success and clears the project context.
TEST_CASE("Project close removes workspace and clears context", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);
    REQUIRE(result.has_value());

    const std::filesystem::path workspace_directory = project.workspaceDirectory();
    REQUIRE(std::filesystem::is_directory(workspace_directory));

    const std::expected<void, std::string> closed = project.close();

    REQUIRE(closed.has_value());
    CHECK(project.path().empty());
    CHECK(project.workspaceDirectory().empty());

    std::error_code error;
    CHECK_FALSE(std::filesystem::exists(workspace_directory, error));
}

// Verifies project package loading enforces project.json at the package root.
TEST_CASE("Project rejects package wrapped in one root directory", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{
                .path = "wrapped/song/audio/backing.wav",
                .contents = "audio",
            },
            ArchiveEntry{
                .path = "wrapped/song/arrangements/lead.xml",
                .contents = "<Arrangement formatVersion=\"1\" />",
            },
            ArchiveEntry{
                .path = "wrapped/project.json",
                .contents = projectDocumentEntry().contents,
            },
            ArchiveEntry{
                .path = "wrapped/song/song.json",
                .contents = minimalSongDocumentEntry().contents,
            },
        });

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("project.json") != std::string::npos);
}

// Verifies the song document reader loads metadata, assets, and arrangements in file order.
TEST_CASE("Project loads arrangements from song.json", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementProjectPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->arrangements.size() == 2);
    CHECK(result->arrangements[0].part == Part::Lead);
    const Arrangement& bass_arrangement = result->arrangements[1];
    CHECK(bass_arrangement.part == Part::Bass);
    CHECK(
        bass_arrangement.audio_asset.path ==
        (project.workspaceDirectory() / "song/audio/backing.wav").lexically_normal());
    CHECK(bass_arrangement.audio_duration == TimeDuration{});
}

// Verifies stale selected-arrangement IDs do not fail project loading.
TEST_CASE("Project loads stale selected arrangement state", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementProjectPackage(path, "song/song.json", "missing");

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    REQUIRE(result.has_value());
    CHECK(project.editorState().selected_arrangement == std::optional<std::string>{"missing"});
}

// Verifies project loading requires the song document under the strict song directory.
TEST_CASE("Project rejects root song.json", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementProjectPackage(path, "song.json");

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("song.json") != std::string::npos);
}

// Verifies project loading rejects path traversal before extracting archive entries.
TEST_CASE("Project rejects unsafe RHP entries", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe.rhp";
    writeArchive(path, std::vector{ArchiveEntry{.path = "../outside.txt", .contents = "bad"}});

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot escape the extracted project directory.
TEST_CASE("Project rejects unsafe asset paths", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetProjectPackage(path);

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot use Windows drive or stream syntax.
TEST_CASE("Project rejects colon-separated asset paths", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetProjectPackage(path, "audio/backing.wav:stream");

    Project project;
    const std::expected<Song, std::string> result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("unsafe") != std::string::npos);
}

// Verifies save persists the session-owned song through the open project package.
TEST_CASE("Project saves session song metadata", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    auto song = project.load(path);
    REQUIRE(song.has_value());
    song->metadata.title = "Updated Title";
    song->metadata.artist = "Updated Artist";
    song->metadata.album = "Updated Album";
    song->metadata.year = 2026;

    const std::expected<void, std::string> saved = project.save(
        *song,
        ProjectEditorState{
            .cursor_position = TimePosition{4.5},
            .selected_arrangement = std::string{"lead"},
        });
    REQUIRE(saved.has_value());

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(path);
    REQUIRE(reloaded_song.has_value());
    CHECK(reloaded_song->metadata.title == "Updated Title");
    CHECK(reloaded_song->metadata.artist == "Updated Artist");
    CHECK(reloaded_song->metadata.album == "Updated Album");
    CHECK(reloaded_song->metadata.year == 2026);
    CHECK(reloaded_project.editorState().cursor_position == TimePosition{4.5});
    CHECK(
        reloaded_project.editorState().selected_arrangement == std::optional<std::string>{"lead"});
}

// Verifies save can import a session audio path that lives outside the extracted workspace.
TEST_CASE("Project save imports external arrangement audio", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    const std::filesystem::path external_audio_path = directory.path() / "replacement.wav";
    writeMinimalProjectPackage(path);
    {
        std::ofstream external_audio{external_audio_path, std::ios::binary};
        external_audio << "replacement bytes";
    }

    Project project;
    auto song = project.load(path);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    song->arrangements.front().audio_asset = AudioAsset{external_audio_path};

    const std::expected<void, std::string> saved = project.save(*song);
    REQUIRE(saved.has_value());

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(path);
    REQUIRE(reloaded_song.has_value());
    REQUIRE(reloaded_song->arrangements.size() == 1);
    const AudioAsset& reloaded_audio_asset = reloaded_song->arrangements.front().audio_asset;
    CHECK(reloaded_audio_asset.path.parent_path().filename() == "audio");
    CHECK(std::filesystem::is_regular_file(reloaded_audio_asset.path));
}

// Verifies saveAs creates an editor project package even before a project package path exists.
TEST_CASE("Project saveAs writes an unopened project", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path project_package_path = directory.path() / "saved.rhp";
    const std::filesystem::path audio_path = directory.path() / "backing.wav";
    {
        std::ofstream audio{audio_path, std::ios::binary};
        audio << "audio bytes";
    }

    Project project;
    const Song song = makeSaveableSong(audio_path);
    const std::expected<void, std::string> saved = project.saveAs(project_package_path, song);

    REQUIRE(saved.has_value());
    CHECK(project.path() == project_package_path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));

    Project reloaded_project;
    const auto reloaded_song = reloaded_project.load(project_package_path);
    REQUIRE(reloaded_song.has_value());
    CHECK(reloaded_song->metadata.title == "Imported Song");
    CHECK(reloaded_song->metadata.artist == "Imported Artist");
    REQUIRE(reloaded_song->arrangements.size() == 1);
    const AudioAsset& reloaded_audio_asset = reloaded_song->arrangements.front().audio_asset;
    CHECK(std::filesystem::is_regular_file(reloaded_audio_asset.path));
}

// Verifies publish writes native song content at the package root without retargeting save.
TEST_CASE("Project publish keeps project path", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path project_path = directory.path() / "song.rhp";
    const std::filesystem::path publish_path = directory.path() / "song.rock";
    writeMinimalProjectPackage(project_path);

    Project project;
    auto song = project.load(project_path);
    REQUIRE(song.has_value());
    song->metadata.title = "Published Title";

    const std::expected<void, std::string> published = project.publish(publish_path, *song);

    REQUIRE(published.has_value());
    CHECK(project.path() == project_path);
    CHECK(std::filesystem::is_regular_file(publish_path));

    const std::vector<std::string> entry_names = archiveEntryNames(publish_path);
    CHECK(std::ranges::find(entry_names, "project.json") == entry_names.end());
    CHECK(std::ranges::find(entry_names, "song/song.json") == entry_names.end());
    CHECK(std::ranges::find(entry_names, "song.json") != entry_names.end());
    CHECK(std::ranges::find(entry_names, "audio/backing.wav") != entry_names.end());
    CHECK(std::ranges::find(entry_names, "arrangements/lead.xml") != entry_names.end());
    CHECK(std::ranges::none_of(entry_names, [](const std::string& name) {
        return name.starts_with("song/");
    }));
    CHECK(
        archiveEntryContents(publish_path, "song.json").find("Published Title") !=
        std::string::npos);
}

// Verifies save has a clear failure when no project package has been opened yet.
TEST_CASE("Project save rejects unopened projects", "[core][project]")
{
    Project project;
    const std::expected<void, std::string> saved = project.save(Song{});

    CHECK_FALSE(saved.has_value());
    CHECK(saved.error().find("path") != std::string::npos);
}

} // namespace rock_hero::editor::core
