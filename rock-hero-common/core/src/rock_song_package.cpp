#include "rock_song_package.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/archive_io.h>
#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/package_id.h>
#include <rock_hero/common/core/workspace_paths.h>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

constexpr std::string_view g_song_document_name{"song.json"};
constexpr std::string_view g_arrangements_directory_name{"arrangements"};
constexpr std::string_view g_arrangement_file_extension{".xml"};
constexpr int g_zip_compression_level = 9;

// Converts std::filesystem paths to JUCE paths while preserving Windows wide paths.
[[nodiscard]] juce::File juceFileFromPath(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return juce::File{juce::String{path.wstring().c_str()}};
#else
    return juce::File{juce::String::fromUTF8(path.string().c_str())};
#endif
}

// Finds the required native song document in an extracted song package directory.
[[nodiscard]] std::optional<std::filesystem::path> findSongDocument(
    const std::filesystem::path& directory)
{
    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path song_document_path = directory / g_song_document_name;
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

// Rejects archive-relative paths that could escape or ambiguously address a workspace directory.
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

// Converts a Rock song package-relative path into a concrete file path inside the extracted
// directory.
[[nodiscard]] std::optional<std::filesystem::path> resolveExistingFile(
    const std::filesystem::path& directory, const std::string& relative_path)
{
    const std::filesystem::path path{relative_path};
    if (!isSafeRelativePath(path))
    {
        return std::nullopt;
    }

    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path resolved_path = (directory / path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }

    return resolved_path;
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

// Builds the canonical package-relative arrangement XML path for a stable arrangement ID.
[[nodiscard]] std::filesystem::path arrangementFilePath(std::string_view arrangement_id)
{
    return std::filesystem::path{std::string{g_arrangements_directory_name}} /
           (std::string{arrangement_id} + std::string{g_arrangement_file_extension});
}

// Reports whether a package-relative arrangement file path is canonical for its arrangement ID.
[[nodiscard]] bool isCanonicalArrangementFileRef(
    std::string_view arrangement_id, const std::string& file_ref)
{
    return file_ref == arrangementFilePath(arrangement_id).generic_string();
}

// Reads song metadata while treating missing descriptive fields as blank draft values.
[[nodiscard]] SongMetadata readMetadata(const juce::var& song_document)
{
    const juce::var& metadata = Json::value(song_document, "metadata");
    if (!metadata.isObject())
    {
        return {};
    }

    return SongMetadata{
        .title = Json::readOptionalString(metadata, "title"),
        .artist = Json::readOptionalString(metadata, "artist"),
        .album = Json::readOptionalString(metadata, "album"),
        .year = Json::readOptionalInt(metadata, "year", 0),
    };
}

// Reads song audio assets into an ID map keyed only inside song package IO.
[[nodiscard]] std::optional<std::unordered_map<std::string, AudioAsset>> readAudioAssets(
    const std::filesystem::path& directory, const juce::var& song_document,
    std::string& error_message)
{
    const juce::var& audio_assets_json = Json::value(song_document, "audioAssets");
    if (!audio_assets_json.isArray() || audio_assets_json.size() == 0)
    {
        error_message = "song.json must contain at least one audio asset";
        return std::nullopt;
    }

    std::unordered_map<std::string, AudioAsset> audio_assets;
    const juce::Array<juce::var>* const audio_asset_array = audio_assets_json.getArray();
    for (const juce::var& asset_json : *audio_asset_array)
    {
        if (!asset_json.isObject())
        {
            error_message = "audioAssets entries must be objects";
            return std::nullopt;
        }

        const auto id = Json::readRequiredString(asset_json, "id");
        const auto relative_path = Json::readRequiredString(asset_json, "path");
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

// Reads arrangements from song-document entries into project-owned core values.
[[nodiscard]] std::optional<std::vector<Arrangement>> readArrangements(
    const std::filesystem::path& directory, const juce::var& song_document,
    const std::unordered_map<std::string, AudioAsset>& audio_assets, std::string& error_message)
{
    const juce::var& arrangements_json = Json::value(song_document, "arrangements");
    if (!arrangements_json.isArray() || arrangements_json.size() == 0)
    {
        error_message = "song.json must contain at least one arrangement";
        return std::nullopt;
    }

    std::vector<Arrangement> arrangements;
    arrangements.reserve(static_cast<std::size_t>(arrangements_json.size()));
    std::set<std::string> arrangement_ids;

    const juce::Array<juce::var>* const arrangement_array = arrangements_json.getArray();
    for (const juce::var& arrangement_json : *arrangement_array)
    {
        if (!arrangement_json.isObject())
        {
            error_message = "arrangement entries must be objects";
            return std::nullopt;
        }

        const auto id = Json::readRequiredString(arrangement_json, "id");
        const auto part_text = Json::readRequiredString(arrangement_json, "part");
        const auto arrangement_file = Json::readRequiredString(arrangement_json, "file");
        const auto audio_id = Json::readRequiredString(arrangement_json, "audio");
        std::string tone_document_ref;
        if (!id.has_value() || id->empty() || !part_text.has_value() ||
            !arrangement_file.has_value() || !audio_id.has_value())
        {
            error_message =
                "arrangement entries require non-empty id, part, file, and audio fields";
            return std::nullopt;
        }

        if (!isCanonicalPackageId(*id))
        {
            error_message = "arrangement id must be a canonical UUIDv4: " + *id;
            return std::nullopt;
        }

        if (!arrangement_ids.insert(*id).second)
        {
            error_message = "duplicate arrangement id: " + *id;
            return std::nullopt;
        }

        const auto part = parsePart(*part_text);
        if (!part.has_value())
        {
            error_message = "unsupported arrangement part: " + *part_text;
            return std::nullopt;
        }

        if (!isCanonicalArrangementFileRef(*id, *arrangement_file))
        {
            error_message = "arrangement file must match arrangement id: " + *arrangement_file;
            return std::nullopt;
        }

        if (!resolveExistingFile(directory, *arrangement_file).has_value())
        {
            error_message = "arrangement file is missing or unsafe: " + *arrangement_file;
            return std::nullopt;
        }

        const juce::var& tone_document_json = Json::value(arrangement_json, "toneDocument");
        if (!tone_document_json.isVoid() && !tone_document_json.isUndefined())
        {
            if (!tone_document_json.isString() || tone_document_json.toString().isEmpty())
            {
                error_message = "arrangement toneDocument must be a non-empty string when present";
                return std::nullopt;
            }

            tone_document_ref = tone_document_json.toString().toStdString();
            if (!isCanonicalToneDocumentRef(tone_document_ref))
            {
                error_message =
                    "tone document path must be tones/<uuid>/tone.json: " + tone_document_ref;
                return std::nullopt;
            }

            if (!resolveExistingFile(directory, tone_document_ref).has_value())
            {
                error_message = "tone document is missing or unsafe: " + tone_document_ref;
                return std::nullopt;
            }
        }

        const auto audio_asset = audio_assets.find(*audio_id);
        if (audio_asset == audio_assets.end())
        {
            error_message = "arrangement references unknown audio asset: " + *audio_id;
            return std::nullopt;
        }

        arrangements.push_back(
            Arrangement{
                .id = *id,
                .part = *part,
                .difficulty = DifficultyRating{},
                .audio_asset = audio_asset->second,
                .audio_duration = TimeDuration{},
                .tone_document_ref = std::move(tone_document_ref),
                .note_events = {},
            });
    }

    return arrangements;
}

// Reports whether a ZIP entry is a directory marker.
[[nodiscard]] bool isDirectoryEntry(std::string_view entry_path_name) noexcept
{
    return !entry_path_name.empty() && entry_path_name.back() == '/';
}

// Extracts one regular entry stream into the already-resolved output path.
[[nodiscard]] std::expected<void, ArchiveError> extractFileEntry(
    juce::ZipFile& archive, int index, const std::filesystem::path& output_path)
{
    const std::unique_ptr<juce::InputStream> input_stream{archive.createStreamForEntry(index)};
    if (input_stream == nullptr)
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::EntryOpenFailed,
            "Could not open archive entry for extraction",
        }};
    }

    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error)
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OutputDirectoryCreationFailed,
            "Could not create archive output directory: " + error.message(),
        }};
    }

    std::ofstream output{output_path, std::ios::binary};
    if (!output.is_open())
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OutputWriteFailed,
            "Could not write archive entry: " + output_path.string(),
        }};
    }

    // Fixed buffer keeps each read well under juce::InputStream::read's INT_MAX return ceiling.
    // Do not replace this loop with a single full-entry read: that would silently truncate
    // entries larger than 2 GiB.
    constexpr std::size_t buffer_size = 65536;
    std::array<char, buffer_size> buffer{};
    while (true)
    {
        const int bytes_read = input_stream->read(buffer.data(), static_cast<int>(buffer.size()));
        if (bytes_read < 0)
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::EntryReadFailed,
                "Could not read archive entry",
            }};
        }

        if (bytes_read == 0)
        {
            break;
        }

        output.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
        if (!output.good())
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::OutputWriteFailed,
                "Could not write archive entry: " + output_path.string(),
            }};
        }
    }

    return std::expected<void, ArchiveError>{};
}

