#include "package/package_description.h"

#include "song_document_json.h"

#include <expected>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Streams one archive entry as UTF-8 text; empty when the entry is absent or unreadable. ZIP
// entry names always use forward slashes, matching the package-relative reference grammar.
[[nodiscard]] std::optional<std::string> readArchiveEntryText(
    juce::ZipFile& archive, const std::string& entry_name)
{
    const int entry_index = archive.getIndexOfFileName(juce::String{entry_name.c_str()});
    if (entry_index < 0)
    {
        return std::nullopt;
    }

    const std::unique_ptr<juce::InputStream> stream{archive.createStreamForEntry(entry_index)};
    if (stream == nullptr)
    {
        return std::nullopt;
    }
    return stream->readEntireStreamAsString().toStdString();
}

// True when the archive contains the exact entry name.
[[nodiscard]] bool archiveContainsEntry(juce::ZipFile& archive, const std::string& entry_name)
{
    return archive.getIndexOfFileName(juce::String{entry_name.c_str()}) >= 0;
}

// Peeks one arrangement entry leniently: structural damage becomes a warning and a partial
// description rather than failing the whole package (the library shows warned entries).
void describeArrangement(
    juce::ZipFile& archive, const juce::var& arrangement_json,
    const std::map<std::string, std::string>& audio_paths_by_id, PackageDescription& description)
{
    if (!arrangement_json.isObject())
    {
        description.warnings.emplace_back("arrangement entry is not an object");
        return;
    }

    ArrangementDescription arrangement;
    arrangement.id = Json::tryReadString(arrangement_json, "id").value_or(std::string{});
    if (arrangement.id.empty())
    {
        description.warnings.emplace_back("arrangement entry is missing an id");
    }

    const std::string part_text =
        Json::tryReadString(arrangement_json, "part").value_or(std::string{});
    arrangement.part = parseSongDocumentPart(part_text);
    if (!arrangement.part.has_value())
    {
        description.warnings.emplace_back(
            "arrangement " + arrangement.id + " has an unsupported part: " + part_text);
    }

    const std::string audio_id =
        Json::tryReadString(arrangement_json, "audio").value_or(std::string{});
    const auto audio_path = audio_paths_by_id.find(audio_id);
    arrangement.audio_asset_present =
        audio_path != audio_paths_by_id.end() && archiveContainsEntry(archive, audio_path->second);
    if (!arrangement.audio_asset_present)
    {
        description.warnings.emplace_back(
            "arrangement " + arrangement.id + " backing audio entry is missing");
    }

    const juce::var& chart_json = Json::value(arrangement_json, "chart");
    if (chart_json.isString())
    {
        arrangement.chart_ref = chart_json.toString().toStdString();
        const std::optional<std::string> chart_text =
            readArchiveEntryText(archive, arrangement.chart_ref);
        if (!chart_text.has_value())
        {
            description.warnings.emplace_back(
                "chart entry is missing from the archive: " + arrangement.chart_ref);
        }
        else if (const auto chart = parseChartDocument(*chart_text); !chart.has_value())
        {
            description.warnings.emplace_back(
                "chart entry could not be parsed: " + arrangement.chart_ref + " (" +
                chart.error().message + ")");
        }
        else
        {
            arrangement.tuning = ArrangementTuningDescription{
                .strings = chart->tuning.strings,
                .capo = chart->tuning.capo,
                .cent_offset = chart->tuning.cent_offset,
            };
        }
    }

    description.arrangements.push_back(std::move(arrangement));
}

} // namespace

std::expected<PackageDescription, SongPackageError> readRockSongPackageDescription(
    const std::filesystem::path& package_path)
{
    juce::ZipFile archive{juceFileFromPath(package_path)};
    if (archive.getNumEntries() <= 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotExtractPackage,
            "Package is not a readable archive: " + package_path.string(),
        }};
    }

    const std::optional<std::string> song_document_text =
        readArchiveEntryText(archive, "song.json");
    if (!song_document_text.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::MissingSongDocument,
            "Package archive does not contain song.json",
        }};
    }

    auto parsed_document = Json::parseDocument(juce::String{song_document_text->c_str()});
    if (!parsed_document.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Could not parse song.json: " + parsed_document.error().message,
        }};
    }

    const juce::var song_document = std::move(*parsed_document);
    if (auto version_ok = requireSupportedSongDocumentVersion(song_document);
        !version_ok.has_value())
    {
        return std::unexpected{std::move(version_ok.error())};
    }

    PackageDescription description;
    description.format_version = Json::readOptionalInt(song_document, "formatVersion", 0);
    description.metadata = readSongDocumentMetadata(song_document);

    // Audio-asset presence checks resolve arrangement audio ids through the asset list without
    // opening (or extracting) any audio bytes.
    std::map<std::string, std::string> audio_paths_by_id;
    const juce::var& audio_assets_json = Json::value(song_document, "audioAssets");
    if (const juce::Array<juce::var>* const assets = audio_assets_json.getArray())
    {
        for (const juce::var& asset_json : *assets)
        {
            const auto id = Json::tryReadString(asset_json, "id");
            const auto relative_path = Json::tryReadString(asset_json, "path");
            if (id.has_value() && relative_path.has_value())
            {
                audio_paths_by_id.emplace(*id, *relative_path);
            }
        }
    }

    const juce::var& arrangements_json = Json::value(song_document, "arrangements");
    if (const juce::Array<juce::var>* const arrangements = arrangements_json.getArray())
    {
        for (const juce::var& arrangement_json : *arrangements)
        {
            describeArrangement(archive, arrangement_json, audio_paths_by_id, description);
        }
    }
    else
    {
        description.warnings.emplace_back("song.json has no arrangements array");
    }

    return description;
}

} // namespace rock_hero::common::core
