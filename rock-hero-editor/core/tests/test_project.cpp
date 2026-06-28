#include "rock_song_importer.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/audio_normalization.h>
#include <rock_hero/common/core/juce_path.h>
#include <rock_hero/editor/core/project.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

using common::core::Arrangement;
using common::core::AudioAsset;
using common::core::DifficultyRating;
using common::core::Part;
using common::core::Song;
using common::core::TimeDuration;
using common::core::Tuning;

namespace
{

constexpr const char* g_lead_arrangement_id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";
constexpr const char* g_bass_arrangement_id = "7aa55c5a-0e97-4e71-8f74-86b05bb6a2c9";
constexpr const char* g_arrangement_document_contents =
    R"({"formatVersion":1,"tuning":["E4","B3","G3","D3","A2","E2"],"events":[]})";

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
              std::filesystem::path{
                  "rock-hero-project-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
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
    juce::ZipFile::Builder archive_builder;

    for (const ArchiveEntry& entry : entries)
    {
        auto input_stream = std::make_unique<juce::MemoryInputStream>(
            entry.contents.data(), entry.contents.size(), true);
        archive_builder.addEntry(
            // JUCE's ZipFile::Builder takes ownership of the stream pointer.
            input_stream.release(), // NOLINT(cppcoreguidelines-owning-memory)
            9,
            juce::String::fromUTF8(entry.path.c_str()),
            juce::Time::getCurrentTime());
    }

    juce::FileOutputStream output_stream{common::core::juceFileFromPath(path)};
    REQUIRE(output_stream.openedOk());
    REQUIRE(output_stream.setPosition(0));
    REQUIRE(output_stream.truncate().wasOk());
    REQUIRE(archive_builder.writeToStream(output_stream, nullptr));
    output_stream.flush();
    REQUIRE(output_stream.getStatus().wasOk());
}

// Reads archive entry names so archive-layout tests can verify the public file contract.
[[nodiscard]] std::vector<std::string> archiveEntryNames(const std::filesystem::path& path)
{
    const juce::ZipFile archive{common::core::juceFileFromPath(path)};

    const int entry_count = archive.getNumEntries();
    REQUIRE(entry_count >= 0);

    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(entry_count));
    for (int index = 0; index < entry_count; ++index)
    {
        const juce::ZipFile::ZipEntry* const entry = archive.getEntry(index);
        REQUIRE(entry != nullptr);
        names.push_back(entry->filename.toStdString());
    }

    return names;
}

// Reads one text archive entry for project and song serialization assertions.
[[nodiscard]] std::string archiveEntryContents(
    const std::filesystem::path& path, const std::string& entry_name)
{
    juce::ZipFile archive{common::core::juceFileFromPath(path)};
    const int entry_index = archive.getIndexOfFileName(juce::String::fromUTF8(entry_name.c_str()));
    REQUIRE(entry_index >= 0);

    std::unique_ptr<juce::InputStream> input_stream{archive.createStreamForEntry(entry_index)};
    REQUIRE(input_stream != nullptr);
    return input_stream->readEntireStreamAsString().toStdString();
}

// Returns the package-relative native arrangement document path for a stable ID.
[[nodiscard]] std::filesystem::path arrangementDocumentPath(std::string_view arrangement_id)
{
    return std::filesystem::path{"arrangements"} / (std::string{arrangement_id} + ".json");
}

// Returns an arrangement document reference as it appears in song.json.
[[nodiscard]] std::string arrangementDocumentRef(std::string_view arrangement_id)
{
    return arrangementDocumentPath(arrangement_id).generic_string();
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

// Returns the minimal nested song document shared by project package tests.
[[nodiscard]] ArchiveEntry minimalSongDocumentEntry()
{
    return ArchiveEntry{
        .path = "song/song.json",
        .contents =
            R"({
                "formatVersion": 1,)" +
            tempoMapJsonFragment() +
            R"(
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
                        "id": ")" +
            std::string{g_lead_arrangement_id} +
            R"(",
                        "part": "Lead",
                        "file": ")" +
            arrangementDocumentRef(g_lead_arrangement_id) +
            R"(",
                        "audio": "backing"
                    }
                ]
            })",
    };
}