// Extracts all safe, regular ZIP entries into a newly-created workspace directory.
[[nodiscard]] std::expected<void, ArchiveError> extractZipToWorkspace(
    juce::ZipFile& archive, const std::filesystem::path& workspace_directory)
{
    const int entry_count = archive.getNumEntries();
    if (entry_count <= 0)
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::UnsafeEntry,
            "Archive is empty or is not a valid zip archive",
        }};
    }

    std::set<std::string> extracted_entries;
    for (int index = 0; index < entry_count; ++index)
    {
        const juce::ZipFile::ZipEntry* const entry = archive.getEntry(index);
        if (entry == nullptr)
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::UnsafeEntry,
                "Could not read archive entry metadata",
            }};
        }

        const std::string entry_name = entry->filename.toStdString();
        if (entry->isSymbolicLink || !isSafeZipEntryName(entry_name))
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::UnsafeEntry,
                "Archive contains an unsafe entry: " + entry_name,
            }};
        }

        const std::string entry_path_name = zipEntryPathName(entry_name);
        const std::string normalized_name = normalizedZipEntryName(entry_name);
        if (isDirectoryEntry(entry_path_name))
        {
            continue;
        }

        if (!extracted_entries.insert(normalized_name).second)
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::UnsafeEntry,
                "Archive contains a duplicate entry: " + entry_name,
            }};
        }

        const std::filesystem::path output_path =
            (workspace_directory / std::filesystem::path{entry_path_name}).lexically_normal();
        if (auto extraction_error = extractFileEntry(archive, index, output_path);
            !extraction_error.has_value())
        {
            return std::unexpected{std::move(extraction_error.error())};
        }
    }

    return std::expected<void, ArchiveError>{};
}

