#include "package/rock_song_package.h"
#include "rock_song_package_format.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <juce_core/juce_core.h>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/package/archive_io.h>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/package/workspace_paths.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/song/audio_normalization.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
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

constexpr int g_zip_compression_level = 9;

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
[[nodiscard]] std::expected<std::filesystem::path, SongPackageError> importAudioAsset(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& source_path,
    std::size_t asset_index)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(source_path, error))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidAudioAsset,
            "Audio asset does not exist: " + source_path.string(),
        }};
    }

    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path relative_path =
        uniqueAudioPath(workspace_directory, source_path, asset_index);
    const std::filesystem::path output_path = workspace_directory / relative_path;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidAudioAsset,
            "Could not create audio asset directory: " + error.message(),
        }};
    }

    std::filesystem::copy_file(
        source_path, output_path, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidAudioAsset,
            "Could not copy audio asset into song package: " + error.message(),
        }};
    }

    return relative_path;
}

// Escapes a project string as a JSON string literal.
[[nodiscard]] std::string jsonString(const std::string& text)
{
    return juce::JSON::toString(Json::makeString(text)).toStdString();
}

// Formats native timing values at the fixed package precision.
[[nodiscard]] std::string formatTimingValue(double value)
{
    return std::format("{:.{}f}", value, g_timing_decimals);
}

// Formats non-timing JSON numbers compactly without introducing string values.
[[nodiscard]] std::string formatJsonDouble(double value)
{
    return std::format("{:.15g}", value);
}

// Renders one time-signature change as a compact object line.
[[nodiscard]] std::string formatTimeSignatureLine(const TimeSignatureChange& signature)
{
    return R"({ "measure": )" + std::to_string(signature.measure) + R"(, "numerator": )" +
           std::to_string(signature.numerator) + R"(, "denominator": )" +
           std::to_string(signature.denominator) + " }";
}

// Renders one beat anchor as a compact object line, addressing its beat with an on-beat position
// token and pinning it to seconds at the fixed package precision.
[[nodiscard]] std::string formatAnchorLine(const BeatAnchor& anchor)
{
    return R"({ "position": ")" + formatBeatPositionToken(anchor.measure, anchor.beat) +
           R"(", "seconds": )" + formatTimingValue(anchor.seconds) + " }";
}

// Song-document audio entry retained between validation and final JSON formatting.
struct AudioAssetDocumentEntry
{
    std::string id;
    std::string path;
    std::optional<AudioNormalization> normalization;
    TimeDuration start_offset;
};

// One authored tone change retained between validation and final JSON formatting. Only the start
// is persisted: changes tile the song gap-free, so each region's end is the next change's start
// (or the tempo-map terminal) and the reader derives it. Region ids are session-scoped and minted
// at load, so none is persisted; the tone is named by its catalog ID, from which the reader
// derives the canonical `tones/<uuid>/tone.json` document path.
struct ToneChangeDocumentEntry
{
    std::string start;
    std::string tone_id;
};

// One named catalog tone retained between validation and final JSON formatting. The ID is the
// tone's UUID; its document path is derived from the canonical layout rather than persisted.
struct ToneCatalogDocumentEntry
{
    std::string id;
    std::string name;
};

// Song-document arrangement entry retained between validation and final JSON formatting.
struct ArrangementDocumentEntry
{
    std::string id;
    std::string part;
    std::string audio;
    std::vector<ToneCatalogDocumentEntry> tones;
    std::vector<ToneChangeDocumentEntry> tone_changes;
    std::vector<std::string> tone_automation_lines;
    std::string chart_document;
};

// Renders one authored tone change as a compact object line. The change carries no name of its
// own; its name comes from the catalog tone it references.
[[nodiscard]] std::string formatToneChangeLine(const ToneChangeDocumentEntry& entry)
{
    std::string line = "{ \"start\": ";
    line += jsonString(entry.start);
    line += ", \"tone\": ";
    line += jsonString(entry.tone_id);
    line += " }";

    return line;
}

// Renders one catalog tone (its ID and user-facing name) as a compact object line.
[[nodiscard]] std::string formatToneCatalogLine(const ToneCatalogDocumentEntry& entry)
{
    std::string line = "{ \"id\": ";
    line += jsonString(entry.id);
    line += ", \"name\": ";
    line += jsonString(entry.name);
    line += " }";

    return line;
}

