#include "project_loader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/audio_asset.h>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zip.h>

namespace rock_hero::core
{

namespace
{

using Json = nlohmann::json;

struct ParsedArrangement
{
    std::string id;
    Arrangement arrangement;
};

struct ManifestProject
{
    Song song;
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

class ZipSource final
{
public:
    // Takes ownership of a libzip source until it is transferred to an archive.
    explicit ZipSource(zip_source_t* source) noexcept
        : m_source(source)
    {}

    // Frees the source when zip_open_from_source() did not take ownership.
    ~ZipSource()
    {
        if (m_source != nullptr)
        {
            zip_source_free(m_source);
        }
    }

    ZipSource(const ZipSource&) = delete;
    ZipSource& operator=(const ZipSource&) = delete;

    // Transfers the source pointer and clears the moved-from owner.
    ZipSource(ZipSource&& other) noexcept
        : m_source(std::exchange(other.m_source, nullptr))
    {}

    // Releases any old source before taking ownership from another source owner.
    ZipSource& operator=(ZipSource&& other) noexcept
    {
        if (this != &other)
        {
            if (m_source != nullptr)
            {
                zip_source_free(m_source);
            }
            m_source = std::exchange(other.m_source, nullptr);
        }

        return *this;
    }

    // Returns the raw source pointer for libzip calls.
    [[nodiscard]] zip_source_t* get() const noexcept
    {
        return m_source;
    }

    // Transfers ownership to libzip after a successful open-from-source call.
    zip_source_t* release() noexcept
    {
        return std::exchange(m_source, nullptr);
    }

private:
    zip_source_t* m_source{};
};

class ZipArchive final
{
public:
    // Takes ownership of a libzip archive handle.
    explicit ZipArchive(zip_t* archive) noexcept
        : m_archive(archive)
    {}

    // Closes the read-only archive without attempting to write changes.
    ~ZipArchive()
    {
        if (m_archive != nullptr)
        {
            zip_discard(m_archive);
        }
    }

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    // Transfers the archive handle and clears the moved-from owner.
    ZipArchive(ZipArchive&& other) noexcept
        : m_archive(std::exchange(other.m_archive, nullptr))
    {}

    // Releases any old archive before taking ownership from another archive owner.
    ZipArchive& operator=(ZipArchive&& other) noexcept
    {
        if (this != &other)
        {
            if (m_archive != nullptr)
            {
                zip_discard(m_archive);
            }
            m_archive = std::exchange(other.m_archive, nullptr);
        }

        return *this;
    }

    // Returns the raw archive handle for libzip calls.
    [[nodiscard]] zip_t* get() const noexcept
    {
        return m_archive;
    }

private:
    zip_t* m_archive{};
};

class ZipFile final
{
public:
    // Takes ownership of an opened archive entry.
    explicit ZipFile(zip_file_t* file) noexcept
        : m_file(file)
    {}

    // Closes the entry stream when extraction leaves scope.
    ~ZipFile()
    {
        if (m_file != nullptr)
        {
            zip_fclose(m_file);
        }
    }

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;

    // Transfers the entry handle and clears the moved-from owner.
    ZipFile(ZipFile&& other) noexcept
        : m_file(std::exchange(other.m_file, nullptr))
    {}

    // Releases any old entry before taking ownership from another entry owner.
    ZipFile& operator=(ZipFile&& other) noexcept
    {
        if (this != &other)
        {
            if (m_file != nullptr)
            {
                zip_fclose(m_file);
            }
            m_file = std::exchange(other.m_file, nullptr);
        }

        return *this;
    }