// Converts arrangement parts into the stable song-document spelling.
[[nodiscard]] std::string partName(Part part)
{
    switch (part)
    {
        case Part::Lead:
        {
            return "Lead";
        }
        case Part::Rhythm:
        {
            return "Rhythm";
        }
        case Part::Bass:
        {
            return "Bass";
        }
    }

    return "Lead";
}

// Returns true when a relative path tries to escape its base.
[[nodiscard]] bool startsWithParentTraversal(const std::filesystem::path& path)
{
    return std::ranges::any_of(
        path, [](const std::filesystem::path& part) { return part == ".."; });
}

// Replaces path characters that would be awkward or unsafe in generated song package entries.
[[nodiscard]] std::string sanitizeFileName(std::string file_name, std::size_t fallback_index)
{
    if (file_name.empty() || file_name == "." || file_name == "..")
    {
        file_name = "audio-" + std::to_string(fallback_index + 1);
    }

    for (char& character : file_name)
    {
        const auto value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '.' && character != '-' && character != '_')
        {
            character = '_';
        }
    }

    return file_name;
}

// Chooses a generated Rock song package-relative audio path that does not overwrite another asset.
[[nodiscard]] std::filesystem::path uniqueAudioPath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& source_path,
    std::size_t asset_index)
{
    const std::string file_name = sanitizeFileName(source_path.filename().string(), asset_index);
    const std::filesystem::path stem = std::filesystem::path{file_name}.stem();
    const std::filesystem::path extension = std::filesystem::path{file_name}.extension();

    for (std::size_t attempt = 0; attempt < 100; ++attempt)
    {
        const std::string candidate_file_name =
            attempt == 0 ? file_name
                         : stem.string() + "-" + std::to_string(attempt + 1) + extension.string();
        const std::filesystem::path candidate_name{candidate_file_name};
        std::filesystem::path relative_path = std::filesystem::path{"audio"} / candidate_name;
        std::error_code error;
        if (!std::filesystem::exists(workspace_directory / relative_path, error))
        {
            return relative_path;
        }
    }

    return std::filesystem::path{"audio"} /
           ("audio-" + std::to_string(asset_index + 1) + source_path.extension().string());
}

// Copies an external audio asset into the song package workspace and returns its relative path.
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

    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path relative_path =
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
        error_message = "Could not copy audio asset into song package: " + error.message();
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