// Renders one plugin-parameter automation entry with its points inline. Musical positions are the
// persisted truth (runtime seconds are derived through the tempo map), so points serialize as
// grid-position tokens; the linear default shape is omitted.
[[nodiscard]] std::string formatToneAutomationLine(const ToneParameterAutomation& automation)
{
    std::string line = "{ \"plugin\": ";
    line += jsonString(automation.plugin_id);
    line += ", \"param\": ";
    line += jsonString(automation.param_id);
    line += ", \"points\": [ ";
    for (std::size_t index = 0; index < automation.points.size(); ++index)
    {
        const ToneAutomationPoint& point = automation.points[index];
        if (index != 0)
        {
            line += ", ";
        }
        line += "{ \"position\": ";
        line += jsonString(formatGridPositionToken(point.position));
        line += ", \"value\": ";
        line += formatJsonDouble(point.norm_value);
        if (point.curve_shape != 0.0F)
        {
            line += ", \"shape\": ";
            line += formatJsonDouble(point.curve_shape);
        }
        line += " }";
    }
    line += " ] }";

    return line;
}

// Renders one audio asset entry as a compact object line.
[[nodiscard]] std::string formatAudioAssetLine(const AudioAssetDocumentEntry& entry)
{
    std::string line = "{ \"id\": ";
    line += jsonString(entry.id);
    line += ", \"path\": ";
    line += jsonString(entry.path);
    if (entry.normalization.has_value())
    {
        line += R"(, "normalization": { "gainDb": )";
        line += formatJsonDouble(entry.normalization->gain_db);
        line += ", \"validationSha256\": ";
        line += jsonString(entry.normalization->validation_sha256);
        line += " }";
    }
    // Omit the offset when the audio starts at the score's first beat, so assets without an
    // alignment offset round-trip byte-for-byte with pre-offset packages.
    if (entry.start_offset.seconds != 0.0)
    {
        line += ", \"startOffset\": ";
        line += formatJsonDouble(entry.start_offset.seconds);
    }
    line += " }";

    return line;
}

// Renders one arrangement reference as a compact object line.
[[nodiscard]] std::string formatArrangementLine(const ArrangementDocumentEntry& entry)
{
    std::string line = "{ \"id\": ";
    line += jsonString(entry.id);
    line += ", \"part\": ";
    line += jsonString(entry.part);
    line += ", \"audio\": ";
    line += jsonString(entry.audio);
    if (!entry.tones.empty())
    {
        line += R"(, "tones": [)";
        for (std::size_t index = 0; index < entry.tones.size(); ++index)
        {
            line += (index == 0 ? "\n      " : ",\n      ");
            line += formatToneCatalogLine(entry.tones[index]);
        }
        line += "\n    ]";
    }
    if (!entry.tone_changes.empty())
    {
        line += R"(, "toneChanges": [)";
        for (std::size_t index = 0; index < entry.tone_changes.size(); ++index)
        {
            line += (index == 0 ? "\n      " : ",\n      ");
            line += formatToneChangeLine(entry.tone_changes[index]);
        }
        line += "\n    ]";
    }
    if (!entry.tone_automation_lines.empty())
    {
        line += R"(, "toneAutomation": [)";
        for (std::size_t index = 0; index < entry.tone_automation_lines.size(); ++index)
        {
            line += (index == 0 ? "\n      " : ",\n      ");
            line += entry.tone_automation_lines[index];
        }
        line += "\n    ]";
    }
    if (!entry.chart_document.empty())
    {
        line += ", \"chart\": ";
        line += jsonString(entry.chart_document);
    }
    line += " }";

    return line;
}

// Pairs the generated song document with arrangement IDs useful to callers.
struct SongDocumentForSave
{
    std::string contents;
    std::vector<std::string> arrangement_ids;
};