    // Returns the raw entry handle for libzip calls.
    [[nodiscard]] zip_file_t* get() const noexcept
    {
        return m_file;
    }

private:
    zip_file_t* m_file{};
};

// Builds a uniform manifest-read failure result.
[[nodiscard]] ManifestLoadResult failManifestLoad(std::string message)
{
    return ManifestLoadResult{
        .project = std::nullopt,
        .error_message = std::move(message),
    };
}

// Builds a failed project-load result with a single message.
[[nodiscard]] ProjectLoadResult failProjectLoad(std::string message)
{
    return ProjectLoadResult{
        .project = std::nullopt,
        .error_message = std::move(message),
    };
}

// Converts a libzip error object into a stable project-owned string.
[[nodiscard]] std::string zipErrorMessage(zip_error_t& error)
{
    const char* message = zip_error_strerror(&error);
    if (message == nullptr)
    {
        return "Unknown zip error";
    }

    return message;
}

// Converts the integer error from zip_open() into a stable project-owned string.
[[nodiscard]] std::string zipErrorMessage(int error_code)
{
    zip_error_t error;
    zip_error_init_with_code(&error, error_code);
    std::string message = zipErrorMessage(error);
    zip_error_fini(&error);
    return message;
}

// Opens an archive through the platform path API while keeping libzip private to core.
[[nodiscard]] std::optional<ZipArchive> openArchive(
    const std::filesystem::path& package_path, std::string& error_message)
{
#if defined(_WIN32)
    zip_error_t error;
    zip_error_init(&error);

    ZipSource source{zip_source_win32w_create(
        package_path.wstring().c_str(), 0, ZIP_LENGTH_TO_END, &error)};
    if (source.get() == nullptr)
    {
        error_message = zipErrorMessage(error);
        zip_error_fini(&error);
        return std::nullopt;
    }

    zip_t* archive = zip_open_from_source(source.get(), ZIP_RDONLY, &error);
    if (archive == nullptr)
    {
        error_message = zipErrorMessage(error);
        zip_error_fini(&error);
        return std::nullopt;
    }

    source.release();
    zip_error_fini(&error);
    return ZipArchive{archive};
#else
    int error_code{};
    zip_t* archive = zip_open(package_path.string().c_str(), ZIP_RDONLY, &error_code);
    if (archive == nullptr)
    {
        error_message = zipErrorMessage(error_code);
        return std::nullopt;
    }

    return ZipArchive{archive};
#endif
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

// Normalizes ZIP path separators without changing the entry name's case.
[[nodiscard]] std::string zipEntryPathName(std::string_view entry_name)
{
    std::string path_name;
    path_name.reserve(entry_name.size());
    for (const char character : entry_name)
    {
        path_name.push_back(character == '\\' ? '/' : character);
    }

    return path_name;
}

// Normalizes ZIP entry names for safety checks and duplicate detection.
[[nodiscard]] std::string normalizedZipEntryName(std::string_view entry_name)
{
    std::string normalized = zipEntryPathName(entry_name);
    for (char& character : normalized)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return normalized;
}

// Rejects archive-relative paths that could escape or ambiguously address a project directory.
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    if (path.string().find(':') != std::string::npos)
    {
        return false;
    }

    return std::ranges::none_of(path, [](const std::filesystem::path& part) {
        return part.empty() || part == "." || part == "..";
    });
}

// Rejects ZIP entry names that could escape or ambiguously address the cache directory.
[[nodiscard]] bool isSafeZipEntryName(std::string_view entry_name)
{
    if (entry_name.empty())
    {
        return false;
    }

    const std::string normalized = normalizedZipEntryName(entry_name);
    if (normalized.empty() || normalized.front() == '/' ||
        normalized.find(':') != std::string::npos)
    {
        return false;
    }

    std::size_t part_start = 0;
    while (part_start < normalized.size())
    {
        const std::size_t part_end = normalized.find('/', part_start);
        const std::string_view part{
            normalized.data() + part_start,
            (part_end == std::string::npos ? normalized.size() : part_end) - part_start
        };

        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }

        if (part_end == std::string::npos)
        {
            break;
        }
        part_start = part_end + 1;
    }