// Pairs the generated song document with arrangement IDs useful to callers.
struct SongDocumentForSave
{
    juce::var document;
    std::vector<std::string> arrangement_ids;
};

// Chooses the ID to write for one arrangement, generating a stable fallback when needed.
[[nodiscard]] std::optional<std::string> arrangementIdForSave(
    const Arrangement& arrangement, std::set<std::string>& used_ids, std::string& error_message)
{
    if (!arrangement.id.empty())
    {
        if (!isCanonicalPackageId(arrangement.id))
        {
            error_message = "Cannot save a non-canonical arrangement id: " + arrangement.id;
            return std::nullopt;
        }

        if (!used_ids.insert(arrangement.id).second)
        {
            error_message = "Cannot save duplicate arrangement id: " + arrangement.id;
            return std::nullopt;
        }

        return arrangement.id;
    }

    std::string candidate = generatePackageId();
    const bool inserted = used_ids.insert(candidate).second;
    assert(inserted && "Generated UUIDv4 arrangement ID unexpectedly collided");
    if (!inserted)
    {
        error_message = "Could not generate a unique arrangement id";
        return std::nullopt;
    }

    return candidate;
}

// Creates the JSON song document that represents the supplied session song.
[[nodiscard]] std::optional<SongDocumentForSave> buildSongDocumentForSave(
    const std::filesystem::path& workspace_directory, const Song& song, std::string& error_message)
{
    if (song.arrangements.empty())
    {
        error_message = "Cannot save a song package with no arrangements";
        return std::nullopt;
    }

    juce::var audio_assets = Json::makeArray();
    juce::var arrangements = Json::makeArray();
    std::unordered_map<std::string, std::string> audio_ids_by_path;
    std::set<std::string> used_arrangement_ids;
    std::vector<std::string> arrangement_ids;
    arrangement_ids.reserve(song.arrangements.size());

    for (std::size_t index = 0; index < song.arrangements.size(); ++index)
    {
        const Arrangement& arrangement = song.arrangements[index];
        if (arrangement.audio_asset.path.empty())
        {
            error_message = "Cannot save an arrangement without audio";
            return std::nullopt;
        }

        std::filesystem::path relative_audio_path;
        const std::filesystem::path& source_path = arrangement.audio_asset.path;
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
            audio_assets.append(
                Json::makeObject({
                    {"id", Json::makeString(generated_id)},
                    {"path", Json::makeString(relative_audio_name)},
                }));
            audio_id = audio_ids_by_path.emplace(relative_audio_name, generated_id).first;
        }

        const auto arrangement_id =
            arrangementIdForSave(arrangement, used_arrangement_ids, error_message);
        if (!arrangement_id.has_value())
        {
            return std::nullopt;
        }

        const std::filesystem::path arrangement_file = arrangementFilePath(*arrangement_id);
        if (const auto arrangement_error =
                ensureArrangementFile(workspace_directory, arrangement_file);
            arrangement_error.has_value())
        {
            error_message = *arrangement_error;
            return std::nullopt;
        }

        const juce::var arrangement_document = Json::makeObject({
            {"id", Json::makeString(*arrangement_id)},
            {"part", Json::makeString(partName(arrangement.part))},
            {"file", Json::makeString(arrangement_file.generic_string())},
            {"audio", Json::makeString(audio_id->second)},
        });
        if (!arrangement.tone_document_ref.empty())
        {
            const std::filesystem::path tone_document_path{arrangement.tone_document_ref};
            if (!isSafeRelativePath(tone_document_path) ||
                !isCanonicalToneDocumentRef(arrangement.tone_document_ref))
            {
                error_message = "Cannot save a non-canonical tone document path: " +
                                arrangement.tone_document_ref;
                return std::nullopt;
            }

            std::error_code tone_document_error;
            const std::filesystem::path resolved_tone_document_path =
                (workspace_directory / tone_document_path).lexically_normal();
            if (!std::filesystem::is_regular_file(resolved_tone_document_path, tone_document_error))
            {
                error_message =
                    "Cannot save a missing tone document: " + arrangement.tone_document_ref;
                return std::nullopt;
            }

            arrangement_document.getDynamicObject()->setProperty(
                Json::identifier("toneDocument"), Json::makeString(arrangement.tone_document_ref));
        }

        arrangements.append(arrangement_document);
        arrangement_ids.push_back(*arrangement_id);
    }

    return SongDocumentForSave{
        .document = Json::makeObject({
            {"formatVersion", juce::var{1}},
            {"metadata",
             Json::makeObject({
                 {"title", Json::makeString(song.metadata.title)},
                 {"artist", Json::makeString(song.metadata.artist)},
                 {"album", Json::makeString(song.metadata.album)},
                 {"year", juce::var{song.metadata.year}},
             })},
            {"audioAssets", audio_assets},
            {"arrangements", arrangements},
        }),
        .arrangement_ids = std::move(arrangement_ids),
    };
}

