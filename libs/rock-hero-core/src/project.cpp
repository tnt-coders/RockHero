#include "project.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
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

    // Releases ownership after libzip closes the archive successfully.
    zip_t* release() noexcept
    {
        return std::exchange(m_archive, nullptr);
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

// Builds a uniform song read failure result.
[[nodiscard]] std::expected<Song, std::string> failSongLoad(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-load result with a single message.
[[nodiscard]] std::expected<Song, std::string> failProjectLoad(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-import result with a single message.
[[nodiscard]] std::expected<Song, std::string> failProjectImport(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-save result with a single message.
[[nodiscard]] std::expected<void, std::string> failProjectSave(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
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

// Opens an archive for writing through the platform path API while keeping libzip private.
[[nodiscard]] std::optional<ZipArchive> openArchiveForWriting(
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

    zip_t* archive = zip_open_from_source(source.get(), ZIP_CREATE | ZIP_TRUNCATE, &error);
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
    zip_t* archive =
        zip_open(package_path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error_code);
    if (archive == nullptr)
    {
        error_message = zipErrorMessage(error_code);
        return std::nullopt;
    }

    return ZipArchive{archive};
#endif
}

// Finds the required editable song document in an extracted project directory.
[[nodiscard]] std::optional<std::filesystem::path> findSongDocument(
    const std::filesystem::path& directory)
{
    const std::filesystem::path song_document_path = directory / "song.json";
    std::error_code error;
    if (std::filesystem::is_regular_file(song_document_path, error))
    {
        return song_document_path;
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

// Rejects ZIP entry names that could escape or ambiguously address the workspace directory.
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

// Converts a package-relative path into a concrete file path inside the extracted directory.
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

// Reads a required string property from a song-document object.
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

// Translates song-document part names into the current core enum.
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
[[nodiscard]] SongMetadata readMetadata(const Json& song_document)
{
    const auto metadata = song_document.find("metadata");
    if (metadata == song_document.end() || !metadata->is_object())
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

// Reads song audio assets into an ID map keyed only inside project IO.
[[nodiscard]] std::optional<std::unordered_map<std::string, AudioAsset>> readAudioAssets(
    const std::filesystem::path& directory, const Json& song_document, std::string& error_message)
{
    const auto audio_assets_json = song_document.find("audioAssets");
    if (audio_assets_json == song_document.end() || !audio_assets_json->is_array() ||
        audio_assets_json->empty())
    {
        error_message = "song.json must contain at least one audio asset";
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

// Reads arrangements from song-document entries into framework-free core values.
[[nodiscard]] std::optional<std::vector<Arrangement>> readArrangements(
    const std::filesystem::path& directory, const Json& song_document,
    const std::unordered_map<std::string, AudioAsset>& audio_assets, std::string& error_message)
{
    const auto arrangements_json = song_document.find("arrangements");
    if (arrangements_json == song_document.end() || !arrangements_json->is_array() ||
        arrangements_json->empty())
    {
        error_message = "song.json must contain at least one arrangement";
        return std::nullopt;
    }

    std::vector<Arrangement> arrangements;
    arrangements.reserve(arrangements_json->size());

    for (const Json& arrangement_json : *arrangements_json)
    {
        if (!arrangement_json.is_object())
        {
            error_message = "arrangement entries must be objects";
            return std::nullopt;
        }

        const auto part_text = readRequiredString(arrangement_json, "part");
        const auto arrangement_file = readRequiredString(arrangement_json, "file");
        const auto audio_id = readRequiredString(arrangement_json, "audio");
        if (!part_text.has_value() || !arrangement_file.has_value() || !audio_id.has_value())
        {
            error_message = "arrangement entries require part, file, and audio fields";
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
            Arrangement{
                .part = *part,
                .difficulty = DifficultyRating{},
                .audio_asset = audio_asset->second,
            });
    }

    return arrangements;
}

// Reads song.json and resolves package-relative asset references into core data.
[[nodiscard]] std::expected<Song, std::string> readSong(const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return failSongLoad("Project directory does not exist");
    }

    const auto song_document_path = findSongDocument(directory);
    if (!song_document_path.has_value())
    {
        return failSongLoad("Project directory does not contain song.json");
    }

    std::ifstream song_document_file{*song_document_path};
    if (!song_document_file.is_open())
    {
        return failSongLoad("Could not open song.json");
    }

    Json song_document;
    try
    {
        song_document = Json::parse(song_document_file);
    }
    catch (const Json::parse_error& exception)
    {
        return failSongLoad(std::string{"Could not parse song.json: "} + exception.what());
    }

    try
    {
        if (!song_document.is_object() || song_document.value("formatVersion", 0) != 1)
        {
            return failSongLoad("Unsupported song.json formatVersion");
        }

        std::string error_message;
        const auto audio_assets = readAudioAssets(directory, song_document, error_message);
        if (!audio_assets.has_value())
        {
            return failSongLoad(error_message);
        }

        auto arrangements =
            readArrangements(directory, song_document, *audio_assets, error_message);
        if (!arrangements.has_value())
        {
            return failSongLoad(error_message);
        }

        Song song;
        song.metadata = readMetadata(song_document);
        song.chart.arrangements = std::move(*arrangements);

        return std::expected<Song, std::string>{std::in_place, std::move(song)};
    }
    catch (const Json::exception& exception)
    {
        return failSongLoad(std::string{"Invalid song.json: "} + exception.what());
    }
}

// Creates one workspace directory under the platform temp directory for an open project.
[[nodiscard]] std::optional<std::filesystem::path> createWorkspaceDirectory(
    std::string& error_message)
{
    std::error_code error;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        error_message = "Could not locate temporary directory: " + error.message();
        return std::nullopt;
    }

    const std::filesystem::path workspace_root = temp_root / "RockHero" / "project-workspaces";
    std::filesystem::create_directories(workspace_root, error);
    if (error)
    {
        error_message = "Could not create project workspace root: " + error.message();
        return std::nullopt;
    }

    static std::atomic_uint64_t workspace_sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t sequence = workspace_sequence.fetch_add(1, std::memory_order_relaxed);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path workspace_directory =
            workspace_root /
            (std::to_string(now) + "-" + std::to_string(sequence) + "-" + std::to_string(attempt));

        error.clear();
        if (std::filesystem::create_directory(workspace_directory, error))
        {
            return workspace_directory;
        }

        if (error)
        {
            error_message = "Could not create project workspace directory: " + error.message();
            return std::nullopt;
        }
    }

    error_message = "Could not allocate a unique project workspace directory";
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

// Extracts all safe, regular ZIP entries into a newly-created workspace directory.
[[nodiscard]] std::optional<std::string> extractZipToWorkspace(
    zip_t& archive, const std::filesystem::path& workspace_directory)
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
            (workspace_directory / std::filesystem::path{entry_path_name}).lexically_normal();
        if (const auto extraction_error = extractFileEntry(archive, index, output_path);
            extraction_error.has_value())
        {
            return extraction_error;
        }
    }

    return std::nullopt;
}

// Converts arrangement parts into the stable song-document spelling.
[[nodiscard]] std::string partName(Part part)
{
    switch (part)
    {
    case Part::Lead:
        return "Lead";
    case Part::Rhythm:
        return "Rhythm";
    case Part::Bass:
        return "Bass";
    }

    return "Lead";
}

// Produces a lowercase stem suitable for generated arrangement file names.
[[nodiscard]] std::string partFileStem(Part part)
{
    switch (part)
    {
    case Part::Lead:
        return "lead";
    case Part::Rhythm:
        return "rhythm";
    case Part::Bass:
        return "bass";
    }

    return "arrangement";
}

// Returns true when a relative path tries to escape its base.
[[nodiscard]] bool startsWithParentTraversal(const std::filesystem::path& path)
{
    return std::ranges::any_of(
        path, [](const std::filesystem::path& part) { return part == ".."; });
}

// Resolves an asset path and reports its package-relative spelling when it is in the workspace.
[[nodiscard]] std::optional<std::filesystem::path> relativeWorkspacePath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& asset_path)
{
    const std::filesystem::path workspace = workspace_directory.lexically_normal();
    const std::filesystem::path resolved_path =
        (asset_path.is_absolute() ? asset_path : workspace / asset_path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }

    const std::filesystem::path relative_path = resolved_path.lexically_relative(workspace);
    if (relative_path.empty() || startsWithParentTraversal(relative_path) ||
        !isSafeRelativePath(relative_path))
    {
        return std::nullopt;
    }

    return relative_path;
}

// Replaces path characters that would be awkward or unsafe in generated package entries.
[[nodiscard]] std::string sanitizeFileName(std::string file_name, std::size_t fallback_index)
{
    if (file_name.empty() || file_name == "." || file_name == "..")
    {
        file_name = "audio-" + std::to_string(fallback_index + 1);
    }

    for (char& character : file_name)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '.' && character != '-' && character != '_')
        {
            character = '_';
        }
    }

    return file_name;
}

// Chooses a generated package-relative audio path that does not overwrite another asset.
[[nodiscard]] std::filesystem::path uniqueAudioPath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& source_path,
    std::size_t asset_index)
{
    const std::string file_name = sanitizeFileName(source_path.filename().string(), asset_index);
    const std::filesystem::path stem = std::filesystem::path{file_name}.stem();
    const std::filesystem::path extension = std::filesystem::path{file_name}.extension();

    for (std::size_t attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path candidate_name =
            attempt == 0 ? std::filesystem::path{file_name}
                         : stem.string() + "-" + std::to_string(attempt + 1) + extension.string();
        const std::filesystem::path relative_path = std::filesystem::path{"audio"} / candidate_name;
        std::error_code error;
        if (!std::filesystem::exists(workspace_directory / relative_path, error))
        {
            return relative_path;
        }
    }

    return std::filesystem::path{"audio"} /
           ("audio-" + std::to_string(asset_index + 1) + source_path.extension().string());
}

// Copies an external audio asset into the project workspace and returns its relative path.
[[nodiscard]] std::optional<std::filesystem::path> importAudioAsset(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& source_path,
    std::size_t asset_index, std::string& error_message)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(source_path, error))
    {
        error_message = "Audio asset does not exist: " + source_path.string();
        return std::nullopt;
    }

    const std::filesystem::path relative_path =
        uniqueAudioPath(workspace_directory, source_path, asset_index);
    const std::filesystem::path output_path = workspace_directory / relative_path;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error)
    {
        error_message = "Could not create audio asset directory: " + error.message();
        return std::nullopt;
    }

    std::filesystem::copy_file(
        source_path, output_path, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
    {
        error_message = "Could not copy audio asset into project: " + error.message();
        return std::nullopt;
    }

    return relative_path;
}

