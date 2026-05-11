#include "psarc_importer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <open-psarc/psarc_file.h>
#include <optional>
#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/audio_asset.h>
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

using Json = nlohmann::json;

// Builds a failed import result with one stable project-owned message.
[[nodiscard]] std::expected<Song, std::string> failImport(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Converts file names and manifest values for case-insensitive matching.
[[nodiscard]] std::string toLower(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

// Returns a normalized forward-slash path spelling for PSARC file names.
[[nodiscard]] std::string normalizedArchivePath(std::string path)
{
    std::ranges::replace(path, '\\', '/');
    return path;
}

// Identifies Rocksmith arrangement XML files that can be copied without conversion.
[[nodiscard]] bool isArrangementXml(const std::string& path)
{
    const std::string lower_path = toLower(normalizedArchivePath(path));
    return lower_path.ends_with(".xml") && lower_path.find("songs/arr/") != std::string::npos;
}

// Identifies Rocksmith manifest JSON files that carry song and arrangement metadata.
[[nodiscard]] bool isLikelyArrangementManifest(const std::string& path)
{
    const std::string lower_path = toLower(normalizedArchivePath(path));
    return lower_path.ends_with(".json") && lower_path.find("songs_dlc_") != std::string::npos;
}

// Reads one extracted PSARC payload as text.
[[nodiscard]] std::string bytesToString(const std::vector<std::uint8_t>& bytes)
{
    return std::string{bytes.begin(), bytes.end()};
}

// Parses a JSON document while tolerating a UTF-8 BOM.
[[nodiscard]] std::optional<Json> parseJsonDocument(std::string_view text)
{
    constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};
    if (text.starts_with(utf8_bom))
    {
        text.remove_prefix(utf8_bom.size());
    }

    try
    {
        return Json::parse(text);
    }
    catch (const Json::exception&)
    {
        return std::nullopt;
    }
}

// Finds one of several equivalent Rocksmith manifest keys.
[[nodiscard]] const Json* findJsonValue(
    const Json& object, std::initializer_list<std::string_view> keys)
{
    if (!object.is_object())
    {
        return nullptr;
    }

    for (const std::string_view key : keys)
    {
        const auto value = object.find(std::string{key});
        if (value != object.end())
        {
            return &*value;
        }
    }

    return nullptr;
}

// Rocksmith manifests wrap the relevant attributes under the first Entries object.
[[nodiscard]] const Json* manifestAttributes(const Json& document)
{
    const Json* entries = findJsonValue(document, {"Entries", "entries"});
    if (entries == nullptr || !entries->is_object() || entries->empty())
    {
        return nullptr;
    }

    const Json& first_entry = entries->begin().value();
    return findJsonValue(first_entry, {"Attributes", "attributes"});
}

// Reads a string attribute from a manifest object when it exists.
[[nodiscard]] std::optional<std::string> readString(
    const Json& object, std::initializer_list<std::string_view> keys)
{
    const Json* value = findJsonValue(object, keys);
    if (value == nullptr || !value->is_string())
    {
        return std::nullopt;
    }

    return value->get<std::string>();
}

// Reads a numeric integer attribute from a manifest object when it exists.
[[nodiscard]] std::optional<int> readInt(
    const Json& object, std::initializer_list<std::string_view> keys)
{
    const Json* value = findJsonValue(object, keys);
    if (value == nullptr || !value->is_number())
    {
        return std::nullopt;
    }

    return value->get<int>();
}

// Maps Rocksmith arrangement names to Rock Hero arrangement parts.
[[nodiscard]] std::optional<Part> partFromText(const std::string& text)
{
    const std::string lower_text = toLower(text);
    if (lower_text.find("bass") != std::string::npos)
    {
        return Part::Bass;
    }

    if (lower_text.find("rhythm") != std::string::npos)
    {
        return Part::Rhythm;
    }

    if (lower_text.find("lead") != std::string::npos)
    {
        return Part::Lead;
    }

    return std::nullopt;
}

// Reads the arrangement part from Rocksmith manifest attributes.
[[nodiscard]] std::optional<Part> partFromManifest(const Json& attributes)
{
    const auto arrangement_name = readString(attributes, {"ArrangementName", "arrangementName"});
    if (arrangement_name.has_value())
    {
        if (const auto part = partFromText(*arrangement_name); part.has_value())
        {
            return part;
        }
    }

    const Json* properties = findJsonValue(attributes, {"ArrangementProperties"});
    if (properties == nullptr)
    {
        properties = findJsonValue(attributes, {"arrangementProperties"});
    }

    if (properties != nullptr && properties->is_object())
    {
        if (readInt(*properties, {"pathBass"}).value_or(0) != 0)
        {
            return Part::Bass;
        }

        if (readInt(*properties, {"pathRhythm"}).value_or(0) != 0)
        {
            return Part::Rhythm;
        }

        if (readInt(*properties, {"pathLead"}).value_or(0) != 0)
        {
            return Part::Lead;
        }
    }

    return std::nullopt;
}

// Applies the minimal song metadata required for the native song model.
void applyManifestMetadata(Song& song, const Json& attributes)
{
    if (song.metadata.title.empty())
    {
        song.metadata.title = readString(attributes, {"SongName", "songName"}).value_or("");
    }

    if (song.metadata.artist.empty())
    {
        song.metadata.artist = readString(attributes, {"ArtistName", "artistName"}).value_or("");
    }

    if (song.metadata.album.empty())
    {
        song.metadata.album = readString(attributes, {"AlbumName", "albumName"}).value_or("");
    }

    if (song.metadata.year == 0)
    {
        song.metadata.year = readInt(attributes, {"SongYear", "songYear"}).value_or(0);
    }
}

// Builds a manifest stem to arrangement part map and fills the first available song metadata.
[[nodiscard]] std::unordered_map<std::string, Part> readManifestParts(PsarcFile& psarc, Song& song)
{
    std::unordered_map<std::string, Part> parts_by_stem;

    for (const std::string& file_name : psarc.GetFileList())
    {
        if (!isLikelyArrangementManifest(file_name))
        {
            continue;
        }

        const auto json_document = parseJsonDocument(bytesToString(psarc.ExtractFile(file_name)));
        if (!json_document.has_value())
        {
            continue;
        }

        const Json* attributes = manifestAttributes(*json_document);
        if (attributes == nullptr)
        {
            continue;
        }

        applyManifestMetadata(song, *attributes);

        const auto part = partFromManifest(*attributes);
        if (!part.has_value())
        {
            continue;
        }

        const std::string stem =
            toLower(std::filesystem::path{normalizedArchivePath(file_name)}.stem().string());
        parts_by_stem.emplace(stem, *part);
    }

    return parts_by_stem;
}

// Infers the playable part represented by an arrangement XML entry.
[[nodiscard]] std::optional<Part> arrangementPart(
    const std::string& xml_path, const std::unordered_map<std::string, Part>& parts_by_stem)
{
    if (const auto part = partFromText(xml_path); part.has_value())
    {
        return part;
    }

    const std::string stem =
        toLower(std::filesystem::path{normalizedArchivePath(xml_path)}.stem().string());
    const auto part = parts_by_stem.find(stem);
    if (part != parts_by_stem.end())
    {
        return part->second;
    }

    return std::nullopt;
}

// Converts arrangement parts into native arrangement file stems.
[[nodiscard]] std::string arrangementFileStem(Part part)
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

// Generates the same arrangement path that native save will later reference.
[[nodiscard]] std::filesystem::path arrangementPath(
    Part part, std::unordered_map<std::string, int>& part_counts)
{
    const std::string stem = arrangementFileStem(part);
    int& count = part_counts[stem];
    ++count;

    if (count == 1)
    {
        return std::filesystem::path{"arrangements"} / (stem + ".xml");
    }

    return std::filesystem::path{"arrangements"} / (stem + "-" + std::to_string(count) + ".xml");
}

// Creates parent directories and extracts a PSARC file into the requested workspace path.
void extractFileTo(
    PsarcFile& psarc, const std::string& file_name, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    psarc.ExtractFileTo(file_name, path.string());
}

// Creates parent directories and copies a staged file into the requested workspace path.
void copyFileTo(const std::filesystem::path& source_path, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::copy_file(
        source_path, path, std::filesystem::copy_options::overwrite_existing);
}

// Adds one imported arrangement using the single converted backing audio file.
void addImportedArrangement(
    std::vector<Arrangement>& arrangements, std::string id, Part part,
    const AudioAsset& audio_asset)
{
    arrangements.push_back(
        Arrangement{
            .id = std::move(id),
            .part = part,
            .difficulty = DifficultyRating{},
            .audio_asset = audio_asset,
            .audio_duration = TimeDuration{},
            .tone_timeline_ref = {},
            .note_events = {},
        });
}

// Returns true for converted audio candidates produced by open-psarc.
[[nodiscard]] bool isConvertedAudioFile(const std::filesystem::directory_entry& entry)
{
    if (!entry.is_regular_file())
    {
        return false;
    }

    return toLower(entry.path().extension().string()) == ".ogg";
}

// Chooses the main backing audio from converted PSARC audio files.
[[nodiscard]] std::optional<std::filesystem::path> findConvertedAudio(
    const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> candidates;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator{directory})
    {
        if (isConvertedAudioFile(entry))
        {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.empty())
    {
        return std::nullopt;
    }

    std::ranges::sort(candidates);
    const auto main_audio = std::ranges::find_if(candidates, [](const std::filesystem::path& path) {
        return toLower(path.filename().string()).find("preview") == std::string::npos;
    });

    if (main_audio != candidates.end())
    {
        return *main_audio;
    }

    return candidates.front();
}

// Converts the PSARC audio payload and copies the selected backing file into native layout.
[[nodiscard]] std::expected<AudioAsset, std::string> importAudio(
    PsarcFile& psarc, const std::filesystem::path& workspace_directory)
{
    const std::filesystem::path audio_staging = workspace_directory / ".psarc-audio";
    std::error_code error;
    std::filesystem::remove_all(audio_staging, error);
    std::filesystem::create_directories(audio_staging, error);
    if (error)
    {
        return std::unexpected<std::string>{
            "Could not create PSARC audio staging directory: " + error.message()
        };
    }

    psarc.ConvertAudio(audio_staging.string());

    const auto converted_audio = findConvertedAudio(audio_staging);
    if (!converted_audio.has_value())
    {
        return std::unexpected<std::string>{"PSARC did not produce convertible arrangement audio"};
    }

    const std::filesystem::path imported_audio_path =
        workspace_directory / "audio" / converted_audio->filename();
    std::filesystem::create_directories(imported_audio_path.parent_path(), error);
    if (error)
    {
        return std::unexpected<std::string>{"Could not create audio directory: " + error.message()};
    }

    std::filesystem::copy_file(
        *converted_audio,
        imported_audio_path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error)
    {
        return std::unexpected<std::string>{"Could not import PSARC audio: " + error.message()};
    }

    std::filesystem::remove_all(audio_staging, error);
    return AudioAsset{imported_audio_path.lexically_normal()};
}

// Copies arrangement XML files already present in the PSARC archive.
[[nodiscard]] std::vector<Arrangement> importArchivedArrangements(
    PsarcFile& psarc, const std::unordered_map<std::string, Part>& parts_by_stem,
    const AudioAsset& audio_asset, const std::filesystem::path& workspace_directory)
{
    std::vector<Arrangement> arrangements;
    std::unordered_map<std::string, int> part_counts;

    for (const std::string& file_name : psarc.GetFileList())
    {
        if (!isArrangementXml(file_name))
        {
            continue;
        }

        const auto part = arrangementPart(file_name, parts_by_stem);
        if (!part.has_value())
        {
            continue;
        }

        const std::filesystem::path relative_arrangement_path = arrangementPath(*part, part_counts);
        extractFileTo(psarc, file_name, workspace_directory / relative_arrangement_path);

        addImportedArrangement(
            arrangements, relative_arrangement_path.stem().generic_string(), *part, audio_asset);
    }

    return arrangements;
}

// Converts binary Rocksmith SNG arrangements to XML and copies them into native layout.
[[nodiscard]] std::vector<Arrangement> importConvertedSngArrangements(
    PsarcFile& psarc, const std::unordered_map<std::string, Part>& parts_by_stem,
    const AudioAsset& audio_asset, const std::filesystem::path& workspace_directory)
{
    const std::filesystem::path arrangement_staging = workspace_directory / ".psarc-arrangements";
    std::error_code error;
    std::filesystem::remove_all(arrangement_staging, error);
    std::filesystem::create_directories(arrangement_staging);

    psarc.ConvertSng(arrangement_staging.string());

    std::vector<std::filesystem::path> xml_files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator{arrangement_staging})
    {
        if (entry.is_regular_file() && isArrangementXml(entry.path().generic_string()))
        {
            xml_files.push_back(entry.path());
        }
    }

    std::ranges::sort(xml_files);

    std::vector<Arrangement> arrangements;
    std::unordered_map<std::string, int> part_counts;
    for (const std::filesystem::path& xml_file : xml_files)
    {
        const auto part = arrangementPart(xml_file.generic_string(), parts_by_stem);
        if (!part.has_value())
        {
            continue;
        }

        const std::filesystem::path relative_arrangement_path = arrangementPath(*part, part_counts);
        copyFileTo(xml_file, workspace_directory / relative_arrangement_path);

        addImportedArrangement(
            arrangements, relative_arrangement_path.stem().generic_string(), *part, audio_asset);
    }

    std::filesystem::remove_all(arrangement_staging, error);
    return arrangements;
}

