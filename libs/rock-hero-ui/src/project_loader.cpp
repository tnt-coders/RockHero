#include "project_loader.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/audio_asset.h>
#include <set>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::ui
{

namespace
{

using Json = nlohmann::json;

struct ParsedArrangement
{
    std::string id;
    core::Arrangement arrangement;
};

struct ManifestProject
{
    core::Song song;
    std::size_t selected_arrangement_index{0};
};

struct ManifestLoadResult
{
    std::optional<ManifestProject> project;
    std::string error_message;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return project.has_value();
    }
};

// Builds a uniform manifest-read failure result.
[[nodiscard]] ManifestLoadResult failManifestLoad(std::string message)
{
    return ManifestLoadResult{
        .project = std::nullopt,
        .error_message = std::move(message),
    };
}

// Converts a standard filesystem path into the JUCE file type used at the archive boundary.
[[nodiscard]] juce::File toJuceFile(const std::filesystem::path& path)
{
    const auto path_text = path.wstring();
    return juce::File{juce::String{path_text.c_str()}};
}

// Converts a JUCE file path into the standard type used by project-owned interfaces.
[[nodiscard]] std::filesystem::path toFilesystemPath(const juce::File& file)
{
    return std::filesystem::path{file.getFullPathName().toWideCharPointer()};
}

// Finds the current manifest name while keeping compatibility with the first generated package.
[[nodiscard]] std::optional<std::filesystem::path> findManifest(
    const std::filesystem::path& directory)
{
    for (const std::filesystem::path manifest_name :
         {std::filesystem::path{"manifest.json"}, std::filesystem::path{"project.json"}})
    {
        const std::filesystem::path manifest_path = directory / manifest_name;
        std::error_code error;
        if (std::filesystem::is_regular_file(manifest_path, error))
        {
            return manifest_path;
        }
    }

    return std::nullopt;
}

// Rejects archive-relative paths that could escape or ambiguously address a project directory.
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    return std::ranges::none_of(path, [](const std::filesystem::path& part) {
        return part.empty() || part == "." || part == "..";
    });
}

// Converts a manifest-relative path into a concrete file path inside the extracted directory.
[[nodiscard]] std::optional<std::filesystem::path> resolveExistingFile(
    const std::filesystem::path& directory, const std::string& relative_path)
{
    const std::filesystem::path path{relative_path};
    if (!isSafeRelativePath(path))
    {
        return std::nullopt;
    }

    const std::filesystem::path resolved_path = (directory / path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }

    return resolved_path;
}

// Reads a required string property from a manifest object.
[[nodiscard]] std::optional<std::string> readRequiredString(
    const Json& object, const char* property_name)
{
    const auto value = object.find(property_name);
    if (value == object.end() || !value->is_string())
    {
        return std::nullopt;
    }

    return value->get<std::string>();
}

// Translates manifest part names into the current core enum.
[[nodiscard]] std::optional<core::Part> parsePart(const std::string& text)
{
    if (text == "Lead")
    {
        return core::Part::Lead;
    }

    if (text == "Rhythm")
    {
        return core::Part::Rhythm;
    }

    if (text == "Bass")
    {
        return core::Part::Bass;
    }

    return std::nullopt;
}

// Reads song metadata while treating missing descriptive fields as blank draft values.
[[nodiscard]] core::SongMetadata readMetadata(const Json& manifest)
{
    const auto metadata = manifest.find("metadata");
    if (metadata == manifest.end() || !metadata->is_object())
    {
        return {};
    }

    return core::SongMetadata{
        .title = metadata->value("title", std::string{}),
        .artist = metadata->value("artist", std::string{}),
        .album = metadata->value("album", std::string{}),
        .year = metadata->value("year", 0),
    };
}