// Ensures an arrangement file exists for the generated song-document reference.
[[nodiscard]] std::optional<std::string> ensureArrangementFile(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& relative_path)
{
    const std::filesystem::path arrangement_path = workspace_directory / relative_path;
    std::error_code error;
    if (std::filesystem::is_regular_file(arrangement_path, error))
    {
        return std::nullopt;
    }

    std::filesystem::create_directories(arrangement_path.parent_path(), error);
    if (error)
    {
        return "Could not create arrangement directory: " + error.message();
    }

    std::ofstream arrangement_file{arrangement_path};
    if (!arrangement_file.is_open())
    {
        return "Could not write arrangement file: " + arrangement_path.string();
    }

    arrangement_file << "<Arrangement formatVersion=\"1\" />\n";
    return std::nullopt;
}

// Creates a generated arrangement file path for one arrangement entry.
[[nodiscard]] std::filesystem::path arrangementFilePath(
    Part part, std::unordered_map<std::string, int>& part_counts)
{
    const std::string stem = partFileStem(part);
    int& count = part_counts[stem];
    ++count;

    if (count == 1)
    {
        return std::filesystem::path{"arrangements"} / (stem + ".xml");
    }

    return std::filesystem::path{"arrangements"} / (stem + "-" + std::to_string(count) + ".xml");
}

