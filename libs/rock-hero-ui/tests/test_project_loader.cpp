#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <rock_hero/ui/project_loader.h>
#include <string>

namespace rock_hero::ui
{

namespace
{

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

// Converts a filesystem path into the JUCE file type used by ZipFile::Builder output.
[[nodiscard]] juce::File toJuceFile(const std::filesystem::path& path)
{
    const auto path_text = path.wstring();
    return juce::File{juce::String{path_text.c_str()}};
}

// Adds a UTF-8 text entry to a package builder.
void addTextEntry(juce::ZipFile::Builder& builder, const juce::String& path, std::string contents)
{
    builder.addEntry(
        new juce::MemoryInputStream(contents.data(), contents.size(), true),
        0,
        path,
        juce::Time::getCurrentTime());
}

// Writes a zip-backed .rhp archive from the supplied builder.
void writePackage(const std::filesystem::path& package_path, const juce::ZipFile::Builder& builder)
{
    juce::FileOutputStream output{toJuceFile(package_path)};
    REQUIRE(output.openedOk());
    REQUIRE(builder.writeToStream(output, nullptr));
}

// Writes a minimal valid .rhp package for the loader.
void writeMinimalPackage(const std::filesystem::path& package_path)
{
    juce::ZipFile::Builder builder;
    addTextEntry(builder, "audio/backing.wav", "audio bytes");
    addTextEntry(builder, "arrangements/lead.xml", "<Arrangement formatVersion=\"1\" />");
    addTextEntry(
        builder,
        "manifest.json",
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
        })");

    writePackage(package_path, builder);
}

// Writes a valid package with two arrangements so selectedArrangement can be verified.
void writeTwoArrangementPackage(
    const std::filesystem::path& package_path, const std::string& manifest_name = "manifest.json",
    const std::string& selected_arrangement = "bass")
{
    juce::ZipFile::Builder builder;
    addTextEntry(builder, "audio/backing.wav", "audio bytes");
    addTextEntry(builder, "arrangements/lead.xml", "<Arrangement formatVersion=\"1\" />");
    addTextEntry(builder, "arrangements/bass.xml", "<Arrangement formatVersion=\"1\" />");
    addTextEntry(
        builder,
        manifest_name,
        std::string{
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
        });

    writePackage(package_path, builder);
}

// Writes a package whose manifest asset path tries to escape the extracted directory.
void writeUnsafeAssetPackage(const std::filesystem::path& package_path)
{
    juce::ZipFile::Builder builder;
    addTextEntry(builder, "arrangements/lead.xml", "<Arrangement formatVersion=\"1\" />");
    addTextEntry(
        builder,
        "manifest.json",
        R"({
            "formatVersion": 1,
            "audioAssets": [
                {
                    "id": "backing",
                    "path": "../outside.wav"
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
        })");

    writePackage(package_path, builder);
}

} // namespace

// Verifies a valid .rhp archive extracts to cache and loads through ProjectLoader.
TEST_CASE("ProjectLoader loads a minimal RHP package", "[ui][project-loader]")
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
    CHECK(result.project->song.chart.arrangements.front().part == core::Part::Lead);
    CHECK(result.project->selected_arrangement_index == 0);
    CHECK(std::filesystem::is_directory(result.project->cache.directory()));
}

// Verifies the manifest reader loads metadata, assets, and selected arrangement index.
TEST_CASE("ProjectLoader loads selected arrangement from manifest", "[ui][project-loader]")
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
    CHECK(result.project->song.chart.arrangements[0].part == core::Part::Lead);
    CHECK(result.project->song.chart.arrangements[1].part == core::Part::Bass);
    CHECK(result.project->selected_arrangement_index == 1);
    REQUIRE(result.project->song.chart.arrangements[1].audio_asset.has_value());
    CHECK(
        result.project->song.chart.arrangements[1].audio_asset->path ==
        (result.project->cache.directory() / "audio/backing.wav").lexically_normal());
    CHECK_FALSE(result.project->song.chart.arrangements[1].hasAudio());
}

// Verifies the first generated sample's project.json name remains readable.
TEST_CASE("ProjectLoader accepts the sample project.json name", "[ui][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "song.rhp";
    writeTwoArrangementPackage(package_path, "project.json", "lead");

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    REQUIRE(result.succeeded());
    CHECK(result.project->selected_arrangement_index == 0);
}

// Verifies the loader rejects path traversal before extracting archive entries.
TEST_CASE("ProjectLoader rejects unsafe RHP entries", "[ui][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "unsafe.rhp";
    juce::ZipFile::Builder builder;
    addTextEntry(builder, "../outside.txt", "bad");
    writePackage(package_path, builder);

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    CHECK_FALSE(result.succeeded());
    CHECK(result.error_message.find("unsafe") != std::string::npos);
}

// Verifies manifest asset paths cannot escape the extracted project directory.
TEST_CASE("ProjectLoader rejects unsafe asset paths", "[ui][project-loader]")
{
    const TemporaryPackageDirectory directory;
    const std::filesystem::path package_path = directory.path() / "unsafe-asset.rhp";
    writeUnsafeAssetPackage(package_path);

    const ProjectLoadResult result = ProjectLoader{}.loadProject(package_path);

    CHECK_FALSE(result.succeeded());
    CHECK(result.error_message.find("unsafe") != std::string::npos);
}

} // namespace rock_hero::ui