// Returns the editor project document shared by project package tests.
[[nodiscard]] ArchiveEntry projectDocumentEntry(
    const std::string& selected_arrangement = g_lead_arrangement_id)
{
    std::string selected_arrangement_field;
    if (!selected_arrangement.empty())
    {
        selected_arrangement_field =
            R"(
                    "selectedArrangement": ")" +
            selected_arrangement + R"(")";
    }

    return ArchiveEntry{
        .path = "project.json",
        .contents =
            R"({
                "formatVersion": 1,
                "editorState": {)" +
            selected_arrangement_field +
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
                .path = "song/" + arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
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
                .path = arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            minimalNativeSongDocumentEntry(),
        });
}

// Writes a valid project package with two arrangements so song ordering can be verified.
void writeTwoArrangementProjectPackage(
    const std::filesystem::path& path, const std::string& song_document_name = "song/song.json",
    const std::string& selected_arrangement = g_lead_arrangement_id)
{
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "song/audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = "song/" + arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            ArchiveEntry{
                .path = "song/" + arrangementDocumentRef(g_bass_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            projectDocumentEntry(selected_arrangement),
            ArchiveEntry{
                .path = song_document_name,
                .contents = std::string{
                    R"({
                            "formatVersion": 1,)" +
                    tempoMapJsonFragment() +
                    R"(
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
                                    "id": ")" +
                    std::string{g_lead_arrangement_id} +
                    R"(",
                                    "part": "Lead",
                                    "file": ")" +
                    arrangementDocumentRef(g_lead_arrangement_id) +
                    R"(",
                                    "audio": "backing"
                                },
                                {
                                    "id": ")" +
                    std::string{g_bass_arrangement_id} +
                    R"(",
                                    "part": "Bass",
                                    "file": ")" +
                    arrangementDocumentRef(g_bass_arrangement_id) +
                    R"(",
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
                .path = "song/" + arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            projectDocumentEntry(),
            ArchiveEntry{
                .path = "song/song.json",
                .contents =
                    R"({
                        "formatVersion": 1,)" +
                    tempoMapJsonFragment() +
                    R"(
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
                                "id": ")" +
                    std::string{g_lead_arrangement_id} +
                    R"(",
                                "part": "Lead",
                                "file": ")" +
                    arrangementDocumentRef(g_lead_arrangement_id) +
                    R"(",
                                "audio": "backing"
                            }
                        ]
                    })"
            },
        });
}

// Records each invocation of the test-time normalization analyzer so import tests can assert
// that every unique source path is analyzed exactly once.
struct FakeAnalyzeAudioInvocation
{
    std::filesystem::path input;
    common::core::AudioNormalizationTarget target;
};

// Test seam used in place of common::audio::analyzeAudioForGainNormalization so import tests do
// not depend on a real WAV reader, the loudness analyzer, or libebur128. The fake returns a
// synthetic normalization record that round-trips through Project::import the same way a real
// analysis would.
class FakeAnalyzeAudio final
{
public:
    // Builds the std::function seam expected by Project::import. The returned function captures
    // a reference to *this so test cases can inspect invocations after import completes.
    [[nodiscard]] AudioNormalizationAnalyzer function()
    {
        return [this](
                   const std::filesystem::path& input,
                   const common::core::AudioNormalizationTarget& target) {
            return invoke(input, target);
        };
    }

    // Records the input/target tuple and returns a synthetic normalization record.
    [[nodiscard]] std::expected<
        common::core::AudioNormalization, common::audio::AudioNormalizationError>
    invoke(const std::filesystem::path& input, const common::core::AudioNormalizationTarget& target)
    {
        invocations.push_back(
            FakeAnalyzeAudioInvocation{
                .input = input,
                .target = target,
            });

        return common::core::AudioNormalization{
            .gain_db = -4.0,
            .validation_sha256 = std::string(64, 'a'),
        };
    }

    // Captured per-invocation data. Tests read this after Project::import returns.
    std::vector<FakeAnalyzeAudioInvocation> invocations;
};