// Creates the JSON song document that represents the supplied session song.
[[nodiscard]] std::optional<Json> buildSongDocumentForSave(
    const std::filesystem::path& workspace_directory, const Song& song, std::string& error_message)
{
    if (song.chart.arrangements.empty())
    {
        error_message = "Cannot save a project with no arrangements";
        return std::nullopt;
    }

    Json audio_assets = Json::array();
    Json arrangements = Json::array();
    std::unordered_map<std::string, std::string> audio_ids_by_path;
    std::unordered_map<std::string, int> part_counts;

    for (std::size_t index = 0; index < song.chart.arrangements.size(); ++index)
    {
        const Arrangement& arrangement = song.chart.arrangements[index];
        if (!arrangement.audio_asset.has_value())
        {
            error_message = "Cannot save an arrangement without audio";
            return std::nullopt;
        }

        std::filesystem::path relative_audio_path;
        const std::filesystem::path& source_path = arrangement.audio_asset->path;
        if (const auto workspace_path = relativeWorkspacePath(workspace_directory, source_path);
            workspace_path.has_value())
        {
            relative_audio_path = *workspace_path;
        }
        else
        {
            const auto imported_path =
                importAudioAsset(workspace_directory, source_path, index, error_message);
            if (!imported_path.has_value())
            {
                return std::nullopt;
            }
            relative_audio_path = *imported_path;
        }

        const std::string relative_audio_name = relative_audio_path.generic_string();
        auto audio_id = audio_ids_by_path.find(relative_audio_name);
        if (audio_id == audio_ids_by_path.end())
        {
            const std::string generated_id =
                "audio-" + std::to_string(audio_ids_by_path.size() + 1);
            audio_assets.push_back(
                Json{
                    {"id", generated_id},
                    {"path", relative_audio_name},
                });
            audio_id = audio_ids_by_path.emplace(relative_audio_name, generated_id).first;
        }

        const std::filesystem::path arrangement_file =
            arrangementFilePath(arrangement.part, part_counts);
        if (const auto arrangement_error =
                ensureArrangementFile(workspace_directory, arrangement_file);
            arrangement_error.has_value())
        {
            error_message = *arrangement_error;
            return std::nullopt;
        }

        arrangements.push_back(
            Json{
                {"part", partName(arrangement.part)},
                {"file", arrangement_file.generic_string()},
                {"audio", audio_id->second},
            });
    }

    return Json{
        {"formatVersion", 1},
        {"metadata",
         Json{
             {"title", song.metadata.title},
             {"artist", song.metadata.artist},
             {"album", song.metadata.album},
             {"year", song.metadata.year},
         }},
        {"audioAssets", audio_assets},
        {"arrangements", arrangements},
    };
}