// Renders the full song document while keeping scan-heavy arrays at one object per line.
[[nodiscard]] std::string songDocumentContents(
    const SongMetadata& metadata, const TempoMap& tempo_map,
    const std::vector<AudioAssetDocumentEntry>& audio_assets,
    const std::vector<ArrangementDocumentEntry>& arrangements)
{
    std::string contents = "{\n";
    contents += "  \"formatVersion\": 1,\n";
    contents += "  \"metadata\": {\n";
    contents += "    \"title\": " + jsonString(metadata.title) + ",\n";
    contents += "    \"artist\": " + jsonString(metadata.artist) + ",\n";
    contents += "    \"album\": " + jsonString(metadata.album) + ",\n";
    contents += "    \"year\": " + std::to_string(metadata.year) + "\n";
    contents += "  },\n";
    contents += "  \"tempoMap\": {\n";
    contents += "    \"timeSignatures\": [";
    for (std::size_t index = 0; index < tempo_map.timeSignatures().size(); ++index)
    {
        contents += (index == 0 ? "\n      " : ",\n      ");
        contents += formatTimeSignatureLine(tempo_map.timeSignatures()[index]);
    }
    contents += tempo_map.timeSignatures().empty() ? "],\n" : "\n    ],\n";
    contents += "    \"anchors\": [";
    for (std::size_t index = 0; index < tempo_map.anchors().size(); ++index)
    {
        contents += (index == 0 ? "\n      " : ",\n      ");
        contents += formatAnchorLine(tempo_map.anchors()[index]);
    }
    contents += tempo_map.anchors().empty() ? "]\n" : "\n    ]\n";
    contents += "  },\n";
    contents += "  \"audioAssets\": [";
    for (std::size_t index = 0; index < audio_assets.size(); ++index)
    {
        contents += (index == 0 ? "\n    " : ",\n    ");
        contents += formatAudioAssetLine(audio_assets[index]);
    }
    contents += audio_assets.empty() ? "],\n" : "\n  ],\n";
    contents += "  \"arrangements\": [";
    for (std::size_t index = 0; index < arrangements.size(); ++index)
    {
        contents += (index == 0 ? "\n    " : ",\n    ");
        contents += formatArrangementLine(arrangements[index]);
    }
    contents += arrangements.empty() ? "]\n" : "\n  ]\n";
    contents += "}\n";

    return contents;
}

// Chooses the ID to write for one arrangement, generating a stable fallback when needed.
[[nodiscard]] std::expected<std::string, SongPackageError> arrangementIdForSave(
    const Arrangement& arrangement, std::set<std::string>& used_ids)
{
    if (!arrangement.id.empty())
    {
        if (!isCanonicalPackageId(arrangement.id))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "Cannot save a non-canonical arrangement id: " + arrangement.id,
            }};
        }

        if (!used_ids.insert(arrangement.id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "Cannot save duplicate arrangement id: " + arrangement.id,
            }};
        }

        return arrangement.id;
    }

    std::string candidate = generatePackageId();
    const bool inserted = used_ids.insert(candidate).second;
    assert(inserted && "Generated UUIDv4 arrangement ID unexpectedly collided");
    if (!inserted)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Could not generate a unique arrangement id",
        }};
    }

    return candidate;
}