// Returns a fake analyzer that fails every invocation with the supplied error code so tests can
// verify that Project::import surfaces AudioNormalizationFailed without partially committing the
// imported workspace.
[[nodiscard]] AudioNormalizationAnalyzer makeFailingAnalyzeAudio(
    common::audio::AudioNormalizationErrorCode error_code)
{
    return
        [error_code](const std::filesystem::path&, const common::core::AudioNormalizationTarget&) {
            return std::
                expected<common::core::AudioNormalization, common::audio::AudioNormalizationError>{
                    std::unexpect, common::audio::AudioNormalizationError{error_code}
                };
        };
}

// Creates a song that can be saved into a package from an external audio file.
[[nodiscard]] Song makeSaveableSong(const std::filesystem::path& audio_path)
{
    Song song;
    song.metadata.title = "Imported Song";
    song.metadata.artist = "Imported Artist";
    song.arrangements.push_back(
        Arrangement{
            .id = g_lead_arrangement_id,
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

} // namespace

// Verifies a valid .rhp archive extracts to workspace and loads through Project.
TEST_CASE("Project loads a minimal RHP package", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    FakeAnalyzeAudio fake_analyze;
    const auto result = project.load(path, {}, fake_analyze.function());

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->arrangements.size() == 1);
    CHECK(result->arrangements.front().id == g_lead_arrangement_id);
    CHECK(result->arrangements.front().part == Part::Lead);
    CHECK(
        project.editorState().selected_arrangement ==
        std::optional<std::string>{g_lead_arrangement_id});
    REQUIRE(result->arrangements.front().audio_asset.normalization.has_value());
    if (result->arrangements.front().audio_asset.normalization.has_value())
    {
        CHECK(result->arrangements.front().audio_asset.normalization->gain_db == -4.0);
    }
    CHECK(project.audioNormalizationUpdatedOnLoad());
    CHECK(project.path() == path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));
}

// Verifies project load fails before commit when required normalization analysis fails.
TEST_CASE("Project load surfaces AudioNormalizationFailed", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    const auto result = project.load(
        path,
        common::core::AudioNormalizationTarget{},
        makeFailingAnalyzeAudio(
            common::audio::AudioNormalizationErrorCode::LoudnessMeasurementFailed));

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::AudioNormalizationFailed);
    CHECK(project.workspaceDirectory().empty());
    CHECK_FALSE(project.audioNormalizationUpdatedOnLoad());
}

// Verifies .rock native song packages import into an unsaved editor project workspace.
TEST_CASE("Project imports a native song package", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rock";
    writeMinimalRockSongPackage(path);

    FakeAnalyzeAudio fake_analyze;

    Project project;
    RockSongImporter importer;
    const auto result = project.import(
        path, importer, common::core::AudioNormalizationTarget{}, fake_analyze.function());

    REQUIRE(result.has_value());
    CHECK(result->metadata.title == "Monument");
    CHECK(result->metadata.artist == "A Day To Remember");
    REQUIRE(result->arrangements.size() == 1);
    const Arrangement& arrangement = result->arrangements.front();
    CHECK(arrangement.id == g_lead_arrangement_id);
    CHECK(arrangement.part == Part::Lead);
    CHECK(std::filesystem::is_regular_file(arrangement.audio_asset.path));
    const std::filesystem::path imported_audio_path =
        project.workspaceDirectory() / "song" / "audio" / "backing.wav";
    CHECK(arrangement.audio_asset.path == imported_audio_path);
    CHECK(std::filesystem::exists(imported_audio_path));
    REQUIRE(arrangement.audio_asset.normalization.has_value());
    if (arrangement.audio_asset.normalization.has_value())
    {
        CHECK(arrangement.audio_asset.normalization->gain_db == -4.0);
    }
    CHECK(project.path().empty());
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));
    REQUIRE(fake_analyze.invocations.size() == 1);
}

