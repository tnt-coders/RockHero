#include "library/library_index_store.h"

#include <juce_core/juce_core.h>
#include <optional>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <utility>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

using common::core::Json;

// The index speaks song.json's exact part vocabulary through the shared common/core codec, so a
// cached entry reads identically to its source package and a new Part enumerator is encoded once.
using common::core::parsePartToken;
using common::core::partToken;

// Serializes one arrangement summary; absent optionals are omitted rather than written blank.
[[nodiscard]] juce::var arrangementToJson(const LibraryArrangementSummary& arrangement)
{
    juce::var json = Json::makeObject({
        {"id", Json::makeString(arrangement.id)},
    });

    if (arrangement.part.has_value())
    {
        json.getDynamicObject()->setProperty(
            Json::identifier("part"), Json::makeString(std::string{partToken(*arrangement.part)}));
    }

    if (arrangement.tuning.has_value())
    {
        juce::var strings = Json::makeArray();
        for (const std::string& open_string : arrangement.tuning->strings)
        {
            strings.append(Json::makeString(open_string));
        }
        json.getDynamicObject()->setProperty(
            Json::identifier("tuning"),
            Json::makeObject({
                {"strings", std::move(strings)},
                {"capo", juce::var{arrangement.tuning->capo}},
                {"centOffset", juce::var{arrangement.tuning->cent_offset}},
            }));
    }

    if (arrangement.intensity.has_value())
    {
        json.getDynamicObject()->setProperty(
            Json::identifier("intensity"),
            Json::makeObject({
                {"value", juce::var{arrangement.intensity->value}},
                {"calculatorVersion", juce::var{arrangement.intensity->calculator_version}},
            }));
    }

    return json;
}

// Deserializes one arrangement summary leniently: descriptive damage degrades to absent fields
// because the identity facts on the owning entry are what change detection depends on.
[[nodiscard]] LibraryArrangementSummary arrangementFromJson(const juce::var& json)
{
    LibraryArrangementSummary arrangement;
    arrangement.id = Json::readOptionalString(json, "id");
    arrangement.part = parsePartToken(Json::readOptionalString(json, "part"));

    if (const juce::var& tuning_json = Json::value(json, "tuning"); tuning_json.isObject())
    {
        common::core::ArrangementTuningDescription tuning;
        if (const juce::Array<juce::var>* const strings =
                Json::value(tuning_json, "strings").getArray())
        {
            for (const juce::var& open_string : *strings)
            {
                tuning.strings.push_back(open_string.toString().toStdString());
            }
        }
        tuning.capo = Json::readOptionalInt(tuning_json, "capo", 0);
        tuning.cent_offset = Json::readOptionalDouble(tuning_json, "centOffset", 0.0);
        arrangement.tuning = std::move(tuning);
    }

    if (const juce::var& intensity_json = Json::value(json, "intensity"); intensity_json.isObject())
    {
        const auto value = Json::tryReadDouble(intensity_json, "value");
        const auto calculator_version = Json::tryReadInt64(intensity_json, "calculatorVersion");
        if (value.has_value() && calculator_version.has_value())
        {
            arrangement.intensity = ArrangementIntensity{
                .value = *value,
                .calculator_version = static_cast<int>(*calculator_version),
            };
        }
    }

    return arrangement;
}

// Serializes one library entry; empty hash/album-art/warnings are omitted.
[[nodiscard]] juce::var entryToJson(const LibraryEntry& entry)
{
    juce::var arrangements = Json::makeArray();
    for (const LibraryArrangementSummary& arrangement : entry.arrangements)
    {
        arrangements.append(arrangementToJson(arrangement));
    }

    juce::var json = Json::makeObject({
        {"path", juce::var{common::core::juceStringFromPath(entry.package_path)}},
        {"sizeBytes", juce::var{static_cast<juce::int64>(entry.file_size_bytes)}},
        {"modifiedMs", juce::var{static_cast<juce::int64>(entry.modification_time_milliseconds)}},
        {"metadata",
         Json::makeObject({
             {"title", Json::makeString(entry.metadata.title)},
             {"artist", Json::makeString(entry.metadata.artist)},
             {"album", Json::makeString(entry.metadata.album)},
             {"year", juce::var{entry.metadata.year}},
         })},
        {"arrangements", std::move(arrangements)},
    });

    if (!entry.package_hash.empty())
    {
        json.getDynamicObject()->setProperty(
            Json::identifier("packageHash"), Json::makeString(entry.package_hash));
    }

    if (!entry.album_art_file_name.empty())
    {
        json.getDynamicObject()->setProperty(
            Json::identifier("albumArt"), Json::makeString(entry.album_art_file_name));
    }

    if (!entry.warnings.empty())
    {
        juce::var warnings = Json::makeArray();
        for (const std::string& warning : entry.warnings)
        {
            warnings.append(Json::makeString(warning));
        }
        json.getDynamicObject()->setProperty(Json::identifier("warnings"), std::move(warnings));
    }

    return json;
}