// Writes native song package files and returns arrangement IDs for callers that need them.
[[nodiscard]] std::expected<std::vector<std::string>, SongPackageError> writeSongFilesForSave(
    const std::filesystem::path& song_directory, const Song& song)
{
    std::string error_message;
    std::error_code error;
    std::filesystem::create_directories(song_directory, error);
    if (error)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotCreateSongDirectory,
            "Could not create song directory: " + error.message()
        }};
    }

    auto song_document = buildSongDocumentForSave(song_directory, song, error_message);
    if (!song_document.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            std::move(error_message),
        }};
    }

    std::ofstream song_document_file{song_directory / g_song_document_name};
    if (!song_document_file.is_open())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotWriteSongDocument,
        }};
    }

    song_document_file << juce::JSON::toString(song_document->document).toStdString() << '\n';
    if (!song_document_file.good())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotWriteSongDocument,
        }};
    }

    return std::move(song_document->arrangement_ids);
}

// Adds one regular workspace file to the output archive.
[[nodiscard]] std::expected<void, ArchiveError> addFileToArchive(
    juce::ZipFile::Builder& archive_builder, const std::filesystem::path& workspace_directory,
    const std::filesystem::path& file_path)
{
    const std::filesystem::path relative_path =
        file_path.lexically_normal().lexically_relative(workspace_directory.lexically_normal());
    if (relative_path.empty() || startsWithParentTraversal(relative_path) ||
        !isSafeRelativePath(relative_path))
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::UnsafeWorkspacePath,
            "Archive workspace contains an unsafe file path",
        }};
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(file_path, error))
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::WorkspaceFileReadFailed,
            "Could not read archive workspace file: " + file_path.string(),
        }};
    }

    const std::string entry_name = relative_path.generic_string();
    archive_builder.addFile(
        juceFileFromPath(file_path),
        g_zip_compression_level,
        juce::String::fromUTF8(entry_name.c_str()));

    return std::expected<void, ArchiveError>{};
}

} // namespace

// Extracts a zip archive through JUCE while preserving project-owned safety checks.
std::expected<void, ArchiveError> extractArchiveToWorkspace(
    const std::filesystem::path& archive_path, const std::filesystem::path& workspace_directory)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive_path, error))
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OpenFailed,
            "Archive file does not exist: " + archive_path.string(),
        }};
    }

    juce::ZipFile archive{juceFileFromPath(archive_path)};
    return extractZipToWorkspace(archive, workspace_directory);
}

// Reads song.json and resolves Rock song package-relative asset references into core data.
std::expected<Song, SongPackageError> readRockSongPackageDirectory(
    const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::MissingPackageDirectory,
            "Song package directory does not exist",
        }};
    }

    const auto song_document_path = findSongDocument(directory);
    if (!song_document_path.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::MissingSongDocument,
            "Song package directory does not contain song.json",
        }};
    }

    juce::FileInputStream song_document_file{juceFileFromPath(*song_document_path)};
    if (song_document_file.failedToOpen())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotOpenSongDocument,
            "Could not open song.json: " +
                song_document_file.getStatus().getErrorMessage().toStdString(),
        }};
    }

    const juce::String document_text = song_document_file.readEntireStreamAsString();
    auto parsed_document = Json::parseDocument(document_text);
    if (!parsed_document.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Could not parse song.json: " + parsed_document.error().message,
        }};
    }

    const juce::var song_document = std::move(*parsed_document);
    const auto format_version = Json::readOptionalInt(song_document, "formatVersion", 0);
    if (!song_document.isObject() || format_version != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Unsupported song.json formatVersion",
        }};
    }

    std::string error_message;
    const auto audio_assets = readAudioAssets(directory, song_document, error_message);
    if (!audio_assets.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidAudioAsset,
            std::move(error_message),
        }};
    }

    auto arrangements = readArrangements(directory, song_document, *audio_assets, error_message);
    if (!arrangements.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            std::move(error_message),
        }};
    }

    Song song;
    song.metadata = readMetadata(song_document);
    song.arrangements = std::move(*arrangements);

    return std::expected<Song, SongPackageError>{std::in_place, std::move(song)};
}

