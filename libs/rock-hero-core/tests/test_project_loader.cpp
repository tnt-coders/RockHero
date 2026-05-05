#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/project_loader.h>
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

// Owns a temporary directory for package-loader test archives.
class TemporaryPackageDirectory final
{
public:
    // Creates a clean temp directory for one test case.
    TemporaryPackageDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{"rock-hero-package-loader-test"})
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
[[nodiscard]] zip_t* openPackageForWriting(const std::filesystem::path& package_path)
{
    int error_code{};
    zip_t* archive =
        zip_open(package_path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    REQUIRE(archive != nullptr);
    return archive;
}

// Writes a zip-backed .rhp archive from the supplied entries.
void writePackage(
    const std::filesystem::path& package_path, const std::vector<PackageEntry>& entries)
{
    zip_t* archive = openPackageForWriting(package_path);
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

// Returns the minimal manifest shared by package-loader tests.
[[nodiscard]] PackageEntry minimalManifestEntry()
{
    return PackageEntry{
        .path = "manifest.json",
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
                ],
                "selectedArrangement": "lead"
            })",
    };
}

// Writes a minimal valid .rhp package for the loader.
void writeMinimalPackage(const std::filesystem::path& package_path)
{
    writePackage(
        package_path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            minimalManifestEntry(),
        });
}

// Writes a valid package with two arrangements so selectedArrangement can be verified.
void writeTwoArrangementPackage(
    const std::filesystem::path& package_path, const std::string& manifest_name = "manifest.json",
    const std::string& selected_arrangement = "bass")
{
    writePackage(
        package_path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = "arrangements/bass.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = manifest_name,
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
                            ],
                            "selectedArrangement": ")" +
                    selected_arrangement +
                    R"("
                        })"
                },
            },
        });
}

// Writes a package whose manifest asset path should be rejected as unsafe.
void writeUnsafeAssetPackage(
    const std::filesystem::path& package_path, const std::string& unsafe_path = "../outside.wav")
{
    writePackage(
        package_path,
        std::vector{
            PackageEntry{.path = "audio/backing.wav", .contents = "audio bytes"},
            PackageEntry{
                .path = "arrangements/lead.xml", .contents = "<Arrangement formatVersion=\"1\" />"
            },
            PackageEntry{
                .path = "manifest.json",
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
                        ],
                        "selectedArrangement": "lead"
                    })"
            },
        });
}

} // namespace

// Verifies a valid .rhp archive extracts to cache and loads through ProjectLoader.
TEST_CASE("ProjectLoader loads a minimal RHP package", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "song.rhp";
    writeMinimalPackage(package_path);

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    REQUIRE(result.succeeded());
    REQUIRE(result.project.has_value());
    CHECK(result.project->song.metadata.title == "Monument");
    CHECK(result.project->song.metadata.artist == "A Day To Remember");
    REQUIRE(result.project->song.chart.arrangements.size() == 1);
    CHECK(result.project->song.chart.arrangements.front().part == Part::Lead);
    CHECK(result.project->selected_arrangement_index == 0);
    CHECK(std::filesystem::is_directory(result.project->cache.directory()));
}

// Verifies the manifest reader loads metadata, assets, and selected arrangement index.
TEST_CASE("ProjectLoader loads selected arrangement from manifest", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "song.rhp";
    writeTwoArrangementPackage(package_path);

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    REQUIRE(result.succeeded());
    REQUIRE(result.project.has_value());
    CHECK(result.project->song.metadata.title == "Monument");
    CHECK(result.project->song.metadata.artist == "A Day To Remember");
    REQUIRE(result.project->song.chart.arrangements.size() == 2);
    CHECK(result.project->song.chart.arrangements[0].part == Part::Lead);
    CHECK(result.project->song.chart.arrangements[1].part == Part::Bass);
    CHECK(result.project->selected_arrangement_index == 1);
    REQUIRE(result.project->song.chart.arrangements[1].audio_asset.has_value());
    CHECK(
        result.project->song.chart.arrangements[1].audio_asset->path ==
        (result.project->cache.directory() / "audio/backing.wav").lexically_normal());
    CHECK_FALSE(result.project->song.chart.arrangements[1].hasAudio());
}

// Verifies the first generated sample's project.json name remains readable.
TEST_CASE("ProjectLoader accepts the sample project.json name", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "song.rhp";
    writeTwoArrangementPackage(package_path, "project.json", "lead");

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    REQUIRE(result.succeeded());
    CHECK(result.project->selected_arrangement_index == 0);
}

// Verifies the loader rejects path traversal before extracting archive entries.
TEST_CASE("ProjectLoader rejects unsafe RHP entries", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "unsafe.rhp";
    writePackage(
        package_path, std::vector{PackageEntry{.path = "../outside.txt", .contents = "bad"}});

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    CHECK_FALSE(result.succeeded());
    CHECK(result.error_message.find("unsafe") != std::string::npos);
}

// Verifies manifest asset paths cannot escape the extracted project directory.
TEST_CASE("ProjectLoader rejects unsafe asset paths", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetPackage(package_path);

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    CHECK_FALSE(result.succeeded());
    CHECK(result.error_message.find("unsafe") != std::string::npos);
}

// Verifies manifest asset paths cannot use Windows drive or stream syntax.
TEST_CASE("ProjectLoader rejects colon-separated manifest asset paths", "[core][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetPackage(package_path, "audio/backing.wav:stream");

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    CHECK_FALSE(result.succeeded());
    CHECK(result.error_message.find("unsafe") != std::string::npos);
}

} // namespace rock_hero::core