// Reads manifest audio assets into an ID map keyed only inside the loader.
[[nodiscard]] std::optional<std::unordered_map<std::string, core::AudioAsset>> readAudioAssets(
    const std::filesystem::path& directory, const Json& manifest, std::string& error_message)
{
    const auto audio_assets_json = manifest.find("audioAssets");
    if (audio_assets_json == manifest.end() || !audio_assets_json->is_array() ||
        audio_assets_json->empty())
    {
        error_message = "manifest.json must contain at least one audio asset";
        return std::nullopt;
    }

    std::unordered_map<std::string, core::AudioAsset> audio_assets;
    for (const Json& asset_json : *audio_assets_json)
    {
        if (!asset_json.is_object())
        {
            error_message = "audioAssets entries must be objects";
            return std::nullopt;
        }

        const auto id = readRequiredString(asset_json, "id");
        const auto relative_path = readRequiredString(asset_json, "path");
        if (!id.has_value() || id->empty() || !relative_path.has_value())
        {
            error_message = "audio asset entries require non-empty id and path fields";
            return std::nullopt;
        }

        const auto resolved_path = resolveExistingFile(directory, *relative_path);
        if (!resolved_path.has_value())
        {
            error_message = "audio asset path is missing or unsafe: " + *relative_path;
            return std::nullopt;
        }

        const auto inserted =
            audio_assets.emplace(*id, core::AudioAsset{resolved_path->lexically_normal()});
        if (!inserted.second)
        {
            error_message = "duplicate audio asset id: " + *id;
            return std::nullopt;
        }
    }

    return audio_assets;
}

// Reads arrangements and leaves arrangement IDs as loader-only metadata.
[[nodiscard]] std::optional<std::vector<ParsedArrangement>> readArrangements(
    const std::filesystem::path& directory, const Json& manifest,
    const std::unordered_map<std::string, core::AudioAsset>& audio_assets,
    std::string& error_message)
{
    const auto arrangements_json = manifest.find("arrangements");
    if (arrangements_json == manifest.end() || !arrangements_json->is_array() ||
        arrangements_json->empty())
    {
        error_message = "manifest.json must contain at least one arrangement";
        return std::nullopt;
    }

    std::vector<ParsedArrangement> arrangements;
    arrangements.reserve(arrangements_json->size());

    for (const Json& arrangement_json : *arrangements_json)
    {
        if (!arrangement_json.is_object())
        {
            error_message = "arrangement entries must be objects";
            return std::nullopt;
        }

        const auto id = readRequiredString(arrangement_json, "id");
        const auto part_text = readRequiredString(arrangement_json, "part");
        const auto arrangement_file = readRequiredString(arrangement_json, "file");
        const auto audio_id = readRequiredString(arrangement_json, "audio");
        if (!id.has_value() || id->empty() || !part_text.has_value() ||
            !arrangement_file.has_value() || !audio_id.has_value())
        {
            error_message = "arrangement entries require id, part, file, and audio fields";
            return std::nullopt;
        }

        const auto part = parsePart(*part_text);
        if (!part.has_value())
        {
            error_message = "unsupported arrangement part: " + *part_text;
            return std::nullopt;
        }

        if (!resolveExistingFile(directory, *arrangement_file).has_value())
        {
            error_message = "arrangement file is missing or unsafe: " + *arrangement_file;
            return std::nullopt;
        }

        const auto audio_asset = audio_assets.find(*audio_id);
        if (audio_asset == audio_assets.end())
        {
            error_message = "arrangement references unknown audio asset: " + *audio_id;
            return std::nullopt;
        }

        arrangements.push_back(
            ParsedArrangement{
                .id = *id,
                .arrangement = core::Arrangement{
                    .part = *part,
                    .difficulty = core::DifficultyRating{},
                    .audio_asset = audio_asset->second,
                },
            });
    }

    return arrangements;
}