// Validates one tone document reference (canonical, safe, present on disk) without writing
// anything, so a bad reference fails a save before any side effect occurs.
[[nodiscard]] std::expected<void, SongPackageError> validateToneDocumentOnDisk(
    const std::filesystem::path& workspace_directory, const std::string& tone_document_ref)
{
    const std::filesystem::path tone_document_path{tone_document_ref};
    if (!isSafeRelativePath(tone_document_path) || !isCanonicalToneDocumentRef(tone_document_ref))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a non-canonical tone document path: " + tone_document_ref,
        }};
    }

    std::error_code tone_document_error;
    const std::filesystem::path resolved_tone_document_path =
        (workspace_directory / tone_document_path).lexically_normal();
    if (!std::filesystem::is_regular_file(resolved_tone_document_path, tone_document_error))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a missing tone document: " + tone_document_ref,
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Validates one chart document reference (canonical, safe, present on disk) without writing
// anything. The chart file stays authoritative while charts are read-only, so saves never
// rewrite it; they only refuse to persist a dangling reference.
[[nodiscard]] std::expected<void, SongPackageError> validateChartDocumentOnDisk(
    const std::filesystem::path& workspace_directory, const std::string& chart_ref)
{
    const std::filesystem::path chart_path{chart_ref};
    if (!isSafeRelativePath(chart_path) || !isCanonicalChartDocumentRef(chart_ref))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a non-canonical chart document path: " + chart_ref,
        }};
    }

    std::error_code chart_error;
    const std::filesystem::path resolved_chart_path =
        (workspace_directory / chart_path).lexically_normal();
    if (!std::filesystem::is_regular_file(resolved_chart_path, chart_error))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a missing chart document: " + chart_ref,
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Validates an arrangement's tone references (catalog plus authored tone track) without writing
// anything, so a bad reference fails a save before any side effect occurs.
[[nodiscard]] std::expected<void, SongPackageError> validateArrangementToneReference(
    const std::filesystem::path& workspace_directory, const Arrangement& arrangement,
    const TempoMap& tempo_map)
{
    if (const auto structural = validateToneTrack(arrangement.tone_track, tempo_map);
        !structural.has_value())
    {
        return std::unexpected{structural.error()};
    }

    for (const ToneRegion& region : arrangement.tone_track.regions)
    {
        if (const auto tone_error =
                validateToneDocumentOnDisk(workspace_directory, region.tone_document_ref);
            !tone_error.has_value())
        {
            return tone_error;
        }
    }

    // Catalog tones persist by UUID with the document path derived from the canonical layout, so
    // every catalog reference must be canonical (and its document present) before a save may
    // reduce it to an ID.
    for (const Tone& tone : arrangement.tones)
    {
        if (const auto tone_error =
                validateToneDocumentOnDisk(workspace_directory, tone.tone_document_ref);
            !tone_error.has_value())
        {
            return tone_error;
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Validates an arrangement's plugin-parameter automation: per-entry structural rules plus at most
// one entry per (plugin, parameter) pair. Plugin ids are not resolved against tone documents here;
// an id without a live plugin is a legal unresolved entry by design.
[[nodiscard]] std::expected<void, SongPackageError> validateArrangementToneAutomation(
    const Arrangement& arrangement, const TempoMap& tempo_map)
{
    std::set<std::pair<std::string, std::string>> seen_parameters;
    for (const ToneParameterAutomation& automation : arrangement.tone_automation)
    {
        if (!isValidToneParameterAutomation(automation, tempo_map))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation entry is invalid for plugin: " + automation.plugin_id,
            }};
        }
        if (!seen_parameters.emplace(automation.plugin_id, automation.param_id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation repeats a plugin/parameter pair: " + automation.plugin_id + "/" +
                    automation.param_id,
            }};
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Creates the JSON song document that represents the supplied session song.
[[nodiscard]] std::expected<SongDocumentForSave, SongPackageError> buildSongDocumentForSave(
    const std::filesystem::path& workspace_directory, const Song& song)
{
    if (song.arrangements.empty())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a song package with no arrangements",
        }};
    }

    if (const auto tempo_map_error = validateTempoMap(song.tempo_map); !tempo_map_error.has_value())
    {
        return std::unexpected{tempo_map_error.error()};
    }

    std::vector<AudioAssetDocumentEntry> audio_assets;
    std::vector<ArrangementDocumentEntry> arrangements;
    std::unordered_map<std::string, std::string> audio_ids_by_path;
    std::set<std::string> used_arrangement_ids;
    std::vector<std::string> arrangement_ids;
    audio_assets.reserve(song.arrangements.size());
    arrangements.reserve(song.arrangements.size());
    arrangement_ids.reserve(song.arrangements.size());

    for (std::size_t index = 0; index < song.arrangements.size(); ++index)
    {
        const Arrangement& arrangement = song.arrangements[index];
        if (arrangement.audio_asset.path.empty())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "Cannot save an arrangement without audio",
            }};
        }

        // Validate this arrangement before any side effect (audio copy, document write), so a
        // validation failure does not leave copied audio behind.
        if (const auto tone_error =
                validateArrangementToneReference(workspace_directory, arrangement, song.tempo_map);
            !tone_error.has_value())
        {
            return std::unexpected{tone_error.error()};
        }
        if (const auto automation_error =
                validateArrangementToneAutomation(arrangement, song.tempo_map);
            !automation_error.has_value())
        {
            return std::unexpected{automation_error.error()};
        }
        if (!arrangement.chart_ref.empty())
        {
            if (const auto chart_error =
                    validateChartDocumentOnDisk(workspace_directory, arrangement.chart_ref);
                !chart_error.has_value())
            {
                return std::unexpected{chart_error.error()};
            }
        }

        const auto arrangement_id = arrangementIdForSave(arrangement, used_arrangement_ids);
        if (!arrangement_id.has_value())
        {
            return std::unexpected{arrangement_id.error()};
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
            const auto imported_path = importAudioAsset(workspace_directory, source_path, index);
            if (!imported_path.has_value())
            {
                return std::unexpected{imported_path.error()};
            }
            relative_audio_path = *imported_path;
        }

        const std::string relative_audio_name = relative_audio_path.generic_string();
        auto audio_id = audio_ids_by_path.find(relative_audio_name);
        if (audio_id == audio_ids_by_path.end())
        {
            const std::string generated_id =
                "audio-" + std::to_string(audio_ids_by_path.size() + 1);
            // Persist normalization only when the in-memory asset carries it. Assets without
            // normalization round-trip without growing song.json; the open/import flow will
            // analyze them before the project becomes usable.
            audio_assets.push_back(
                AudioAssetDocumentEntry{
                    .id = generated_id,
                    .path = relative_audio_name,
                    .normalization = arrangement.audio_asset.normalization,
                    .start_offset = arrangement.audio_asset.start_offset,
                });
            audio_id = audio_ids_by_path.emplace(relative_audio_name, generated_id).first;
        }

        std::vector<ToneChangeDocumentEntry> tone_changes;
        tone_changes.reserve(arrangement.tone_track.regions.size());
        for (const ToneRegion& region : arrangement.tone_track.regions)
        {
            tone_changes.push_back(
                ToneChangeDocumentEntry{
                    .start = formatBeatPositionToken(region.start.measure, region.start.beat),
                    .tone_id = toneIdFromToneDocumentRef(region.tone_document_ref),
                });
        }

        std::vector<ToneCatalogDocumentEntry> tones;
        tones.reserve(arrangement.tones.size());
        for (const Tone& tone : arrangement.tones)
        {
            tones.push_back(
                ToneCatalogDocumentEntry{
                    .id = toneIdFromToneDocumentRef(tone.tone_document_ref),
                    .name = tone.name,
                });
        }

        std::vector<std::string> tone_automation_lines;
        tone_automation_lines.reserve(arrangement.tone_automation.size());
        for (const ToneParameterAutomation& automation : arrangement.tone_automation)
        {
            tone_automation_lines.push_back(formatToneAutomationLine(automation));
        }

        arrangements.push_back(
            ArrangementDocumentEntry{
                .id = *arrangement_id,
                .part = partName(arrangement.part),
                .audio = audio_id->second,
                .tones = std::move(tones),
                .tone_changes = std::move(tone_changes),
                .tone_automation_lines = std::move(tone_automation_lines),
                .chart_document = arrangement.chart_ref,
            });
        arrangement_ids.push_back(*arrangement_id);
    }

    return SongDocumentForSave{
        .contents = songDocumentContents(song.metadata, song.tempo_map, audio_assets, arrangements),
        .arrangement_ids = std::move(arrangement_ids),
    };
}

// Writes native song package files and returns arrangement IDs for callers that need them.
[[nodiscard]] std::expected<std::vector<std::string>, SongPackageError> writeSongFilesForSave(
    const std::filesystem::path& song_directory, const Song& song)
{
    std::error_code error;
    std::filesystem::create_directories(song_directory, error);
    if (error)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotCreateSongDirectory,
            "Could not create song directory: " + error.message()
        }};
    }

    auto song_document = buildSongDocumentForSave(song_directory, song);
    if (!song_document.has_value())
    {
        return std::unexpected{std::move(song_document.error())};
    }

    std::ofstream song_document_file{song_directory / g_song_document_name, std::ios::binary};
    if (!song_document_file.is_open())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotWriteSongDocument,
        }};
    }

    song_document_file << song_document->contents;
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