// Writes song.json into the extracted workspace.
[[nodiscard]] std::optional<std::string> writeSongDocumentForSave(
    const std::filesystem::path& workspace_directory, const Song& song)
{
    std::string error_message;
    const auto song_document = buildSongDocumentForSave(workspace_directory, song, error_message);
    if (!song_document.has_value())
    {
        return error_message;
    }

    std::ofstream song_document_file{workspace_directory / "song.json"};
    if (!song_document_file.is_open())
    {
        return "Could not write song.json";
    }

    song_document_file << song_document->dump(4) << '\n';
    if (!song_document_file.good())
    {
        return "Could not write song.json";
    }

    return std::nullopt;
}

// Creates a libzip file source for adding a workspace file to the saved package.
[[nodiscard]] zip_source_t* createFileSource(zip_t& archive, const std::filesystem::path& path)
{
#if defined(_WIN32)
    return zip_source_win32w(&archive, path.wstring().c_str(), 0, ZIP_LENGTH_TO_END);
#else
    return zip_source_file(&archive, path.string().c_str(), 0, ZIP_LENGTH_TO_END);
#endif
}

// Adds one regular workspace file to the output package archive.
[[nodiscard]] std::optional<std::string> addFileToPackage(
    zip_t& archive, const std::filesystem::path& workspace_directory,
    const std::filesystem::path& file_path)
{
    const std::filesystem::path relative_path =
        file_path.lexically_normal().lexically_relative(workspace_directory.lexically_normal());
    if (relative_path.empty() || startsWithParentTraversal(relative_path) ||
        !isSafeRelativePath(relative_path))
    {
        return "Project workspace contains an unsafe file path";
    }

    zip_source_t* source = createFileSource(archive, file_path);
    if (source == nullptr)
    {
        return "Could not read project workspace file: " + file_path.string();
    }

    const std::string entry_name = relative_path.generic_string();
    if (zip_file_add(&archive, entry_name.c_str(), source, ZIP_FL_ENC_UTF_8) < 0)
    {
        zip_source_free(source);
        return "Could not add project package entry: " + entry_name;
    }

    return std::nullopt;
}