// Finds the selected arrangement index by manifest ID, defaulting to the first arrangement.
[[nodiscard]] std::optional<std::size_t> selectedArrangementIndex(
    const Json& manifest, const std::vector<ParsedArrangement>& arrangements,
    std::string& error_message)
{
    const auto selected = manifest.find("selectedArrangement");
    if (selected == manifest.end() || selected->is_null())
    {
        return 0U;
    }

    if (!selected->is_string())
    {
        error_message = "selectedArrangement must be a string";
        return std::nullopt;
    }

    const std::string selected_id = selected->get<std::string>();
    const auto found = std::ranges::find_if(
        arrangements, [&](const ParsedArrangement& item) { return item.id == selected_id; });

    if (found == arrangements.end())
    {
        error_message = "selectedArrangement references unknown arrangement: " + selected_id;
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(arrangements.begin(), found));
}

// Reads the v1 project manifest and resolves package-relative asset references.
[[nodiscard]] ManifestLoadResult readProjectManifest(const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return failManifestLoad("Project directory does not exist");
    }

    const auto manifest_path = findManifest(directory);
    if (!manifest_path.has_value())
    {
        return failManifestLoad("Project directory does not contain manifest.json or project.json");
    }

    std::ifstream manifest_file{*manifest_path};
    if (!manifest_file.is_open())
    {
        return failManifestLoad("Could not open project manifest");
    }

    Json manifest;
    try
    {
        manifest = Json::parse(manifest_file);
    }
    catch (const Json::parse_error& exception)
    {
        return failManifestLoad(
            std::string{"Could not parse project manifest: "} + exception.what());
    }

    try
    {
        if (!manifest.is_object() || manifest.value("formatVersion", 0) != 1)
        {
            return failManifestLoad("Unsupported project manifest formatVersion");
        }

        std::string error_message;
        const auto audio_assets = readAudioAssets(directory, manifest, error_message);
        if (!audio_assets.has_value())
        {
            return failManifestLoad(error_message);
        }

        const auto arrangements =
            readArrangements(directory, manifest, *audio_assets, error_message);
        if (!arrangements.has_value())
        {
            return failManifestLoad(error_message);
        }

        const auto selected_index =
            selectedArrangementIndex(manifest, *arrangements, error_message);
        if (!selected_index.has_value())
        {
            return failManifestLoad(error_message);
        }

        core::Song song;
        song.metadata = readMetadata(manifest);
        song.chart.arrangements.reserve(arrangements->size());
        for (const ParsedArrangement& parsed_arrangement : *arrangements)
        {
            song.chart.arrangements.push_back(parsed_arrangement.arrangement);
        }

        return ManifestLoadResult{
            .project =
                ManifestProject{
                    .song = std::move(song),
                    .selected_arrangement_index = *selected_index,
                },
            .error_message = {},
        };
    }
    catch (const Json::exception& exception)
    {
        return failManifestLoad(std::string{"Invalid project manifest: "} + exception.what());
    }
}

// Creates a unique project-cache directory under the platform temp directory.
[[nodiscard]] juce::File createCacheDirectory()
{
    const juce::File cache_root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("RockHero")
                                      .getChildFile("project-cache");
    const juce::Uuid uuid;
    return cache_root.getChildFile(uuid.toString());
}

// Rejects ZIP entry names that could escape or ambiguously address the cache directory.
[[nodiscard]] bool isSafeZipEntryName(const juce::String& entry_name)
{
    if (entry_name.isEmpty() || entry_name.startsWithChar('/') || entry_name.startsWithChar('\\') ||
        entry_name.containsChar(':'))
    {
        return false;
    }

    const juce::String normalized = entry_name.replaceCharacter('\\', '/');
    const juce::StringArray parts = juce::StringArray::fromTokens(normalized, "/", "");
    for (const juce::String& part : parts)
    {
        if (part.isEmpty() || part == "." || part == "..")
        {
            return false;
        }
    }

    return true;
}

// Normalizes ZIP paths for duplicate detection before extraction.
[[nodiscard]] juce::String normalizedZipEntryName(const juce::String& entry_name)
{
    return entry_name.replaceCharacter('\\', '/').toLowerCase();
}