    return true;
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
[[nodiscard]] std::optional<Part> parsePart(const std::string& text)
{
    if (text == "Lead")
    {
        return Part::Lead;
    }

    if (text == "Rhythm")
    {
        return Part::Rhythm;
    }

    if (text == "Bass")
    {
        return Part::Bass;
    }

    return std::nullopt;
}

// Reads song metadata while treating missing descriptive fields as blank draft values.
[[nodiscard]] SongMetadata readMetadata(const Json& manifest)
{
    const auto metadata = manifest.find("metadata");
    if (metadata == manifest.end() || !metadata->is_object())
    {
        return {};
    }

    return SongMetadata{
        .title = metadata->value("title", std::string{}),
        .artist = metadata->value("artist", std::string{}),
        .album = metadata->value("album", std::string{}),
        .year = metadata->value("year", 0),
    };
}

// Reads manifest audio assets into an ID map keyed only inside the loader.
[[nodiscard]] std::optional<std::unordered_map<std::string, AudioAsset>> readAudioAssets(
    const std::filesystem::path& directory, const Json& manifest, std::string& error_message)
{
    const auto audio_assets_json = manifest.find("audioAssets");
    if (audio_assets_json == manifest.end() || !audio_assets_json->is_array() ||
        audio_assets_json->empty())
    {
        error_message = "manifest.json must contain at least one audio asset";
        return std::nullopt;
    }

    std::unordered_map<std::string, AudioAsset> audio_assets;
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
            audio_assets.emplace(*id, AudioAsset{resolved_path->lexically_normal()});
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
    const std::unordered_map<std::string, AudioAsset>& audio_assets, std::string& error_message)
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
                .arrangement = Arrangement{
                    .part = *part,
                    .difficulty = DifficultyRating{},
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

        Song song;
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

// Creates one cache directory under the platform temp directory for a loaded project.
[[nodiscard]] std::optional<LoadedProjectCache> createCacheDirectory(std::string& error_message)
{
    std::error_code error;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        error_message = "Could not locate temporary directory: " + error.message();
        return std::nullopt;
    }

    const std::filesystem::path cache_root = temp_root / "RockHero" / "project-cache";
    std::filesystem::create_directories(cache_root, error);
    if (error)
    {
        error_message = "Could not create project cache root: " + error.message();
        return std::nullopt;
    }

    static std::atomic_uint64_t cache_sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t sequence = cache_sequence.fetch_add(1, std::memory_order_relaxed);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path cache_directory =
            cache_root /
            (std::to_string(now) + "-" + std::to_string(sequence) + "-" + std::to_string(attempt));

        error.clear();
        if (std::filesystem::create_directory(cache_directory, error))
        {
            return LoadedProjectCache{cache_directory};
        }

        if (error)
        {
            error_message = "Could not create project cache directory: " + error.message();
            return std::nullopt;
        }
    }

    error_message = "Could not allocate a unique project cache directory";
    return std::nullopt;
}

// Reports whether a ZIP entry is a directory marker.
[[nodiscard]] bool isDirectoryEntry(std::string_view entry_path_name) noexcept
{
    return !entry_path_name.empty() && entry_path_name.back() == '/';
}

// Reports whether a ZIP entry carries Unix symlink metadata.
[[nodiscard]] bool isSymlinkEntry(zip_t& archive, zip_uint64_t index)
{
    zip_uint8_t operating_system{};
    zip_uint32_t attributes{};
    if (zip_file_get_external_attributes(
            &archive, index, ZIP_FL_UNCHANGED, &operating_system, &attributes) != 0)
    {
        return false;
    }

    constexpr zip_uint8_t unix_operating_system = ZIP_OPSYS_UNIX;
    constexpr zip_uint32_t unix_mode_shift = 16U;
    constexpr zip_uint32_t unix_file_type_mask = 0170000U;
    constexpr zip_uint32_t unix_symlink_file_type = 0120000U;

    return operating_system == unix_operating_system &&
           (((attributes >> unix_mode_shift) & unix_file_type_mask) == unix_symlink_file_type);
}

// Extracts one regular entry stream into the already-resolved output path.
[[nodiscard]] std::optional<std::string> extractFileEntry(
    zip_t& archive, zip_uint64_t index, const std::filesystem::path& output_path)
{
    ZipFile file{zip_fopen_index(&archive, index, ZIP_FL_UNCHANGED)};
    if (file.get() == nullptr)
    {
        return "Could not open project package entry for extraction";
    }

    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error)
    {
        return "Could not create project package output directory: " + error.message();
    }