// Rewrites the package archive from the current extracted workspace.
[[nodiscard]] std::optional<std::string> writeWorkspaceToPackage(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& package_path)
{
    std::error_code error;
    if (!package_path.parent_path().empty())
    {
        std::filesystem::create_directories(package_path.parent_path(), error);
        if (error)
        {
            return "Could not create project package directory: " + error.message();
        }
    }

    std::string error_message;
    auto archive = openArchiveForWriting(package_path, error_message);
    if (!archive.has_value())
    {
        return "Could not open project package for writing: " + error_message;
    }

    std::filesystem::recursive_directory_iterator directory_iterator{
        workspace_directory,
        error,
    };
    if (error)
    {
        return "Could not enumerate project workspace: " + error.message();
    }

    for (const std::filesystem::directory_entry& entry : directory_iterator)
    {
        error.clear();
        if (!entry.is_regular_file(error))
        {
            if (error)
            {
                return "Could not inspect project workspace file: " + error.message();
            }
            continue;
        }

        if (const auto add_error =
                addFileToPackage(*archive->get(), workspace_directory, entry.path());
            add_error.has_value())
        {
            return add_error;
        }
    }

    if (zip_close(archive->get()) != 0)
    {
        return "Could not write project package";
    }
    archive->release();

    return std::nullopt;
}

// Resolves imported arrangement audio into the workspace owned by the imported project.
[[nodiscard]] std::expected<Song, std::string> normalizeImportedSong(
    const std::filesystem::path& workspace_directory, Song song)
{
    if (song.chart.arrangements.empty())
    {
        return failProjectImport("Imported project must contain at least one arrangement");
    }

    for (Arrangement& arrangement : song.chart.arrangements)
    {
        if (!arrangement.audio_asset.has_value())
        {
            return failProjectImport("Imported arrangements must reference audio");
        }

        const auto relative_audio_path =
            relativeWorkspacePath(workspace_directory, arrangement.audio_asset->path);
        if (!relative_audio_path.has_value())
        {
            return failProjectImport(
                "Imported arrangement audio is missing or outside the project workspace");
        }

        arrangement.audio_asset =
            AudioAsset{(workspace_directory / *relative_audio_path).lexically_normal()};
    }

    return std::expected<Song, std::string>{std::in_place, std::move(song)};
}

} // namespace

// Removes the extracted workspace when the project leaves scope.
Project::~Project()
{
    resetWorkspace();
}

// Transfers package state and workspace ownership, clearing the source paths.
Project::Project(Project&& other)
    : m_path(std::exchange(other.m_path, {}))
    , m_workspace_directory(std::exchange(other.m_workspace_directory, {}))
{}

// Removes the old workspace before taking ownership from another project.
Project& Project::operator=(Project&& other)
{
    if (this != &other)
    {
        resetWorkspace();
        m_path = std::exchange(other.m_path, {});
        m_workspace_directory = std::exchange(other.m_workspace_directory, {});
    }

    return *this;
}

// Returns the file path associated with this open project.
const std::filesystem::path& Project::path() const noexcept
{
    return m_path;
}

// Returns the extracted workspace path used by parsed audio asset references.
const std::filesystem::path& Project::workspaceDirectory() const noexcept
{
    return m_workspace_directory;
}