// Verifies arrangements sharing one source audio path get a single analysis call and identical
// metadata, matching the dedupe contract described in the normalization plan.
TEST_CASE("Project import analyzes each unique source audio once", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rock";
    writeArchive(
        path,
        std::vector{
            ArchiveEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            ArchiveEntry{
                .path = arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            ArchiveEntry{
                .path = arrangementDocumentRef(g_bass_arrangement_id),
                .contents = g_arrangement_document_contents,
            },
            ArchiveEntry{
                .path = "song.json",
                .contents = std::string{
                    R"({
                            "formatVersion": 1,)" +
                    tempoMapJsonFragment() +
                    R"(
                            "metadata": {"title": "x", "artist": "y", "album": "", "year": 0},
                            "audioAssets": [
                                {"id": "backing", "path": "audio/backing.wav"}
                            ],
                            "arrangements": [
                                {"id": ")" +
                    std::string{g_lead_arrangement_id} + R"(", "part": "Lead", "file": ")" +
                    arrangementDocumentRef(g_lead_arrangement_id) +
                    R"(", "audio": "backing"},
                                {"id": ")" +
                    std::string{g_bass_arrangement_id} + R"(", "part": "Bass", "file": ")" +
                    arrangementDocumentRef(g_bass_arrangement_id) +
                    R"(", "audio": "backing"}
                            ]
                        })"
                }
            },
        });

    FakeAnalyzeAudio fake_analyze;
    Project project;
    RockSongImporter importer;
    const auto result = project.import(
        path, importer, common::core::AudioNormalizationTarget{}, fake_analyze.function());

    REQUIRE(result.has_value());
    REQUIRE(result->arrangements.size() == 2);
    CHECK(
        result->arrangements.front().audio_asset.path ==
        result->arrangements.back().audio_asset.path);
    REQUIRE(result->arrangements.front().audio_asset.normalization.has_value());
    CHECK(
        result->arrangements.front().audio_asset.normalization ==
        result->arrangements.back().audio_asset.normalization);
    REQUIRE(fake_analyze.invocations.size() == 1);
}

// Verifies analyzer failures propagate as AudioNormalizationFailed without leaving the previous
// Project context behind.
TEST_CASE("Project import surfaces AudioNormalizationFailed on analysis failure", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rock";
    writeMinimalRockSongPackage(path);

    Project project;
    RockSongImporter importer;
    const auto result = project.import(
        path,
        importer,
        common::core::AudioNormalizationTarget{},
        makeFailingAnalyzeAudio(
            common::audio::AudioNormalizationErrorCode::LoudnessMeasurementFailed));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::AudioNormalizationFailed);
    CHECK(project.workspaceDirectory().empty());
}

// Verifies explicit close reports cleanup success and clears the project context.
TEST_CASE("Project close removes workspace and clears context", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    FakeAnalyzeAudio fake_analyze;
    const auto result = project.load(path, {}, fake_analyze.function());
    REQUIRE(result.has_value());

    const std::filesystem::path workspace_directory = project.workspaceDirectory();
    REQUIRE(std::filesystem::is_directory(workspace_directory));

    const auto closed = project.close();

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
                .path = "wrapped/song/" + arrangementDocumentRef(g_lead_arrangement_id),
                .contents = g_arrangement_document_contents,
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
    FakeAnalyzeAudio fake_analyze;
    const auto result = project.load(path, {}, fake_analyze.function());

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::MissingProjectDocument);
    CHECK(result.error().message.find("project.json") != std::string::npos);
}

// Verifies the song document reader loads metadata, assets, and arrangements in file order.
TEST_CASE("Project loads arrangements from song.json", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeTwoArrangementProjectPackage(path);

    Project project;
    FakeAnalyzeAudio fake_analyze;
    const auto result = project.load(path, {}, fake_analyze.function());

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
    FakeAnalyzeAudio fake_analyze;
    const auto result = project.load(path, {}, fake_analyze.function());

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
    const auto result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::InvalidSongPackage);
    CHECK(result.error().message.find("song.json") != std::string::npos);
}

// Verifies project loading rejects path traversal before extracting archive entries.
TEST_CASE("Project rejects unsafe RHP entries", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe.rhp";
    writeArchive(path, std::vector{ArchiveEntry{.path = "../outside.txt", .contents = "bad"}});

    Project project;
    const auto result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::CouldNotExtractPackage);
    CHECK(result.error().message.find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot escape the extracted project directory.
TEST_CASE("Project rejects unsafe asset paths", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetProjectPackage(path);

    Project project;
    const auto result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::InvalidSongPackage);
    CHECK(result.error().message.find("unsafe") != std::string::npos);
}