    std::ofstream output{output_path, std::ios::binary};
    if (!output.is_open())
    {
        return "Could not write project package entry: " + output_path.string();
    }

    std::array<char, 64 * 1024> buffer{};
    while (true)
    {
        const zip_int64_t bytes_read = zip_fread(file.get(), buffer.data(), buffer.size());
        if (bytes_read < 0)
        {
            return "Could not read project package entry";
        }

        if (bytes_read == 0)
        {
            break;
        }

        output.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
        if (!output.good())
        {
            return "Could not write project package entry: " + output_path.string();
        }
    }

    return std::nullopt;
}

// Extracts all safe, regular ZIP entries into a newly-created cache directory.
[[nodiscard]] std::optional<std::string> extractZipToCache(
    zip_t& archive, const std::filesystem::path& cache_directory)
{
    const zip_int64_t entry_count = zip_get_num_entries(&archive, ZIP_FL_UNCHANGED);
    if (entry_count <= 0)
    {
        return "Project package is empty or is not a valid zip archive";
    }

    std::set<std::string> extracted_entries;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entry_count); ++index)
    {
        zip_stat_t entry_stat;
        zip_stat_init(&entry_stat);
        if (zip_stat_index(&archive, index, ZIP_FL_UNCHANGED, &entry_stat) != 0 ||
            entry_stat.name == nullptr)
        {
            return "Could not read project package entry metadata";
        }

        const std::string entry_name{entry_stat.name};
        if (isSymlinkEntry(archive, index) || !isSafeZipEntryName(entry_name))
        {
            return "Project package contains an unsafe entry: " + entry_name;
        }

        const std::string entry_path_name = zipEntryPathName(entry_name);
        const std::string normalized_name = normalizedZipEntryName(entry_name);
        if (isDirectoryEntry(entry_path_name))
        {
            continue;
        }

        if (!extracted_entries.insert(normalized_name).second)
        {
            return "Project package contains a duplicate entry: " + entry_name;
        }

        const std::filesystem::path output_path =
            (cache_directory / std::filesystem::path{entry_path_name}).lexically_normal();
        if (const auto extraction_error = extractFileEntry(archive, index, output_path);
            extraction_error.has_value())
        {
            return extraction_error;
        }
    }

    return std::nullopt;
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
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(package_path, filesystem_error))
    {
        return failProjectLoad("Project package does not exist: " + package_path.string());
    }

    std::string error_message;
    auto archive = openArchive(package_path, error_message);
    if (!archive.has_value())
    {
        return failProjectLoad("Could not open project package: " + error_message);
    }

    auto cache = createCacheDirectory(error_message);
    if (!cache.has_value())
    {
        return failProjectLoad(error_message);
    }

    const auto extraction_error = extractZipToCache(*archive->get(), cache->directory());
    if (extraction_error.has_value())
    {
        return failProjectLoad(*extraction_error);
    }

    ManifestLoadResult loaded_project = readProjectManifest(cache->directory());
    if (!loaded_project.succeeded())
    {
        return failProjectLoad(loaded_project.error_message);
    }

    return ProjectLoadResult{
        .project =
            LoadedProject{
                .song = std::move(loaded_project.project->song),
                .selected_arrangement_index = loaded_project.project->selected_arrangement_index,
                .cache = std::move(*cache),
            },
        .error_message = {},
    };
}

} // namespace rock_hero::core