// Opens the package archive, extracts it safely, and reads the song document.
std::expected<Song, std::string> Project::load(const std::filesystem::path& package_path)
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

    auto workspace_directory = createWorkspaceDirectory(error_message);
    if (!workspace_directory.has_value())
    {
        return failProjectLoad(error_message);
    }

    Project loaded_project;
    loaded_project.m_path = package_path;
    loaded_project.m_workspace_directory = std::move(*workspace_directory);

    const auto extraction_error =
        extractZipToWorkspace(*archive->get(), loaded_project.m_workspace_directory);
    if (extraction_error.has_value())
    {
        return failProjectLoad(*extraction_error);
    }

    auto loaded_song = readSong(loaded_project.m_workspace_directory);
    if (!loaded_song.has_value())
    {
        return failProjectLoad(std::move(loaded_song.error()));
    }

    Song song = std::move(*loaded_song);
    *this = std::move(loaded_project);

    return song;
}

// Imports a foreign package into a new workspace without assigning a native package path.
std::expected<Song, std::string> Project::import(
    const std::filesystem::path& source_path, IProjectImporter& importer)
{
    std::string error_message;
    auto workspace_directory = createWorkspaceDirectory(error_message);
    if (!workspace_directory.has_value())
    {
        return failProjectImport(error_message);
    }

    Project imported_project;
    imported_project.m_workspace_directory = std::move(*workspace_directory);

    auto imported_song =
        importer.importProject(source_path, imported_project.m_workspace_directory);
    if (!imported_song.has_value())
    {
        return failProjectImport(std::move(imported_song.error()));
    }

    auto normalized_song =
        normalizeImportedSong(imported_project.m_workspace_directory, std::move(*imported_song));
    if (!normalized_song.has_value())
    {
        return failProjectImport(std::move(normalized_song.error()));
    }

    Song song = std::move(*normalized_song);
    *this = std::move(imported_project);

    return song;
}

// Writes the current session song to the open project workspace and package.
std::expected<void, std::string> Project::save(const Song& song)
{
    if (m_path.empty() || m_workspace_directory.empty())
    {
        return failProjectSave("Cannot save before a project path has been chosen");
    }

    std::error_code error;
    if (!std::filesystem::is_directory(m_workspace_directory, error))
    {
        return failProjectSave("Project workspace does not exist");
    }

    if (const auto write_error = writeSongDocumentForSave(m_workspace_directory, song);
        write_error.has_value())
    {
        return failProjectSave(*write_error);
    }

    if (const auto package_error = writeWorkspaceToPackage(m_workspace_directory, m_path);
        package_error.has_value())
    {
        return failProjectSave(*package_error);
    }

    return std::expected<void, std::string>{};
}

// Saves to a chosen package path, creating a workspace first when this project is unopened.
std::expected<void, std::string> Project::saveAs(
    const std::filesystem::path& path, const Song& song)
{
    if (path.empty())
    {
        return failProjectSave("Cannot save a project without a package path");
    }

    if (m_workspace_directory.empty())
    {
        std::string error_message;
        auto workspace_directory = createWorkspaceDirectory(error_message);
        if (!workspace_directory.has_value())
        {
            return failProjectSave(error_message);
        }

        Project saved_project;
        saved_project.m_path = path;
        saved_project.m_workspace_directory = std::move(*workspace_directory);

        if (const auto write_error =
                writeSongDocumentForSave(saved_project.m_workspace_directory, song);
            write_error.has_value())
        {
            return failProjectSave(*write_error);
        }

        if (const auto package_error =
                writeWorkspaceToPackage(saved_project.m_workspace_directory, saved_project.m_path);
            package_error.has_value())
        {
            return failProjectSave(*package_error);
        }

        *this = std::move(saved_project);
        return std::expected<void, std::string>{};
    }

    if (const auto write_error = writeSongDocumentForSave(m_workspace_directory, song);
        write_error.has_value())
    {
        return failProjectSave(*write_error);
    }

    if (const auto package_error = writeWorkspaceToPackage(m_workspace_directory, path);
        package_error.has_value())
    {
        return failProjectSave(*package_error);
    }

    m_path = path;
    return std::expected<void, std::string>{};
}

// Best-effort workspace removal; project loading must not throw from cleanup.
void Project::resetWorkspace() noexcept
{
    if (m_workspace_directory.empty())
    {
        return;
    }

    std::error_code error;
    std::filesystem::remove_all(m_workspace_directory, error);
    m_workspace_directory.clear();
}

} // namespace rock_hero::core