// Verifies song-document asset paths cannot use Windows drive or stream syntax.
TEST_CASE("Project rejects colon-separated asset paths", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetProjectPackage(path, "audio/backing.wav:stream");

    Project project;
    const auto result = project.load(path);

    CHECK_FALSE(result.has_value());
    CHECK(result.error().code == ProjectErrorCode::InvalidSongPackage);
    CHECK(result.error().message.find("unsafe") != std::string::npos);
}

// Verifies save persists the session-owned song through the open project package.
TEST_CASE("Project saves session song metadata", "[core][project]")
{
    const TemporaryArchiveDirectory directory;
    const std::filesystem::path path = directory.path() / "song.rhp";
    writeMinimalProjectPackage(path);

    Project project;
    FakeAnalyzeAudio fake_analyze;
    auto song = project.load(path, {}, fake_analyze.function());
    REQUIRE(song.has_value());
    song->metadata.title = "Updated Title";
    song->metadata.artist = "Updated Artist";
    song->metadata.album = "Updated Album";
    song->metadata.year = 2026;

    const auto saved = project.save(
        *song,
        ProjectEditorState{
            .selected_arrangement = std::string{g_lead_arrangement_id},
        });
    REQUIRE(saved.has_value());

    Project reloaded_project;
    FakeAnalyzeAudio reloaded_fake_analyze;
    const auto reloaded_song = reloaded_project.load(path, {}, reloaded_fake_analyze.function());
    REQUIRE(reloaded_song.has_value());
    CHECK(reloaded_song->metadata.title == "Updated Title");
    CHECK(reloaded_song->metadata.artist == "Updated Artist");
    CHECK(reloaded_song->metadata.album == "Updated Album");
    CHECK(reloaded_song->metadata.year == 2026);
    CHECK(
        reloaded_project.editorState().selected_arrangement ==
        std::optional<std::string>{g_lead_arrangement_id});
    CHECK(archiveEntryContents(path, "project.json").find("cursorPosition") == std::string::npos);
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
    FakeAnalyzeAudio fake_analyze;
    auto song = project.load(path, {}, fake_analyze.function());
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    song->arrangements.front().audio_asset =
        AudioAsset{.path = external_audio_path, .normalization = std::nullopt};

    const auto saved = project.save(*song);
    REQUIRE(saved.has_value());

    Project reloaded_project;
    FakeAnalyzeAudio reloaded_fake_analyze;
    const auto reloaded_song = reloaded_project.load(path, {}, reloaded_fake_analyze.function());
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
    const auto saved = project.saveAs(project_package_path, song);

    REQUIRE(saved.has_value());
    CHECK(project.path() == project_package_path);
    CHECK(std::filesystem::is_directory(project.workspaceDirectory()));

    Project reloaded_project;
    FakeAnalyzeAudio fake_analyze;
    const auto reloaded_song =
        reloaded_project.load(project_package_path, {}, fake_analyze.function());
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
    FakeAnalyzeAudio fake_analyze;
    auto song = project.load(project_path, {}, fake_analyze.function());
    REQUIRE(song.has_value());
    song->metadata.title = "Published Title";

    const auto published = project.publish(publish_path, *song);

    REQUIRE(published.has_value());
    CHECK(project.path() == project_path);
    CHECK(std::filesystem::is_regular_file(publish_path));

    const std::vector<std::string> entry_names = archiveEntryNames(publish_path);
    CHECK(std::ranges::find(entry_names, "project.json") == entry_names.end());
    CHECK(std::ranges::find(entry_names, "song/song.json") == entry_names.end());
    CHECK(std::ranges::find(entry_names, "song.json") != entry_names.end());
    CHECK(std::ranges::find(entry_names, "audio/backing.wav") != entry_names.end());
    CHECK(
        std::ranges::find(entry_names, arrangementDocumentRef(g_lead_arrangement_id)) !=
        entry_names.end());
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
    const auto saved = project.save(Song{});

    CHECK_FALSE(saved.has_value());
    CHECK(saved.error().code == ProjectErrorCode::SavePathRequired);
    CHECK(saved.error().message.find("path") != std::string::npos);
}

} // namespace rock_hero::editor::core