// Extracts a native song package and reads the root song document from the workspace.
std::expected<Song, SongPackageError> readRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory)
{
    if (const auto package_error = extractArchiveToWorkspace(package_path, workspace_directory);
        !package_error.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotExtractPackage,
            "Could not extract native song package: " + package_error.error().message
        }};
    }

    return readRockSongPackageDirectory(workspace_directory);
}

// Resolves an asset path and reports its workspace-relative spelling.
std::optional<std::filesystem::path> relativeWorkspacePath(
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

    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path relative_path = resolved_path.lexically_relative(workspace);
    if (relative_path.empty() || startsWithParentTraversal(relative_path) ||
        !isSafeRelativePath(relative_path))
    {
        return std::nullopt;
    }

    return relative_path;
}

// Writes native song files into a Rock song package content directory.
std::expected<std::vector<std::string>, SongPackageError> writeRockSongPackageDirectory(
    const std::filesystem::path& song_directory, const Song& song)
{
    auto song_files = writeSongFilesForSave(song_directory, song);
    if (!song_files.has_value())
    {
        return std::unexpected{std::move(song_files.error())};
    }

    return std::move(*song_files);
}

// Rewrites the archive from the current workspace.
std::expected<void, ArchiveError> writeWorkspaceToArchive(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& archive_path)
{
    std::error_code error;
    if (!archive_path.parent_path().empty())
    {
        std::filesystem::create_directories(archive_path.parent_path(), error);
        if (error)
        {
            return std::unexpected{ArchiveError{
                ArchiveErrorCode::ArchiveDirectoryCreationFailed,
                "Could not create archive directory: " + error.message(),
            }};
        }
    }

    const std::filesystem::recursive_directory_iterator directory_iterator{
        workspace_directory,
        error,
    };
    if (error)
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::WorkspaceEnumerationFailed,
            "Could not enumerate archive workspace: " + error.message(),
        }};
    }

    juce::ZipFile::Builder archive_builder;
    for (const std::filesystem::directory_entry& entry : directory_iterator)
    {
        error.clear();
        if (!entry.is_regular_file(error))
        {
            if (error)
            {
                return std::unexpected{ArchiveError{
                    ArchiveErrorCode::WorkspaceFileInspectionFailed,
                    "Could not inspect archive workspace file: " + error.message(),
                }};
            }
            continue;
        }

        if (auto add_error = addFileToArchive(archive_builder, workspace_directory, entry.path());
            !add_error.has_value())
        {
            return std::unexpected{std::move(add_error.error())};
        }
    }

    juce::FileOutputStream output_stream{juceFileFromPath(archive_path)};
    if (output_stream.failedToOpen())
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OpenForWritingFailed,
            "Could not open archive for writing: " +
                output_stream.getStatus().getErrorMessage().toStdString(),
        }};
    }

    if (!output_stream.setPosition(0))
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OpenForWritingFailed,
            "Could not seek archive for writing",
        }};
    }

    const juce::Result truncate_result = output_stream.truncate();
    if (truncate_result.failed())
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::OpenForWritingFailed,
            "Could not truncate archive for writing: " +
                truncate_result.getErrorMessage().toStdString(),
        }};
    }

    if (!archive_builder.writeToStream(output_stream, nullptr))
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::WriteFailed,
            "Could not write archive",
        }};
    }
    output_stream.flush();
    if (output_stream.getStatus().failed())
    {
        return std::unexpected{ArchiveError{
            ArchiveErrorCode::WriteFailed,
            "Could not flush archive: " + output_stream.getStatus().getErrorMessage().toStdString(),
        }};
    }

    return std::expected<void, ArchiveError>{};
}

// Writes a native song directory and rewrites its song package archive.
std::expected<void, SongPackageError> writeRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& song_directory,
    const Song& song)
{
    auto song_files = writeRockSongPackageDirectory(song_directory, song);
    if (!song_files.has_value())
    {
        return std::unexpected{std::move(song_files.error())};
    }

    if (const auto package_error = writeWorkspaceToArchive(song_directory, package_path);
        !package_error.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotWritePackage,
            "Could not write native song package: " + package_error.error().message
        }};
    }

    return std::expected<void, SongPackageError>{};
}

} // namespace rock_hero::common::core