// Extracts all safe, regular ZIP entries into a newly-created cache directory.
[[nodiscard]] std::optional<std::string> extractZipToCache(
    juce::ZipFile& zip_file, const juce::File& cache_directory)
{
    if (!cache_directory.createDirectory())
    {
        return std::string{"Could not create project cache directory"};
    }

    if (zip_file.getNumEntries() <= 0)
    {
        return std::string{"Project package is empty or is not a valid zip archive"};
    }

    std::set<juce::String> extracted_entries;
    for (int index = 0; index < zip_file.getNumEntries(); ++index)
    {
        const juce::ZipFile::ZipEntry* entry = zip_file.getEntry(index);
        if (entry == nullptr || entry->filename.isEmpty())
        {
            continue;
        }

        if (entry->isSymbolicLink || !isSafeZipEntryName(entry->filename))
        {
            return std::string{"Project package contains an unsafe entry: "} +
                   entry->filename.toStdString();
        }

        const juce::String normalized_name = normalizedZipEntryName(entry->filename);
        if (!normalized_name.endsWithChar('/') && !normalized_name.endsWithChar('\\') &&
            !extracted_entries.insert(normalized_name).second)
        {
            return std::string{"Project package contains a duplicate entry: "} +
                   entry->filename.toStdString();
        }

        const juce::Result result = zip_file.uncompressEntry(
            index,
            cache_directory,
            juce::ZipFile::OverwriteFiles::no,
            juce::ZipFile::FollowSymlinks::no);
        if (result.failed())
        {
            return result.getErrorMessage().toStdString();
        }
    }

    return std::nullopt;
}

// Builds a failed project-load result with a single message.
[[nodiscard]] ProjectLoadResult failProjectLoad(std::string message)
{
    return ProjectLoadResult{
        .project = std::nullopt,
        .error_message = std::move(message),
    };
}

} // namespace

// Takes ownership of the extracted cache directory.
LoadedProjectCache::LoadedProjectCache(std::filesystem::path directory) noexcept
    : m_directory(std::move(directory))
{}

// Removes the extracted cache when the loaded project leaves scope.
LoadedProjectCache::~LoadedProjectCache()
{
    reset();
}

// Transfers cache ownership and clears the source owner.
LoadedProjectCache::LoadedProjectCache(LoadedProjectCache&& other) noexcept
    : m_directory(std::exchange(other.m_directory, {}))
{}

// Removes the old owned cache before taking ownership from another cache owner.
LoadedProjectCache& LoadedProjectCache::operator=(LoadedProjectCache&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_directory = std::exchange(other.m_directory, {});
    }

    return *this;
}

// Exposes the owned directory for tests and diagnostics.
const std::filesystem::path& LoadedProjectCache::directory() const noexcept
{
    return m_directory;
}

// Best-effort cache removal; project loading must not throw from cleanup.
void LoadedProjectCache::reset() noexcept
{
    if (m_directory.empty())
    {
        return;
    }

    std::error_code error;
    std::filesystem::remove_all(m_directory, error);
    m_directory.clear();
}

// Opens the package archive, extracts it safely, and reads the project manifest.
ProjectLoadResult ProjectLoader::loadProject(const std::filesystem::path& package_path)
{
    const juce::File package_file = toJuceFile(package_path);
    if (!package_file.existsAsFile())
    {
        return failProjectLoad("Project package does not exist: " + package_path.string());
    }

    juce::ZipFile zip_file{package_file};
    const juce::File cache_directory = createCacheDirectory();
    LoadedProjectCache cache{toFilesystemPath(cache_directory)};

    const auto extraction_error = extractZipToCache(zip_file, cache_directory);
    if (extraction_error.has_value())
    {
        return failProjectLoad(*extraction_error);
    }

    ManifestLoadResult loaded_project = readProjectManifest(cache.directory());
    if (!loaded_project.succeeded())
    {
        return failProjectLoad(loaded_project.error_message);
    }

    return ProjectLoadResult{
        .project =
            LoadedProject{
                .song = std::move(loaded_project.project->song),
                .selected_arrangement_index = loaded_project.project->selected_arrangement_index,
                .cache = std::move(cache),
            },
        .error_message = {},
    };
}

} // namespace rock_hero::ui