// Deserializes one entry. Identity facts (path/size/mtime) are required — without them the
// entry cannot participate in change detection, so their absence poisons the whole cache.
[[nodiscard]] std::optional<LibraryEntry> entryFromJson(const juce::var& json)
{
    if (!json.isObject())
    {
        return std::nullopt;
    }

    const auto path_text = Json::tryReadString(json, "path");
    const auto size_bytes = Json::tryReadInt64(json, "sizeBytes");
    const auto modified_milliseconds = Json::tryReadInt64(json, "modifiedMs");
    if (!path_text.has_value() || path_text->empty() || !size_bytes.has_value() ||
        !modified_milliseconds.has_value())
    {
        return std::nullopt;
    }

    LibraryEntry entry;
    entry.package_path =
        common::core::pathFromJuceString(juce::String::fromUTF8(path_text->c_str()));
    entry.file_size_bytes = *size_bytes;
    entry.modification_time_milliseconds = *modified_milliseconds;
    entry.package_hash = Json::readOptionalString(json, "packageHash");
    entry.album_art_file_name = Json::readOptionalString(json, "albumArt");

    const juce::var& metadata_json = Json::value(json, "metadata");
    entry.metadata = common::core::SongMetadata{
        .title = Json::readOptionalString(metadata_json, "title"),
        .artist = Json::readOptionalString(metadata_json, "artist"),
        .album = Json::readOptionalString(metadata_json, "album"),
        .year = Json::readOptionalInt(metadata_json, "year", 0),
    };

    if (const juce::Array<juce::var>* const arrangements =
            Json::value(json, "arrangements").getArray())
    {
        for (const juce::var& arrangement_json : *arrangements)
        {
            entry.arrangements.push_back(arrangementFromJson(arrangement_json));
        }
    }

    if (const juce::Array<juce::var>* const warnings = Json::value(json, "warnings").getArray())
    {
        for (const juce::var& warning : *warnings)
        {
            entry.warnings.push_back(warning.toString().toStdString());
        }
    }

    return entry;
}

// A rebuild-required outcome with the given reason; the cache is discardable by design.
[[nodiscard]] LibraryIndexLoadResult rebuildRequired(std::string reason)
{
    return LibraryIndexLoadResult{
        .index = {},
        .rebuild_required = true,
        .rebuild_reason = std::move(reason),
    };
}

} // namespace

LibraryIndexLoadResult loadLibraryIndex(const std::filesystem::path& index_path)
{
    const juce::File index_file = common::core::juceFileFromPath(index_path);
    if (!index_file.existsAsFile())
    {
        return rebuildRequired("index file does not exist");
    }

    const auto document = Json::parseDocument(index_file.loadFileAsString());
    if (!document.has_value())
    {
        return rebuildRequired("index file could not be parsed: " + document.error().message);
    }

    if (Json::readOptionalInt(*document, "indexFormatVersion", 0) != g_library_index_format_version)
    {
        return rebuildRequired("index format version is not supported");
    }

    const juce::Array<juce::var>* const entries = Json::value(*document, "entries").getArray();
    if (entries == nullptr)
    {
        return rebuildRequired("index document has no entries array");
    }

    LibraryIndexLoadResult result;
    result.index.entries.reserve(static_cast<std::size_t>(entries->size()));
    for (const juce::var& entry_json : *entries)
    {
        std::optional<LibraryEntry> entry = entryFromJson(entry_json);
        if (!entry.has_value())
        {
            return rebuildRequired("index entry is missing its identity facts");
        }
        result.index.entries.push_back(std::move(*entry));
    }

    return result;
}

std::expected<void, LibraryIndexStoreError> saveLibraryIndex(
    const LibraryIndex& index, const std::filesystem::path& index_path)
{
    juce::var entries = Json::makeArray();
    for (const LibraryEntry& entry : index.entries)
    {
        entries.append(entryToJson(entry));
    }

    const juce::var document = Json::makeObject({
        {"indexFormatVersion", juce::var{g_library_index_format_version}},
        {"entries", std::move(entries)},
    });

    const juce::File index_file = common::core::juceFileFromPath(index_path);
    if (const juce::Result created = index_file.getParentDirectory().createDirectory();
        created.failed())
    {
        return std::unexpected{LibraryIndexStoreError{
            LibraryIndexStoreErrorCode::CouldNotWriteIndex,
            "Could not create the index directory: " + created.getErrorMessage().toStdString(),
        }};
    }

    // replaceWithText writes the document to a sibling temp file in the same directory, then
    // replaces the target with it (Windows ReplaceFile / POSIX rename). A process or power
    // interruption therefore leaves either the previous index or the fully written new one in
    // place, never a half-written target, on a journaling filesystem. A mid-write I/O error can
    // still swap in a partial temp, but the loader treats any unparseable index as
    // rebuild-required, so the cache self-heals (same pattern as the chart document writer).
    if (!index_file.replaceWithText(juce::JSON::toString(document)))
    {
        return std::unexpected{LibraryIndexStoreError{
            LibraryIndexStoreErrorCode::CouldNotWriteIndex,
            "Could not write the index file: " + index_path.string(),
        }};
    }

    return {};
}

} // namespace rock_hero::game::core