// Converts SNG arrangements first; embedded XML is only a fallback for packages without SNG data.
[[nodiscard]] std::vector<Arrangement> importArrangements(
    PsarcFile& psarc, const std::unordered_map<std::string, Part>& parts_by_stem,
    const AudioAsset& audio_asset, const std::filesystem::path& workspace_directory)
{
    std::vector<Arrangement> arrangements =
        importConvertedSngArrangements(psarc, parts_by_stem, audio_asset, workspace_directory);
    if (!arrangements.empty())
    {
        return arrangements;
    }

    return importArchivedArrangements(psarc, parts_by_stem, audio_asset, workspace_directory);
}

} // namespace

// Imports the minimal PSARC song metadata, arrangement XML, and converted backing audio.
std::expected<Song, std::string> PsarcImporter::importProject(
    const std::filesystem::path& source_path, const std::filesystem::path& workspace_directory)
{
    try
    {
        PsarcFile psarc{source_path.string()};
        psarc.Open();

        Song song;
        const auto parts_by_stem = readManifestParts(psarc, song);
        const auto audio_asset = importAudio(psarc, workspace_directory);
        if (!audio_asset.has_value())
        {
            return failImport(audio_asset.error());
        }

        song.arrangements =
            importArrangements(psarc, parts_by_stem, *audio_asset, workspace_directory);
        if (song.arrangements.empty())
        {
            return failImport("PSARC did not contain supported arrangement XML files");
        }

        return std::expected<Song, std::string>{std::in_place, std::move(song)};
    }
    catch (const PsarcException& exception)
    {
        return failImport(std::string{"Could not import PSARC: "} + exception.what());
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return failImport(std::string{"Could not write imported PSARC files: "} + exception.what());
    }
}

} // namespace rock_hero::common::core
