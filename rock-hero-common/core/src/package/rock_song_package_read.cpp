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
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/chart/chart_rules.h>
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

// Lightweight parsed form of a persisted tempo-map anchor address.
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

// Reads the optional normalization record persisted on an audio asset entry. Absent, null, and
// incomplete records produce an empty optional so the open/import flow can repair them by
// re-running analysis before the project becomes usable.
[[nodiscard]] std::optional<AudioNormalization> readOptionalNormalization(
    const juce::var& normalization_json)
{
    if (!normalization_json.isObject())
    {
        return std::nullopt;
    }

    const auto gain_db = Json::tryReadDouble(normalization_json, "gainDb");
    const auto validation_sha256 = Json::tryReadString(normalization_json, "validationSha256");
    if (!gain_db.has_value() || !validation_sha256.has_value() || validation_sha256->empty())
    {
        return std::nullopt;
    }

    return AudioNormalization{
        .gain_db = *gain_db,
        .validation_sha256 = *validation_sha256,
    };
}

// Reads song audio assets into an ID map keyed only inside song package IO.
// Reports whether a package-relative audio path names a FLAC file, RockHero's only supported package
// audio format. Compared case-insensitively so a differently-cased extension still matches.
[[nodiscard]] bool hasFlacExtension(const std::string& relative_path)
{
    std::string extension = std::filesystem::path{relative_path}.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".flac";
}

[[nodiscard]] std::expected<std::unordered_map<std::string, AudioAsset>, SongPackageError>
readAudioAssets(const std::filesystem::path& directory, const juce::var& song_document)
{
    const juce::var& audio_assets_json = Json::value(song_document, "audioAssets");
    if (!audio_assets_json.isArray() || audio_assets_json.size() == 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidAudioAsset,
            "song.json must contain at least one audio asset",
        }};
    }

    std::unordered_map<std::string, AudioAsset> audio_assets;
    const juce::Array<juce::var>* const audio_asset_array = audio_assets_json.getArray();
    for (const juce::var& asset_json : *audio_asset_array)
    {
        if (!asset_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidAudioAsset,
                "audioAssets entries must be objects",
            }};
        }

        const auto id = Json::tryReadString(asset_json, "id");
        const auto relative_path = Json::tryReadString(asset_json, "path");
        if (!id.has_value() || id->empty() || !relative_path.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidAudioAsset,
                "audio asset entries require non-empty id and path fields",
            }};
        }

        const auto resolved_path = resolveExistingFile(directory, *relative_path);
        if (!resolved_path.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidAudioAsset,
                "audio asset path is missing or unsafe: " + *relative_path,
            }};
        }

        // FLAC is RockHero's canonical package audio format. Packages that reference WAV, Ogg, or any
        // other format are no longer supported and must be re-imported through the FLAC pipeline.
        // Safety and existence are checked first so an unsafe path still reports as unsafe.
        if (!hasFlacExtension(*relative_path))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidAudioAsset,
                "audio asset must be FLAC, RockHero's canonical package format: " + *relative_path,
            }};
        }

        const auto normalization =
            readOptionalNormalization(Json::value(asset_json, "normalization"));

        // Absent offset means the audio starts at the score's first beat; pre-offset packages
        // simply omit the field.
        const TimeDuration start_offset{
            Json::tryReadDouble(asset_json, "startOffset").value_or(0.0)
        };

        const auto inserted = audio_assets.emplace(
            *id,
            AudioAsset{
                .path = resolved_path->lexically_normal(),
                .normalization = normalization,
                .start_offset = start_offset,
            });
        if (!inserted.second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidAudioAsset,
                "duplicate audio asset id: " + *id,
            }};
        }
    }

    return audio_assets;
}

// Reports whether a parsed JSON integer fits the int fields used by core package values.
[[nodiscard]] bool fitsIntField(std::int64_t value) noexcept
{
    return value >= static_cast<std::int64_t>(std::numeric_limits<int>::min()) &&
           value <= static_cast<std::int64_t>(std::numeric_limits<int>::max());
}

// Reports whether a parsed JSON integer fits a positive int field.
[[nodiscard]] bool fitsPositiveIntField(std::int64_t value) noexcept
{
    return value > 0 && fitsIntField(value);
}

// Reads the tempo-map time-signature array without applying cross-entry ordering rules yet.
[[nodiscard]] std::expected<std::vector<TimeSignatureChange>, SongPackageError>
readTimeSignatureChanges(const juce::var& tempo_map_json)
{
    const juce::var& time_signatures_json = Json::value(tempo_map_json, "timeSignatures");
    if (!time_signatures_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.timeSignatures must be an array",
        }};
    }

    std::vector<TimeSignatureChange> time_signatures;
    time_signatures.reserve(static_cast<std::size_t>(time_signatures_json.size()));

    const juce::Array<juce::var>* const time_signature_array = time_signatures_json.getArray();
    for (const juce::var& signature_json : *time_signature_array)
    {
        if (!signature_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.timeSignatures entries must be objects",
            }};
        }

        const auto measure = Json::tryReadInt64(signature_json, "measure");
        const auto numerator = Json::tryReadInt64(signature_json, "numerator");
        const auto denominator = Json::tryReadInt64(signature_json, "denominator");
        if (!measure.has_value() || !numerator.has_value() || !denominator.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.timeSignatures entries require measure, numerator, and denominator",
            }};
        }

        if (!fitsPositiveIntField(*measure) || !fitsPositiveIntField(*numerator) ||
            !fitsPositiveIntField(*denominator))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.timeSignatures values must be positive integers",
            }};
        }

        time_signatures.push_back(
            TimeSignatureChange{
                .measure = static_cast<int>(*measure),
                .numerator = static_cast<int>(*numerator),
                .denominator = static_cast<int>(*denominator),
            });
    }

    return time_signatures;
}

// Reads the tempo-map anchor array without applying ordering or meter-address rules yet.
[[nodiscard]] std::expected<std::vector<BeatAnchor>, SongPackageError> readBeatAnchors(
    const juce::var& tempo_map_json)
{
    const juce::var& anchors_json = Json::value(tempo_map_json, "anchors");
    if (!anchors_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.anchors must be an array",
        }};
    }

    std::vector<BeatAnchor> anchors;
    anchors.reserve(static_cast<std::size_t>(anchors_json.size()));

    const juce::Array<juce::var>* const anchor_array = anchors_json.getArray();
    for (const juce::var& anchor_json : *anchor_array)
    {
        if (!anchor_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors entries must be objects",
            }};
        }

        const auto position_text = Json::tryReadString(anchor_json, "position");
        const auto seconds = Json::tryReadDouble(anchor_json, "seconds");
        if (!position_text.has_value() || !seconds.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors entries require position and seconds",
            }};
        }

        const auto position = parseBeatPositionToken(*position_text);
        if (!position.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                R"(tempoMap.anchors position must be an on-beat token such as "188:1")",
            }};
        }

        anchors.push_back(
            BeatAnchor{
                .measure = position->measure,
                .beat = position->beat,
                .seconds = *seconds,
            });
    }

    return anchors;
}

// Validates the package-level tempo-map invariants before arrangements use the grid.
// Reads and validates the required song-level tempo map.
[[nodiscard]] std::expected<TempoMap, SongPackageError> readTempoMap(const juce::var& song_document)
{
    const juce::var& tempo_map_json = Json::value(song_document, "tempoMap");
    if (!tempo_map_json.isObject())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "song.json must contain a tempoMap object",
        }};
    }

    auto time_signatures = readTimeSignatureChanges(tempo_map_json);
    if (!time_signatures.has_value())
    {
        return std::unexpected{std::move(time_signatures.error())};
    }

    auto anchors = readBeatAnchors(tempo_map_json);
    if (!anchors.has_value())
    {
        return std::unexpected{std::move(anchors.error())};
    }

    TempoMap tempo_map{std::move(*time_signatures), std::move(*anchors)};
    if (const auto validation_error = validateTempoMap(tempo_map); !validation_error.has_value())
    {
        return std::unexpected{validation_error.error()};
    }

    return tempo_map;
}

// Reads one arrangement's optional authored tone track and validates it against the tempo map.
// The persisted form is a flat "toneChanges" array whose entries reference catalog tones by UUID;
// region ids are session-scoped and minted here rather than persisted.
[[nodiscard]] std::expected<ToneTrack, SongPackageError> readToneTrack(
    const std::filesystem::path& directory, const juce::var& arrangement_json,
    const TempoMap& tempo_map)
{
    ToneTrack tone_track;
    const juce::var& tone_changes_json = Json::value(arrangement_json, "toneChanges");
    if (tone_changes_json.isVoid() || tone_changes_json.isUndefined())
    {
        return tone_track;
    }

    if (!tone_changes_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "arrangement toneChanges must be an array when present",
        }};
    }

    tone_track.regions.reserve(static_cast<std::size_t>(tone_changes_json.size()));
    for (const juce::var& change_json : *tone_changes_json.getArray())
    {
        const auto start_text = Json::tryReadString(change_json, "start");
        const auto tone_id = Json::tryReadString(change_json, "tone");
        if (!change_json.isObject() || !start_text.has_value() || !tone_id.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneChanges entries require start and tone fields",
            }};
        }

        if (!isCanonicalPackageId(*tone_id))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone change tone must be a canonical tone id: " + *tone_id,
            }};
        }

        const auto start = parseGridPositionToken(*start_text);
        if (!start.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone change start must be a \"<measure>:<beat>\" or \"<measure>:<beat>+<n>/<d>\" "
                "token",
            }};
        }

        tone_track.regions.push_back(
            ToneRegion{
                .id = generatePackageId(),
                .start = *start,
                .end = GridPosition{},
                .tone_document_ref = toneDocumentRefForToneId(*tone_id),
            });
    }

    // Regions are persisted as tone-change markers: only starts are stored, and each end derives
    // as the next region's start (the terminal anchor beat for the last), so gaps are structurally
    // unrepresentable.
    if (!tone_track.regions.empty())
    {
        for (std::size_t index = 0; index + 1 < tone_track.regions.size(); ++index)
        {
            tone_track.regions[index].end = tone_track.regions[index + 1].start;
        }
        const auto [terminal_measure, terminal_beat] =
            tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
        tone_track.regions.back().end =
            GridPosition{.measure = terminal_measure, .beat = terminal_beat};
    }

    if (const auto structural = validateToneTrack(tone_track, tempo_map); !structural.has_value())
    {
        return std::unexpected{structural.error()};
    }

    for (const ToneRegion& region : tone_track.regions)
    {
        if (!resolveExistingFile(directory, region.tone_document_ref).has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone region document is missing or unsafe: " + region.tone_document_ref,
            }};
        }
    }

    return tone_track;
}

// Reads the arrangement's named-tone catalog: the "tones" array, whose entries name tones by
// UUID. A missing array reads as an empty catalog (the editor's load baseline then mints a
// default tone), so packages without tone data load rather than fail.
[[nodiscard]] std::expected<std::vector<Tone>, SongPackageError> readToneCatalog(
    const juce::var& arrangement_json, const ToneTrack& tone_track)
{
    std::vector<Tone> tones;
    const auto add_tone = [&tones](const std::string& ref, const std::string& name) {
        if (ref.empty() || std::ranges::any_of(tones, [&ref](const Tone& tone) {
                return tone.tone_document_ref == ref;
            }))
        {
            return;
        }
        tones.push_back(Tone{.tone_document_ref = ref, .name = name});
    };

    const juce::var& tones_json = Json::value(arrangement_json, "tones");
    if (tones_json.isArray())
    {
        for (const juce::var& tone_json : *tones_json.getArray())
        {
            const auto id = Json::tryReadString(tone_json, "id");
            if (!id.has_value() || !isCanonicalPackageId(*id))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "tones entries require a canonical tone id",
                }};
            }
            add_tone(
                toneDocumentRefForToneId(*id),
                Json::tryReadString(tone_json, "name").value_or(std::string{}));
        }
    }

    // Every referenced tone belongs in the catalog; a hand-edited file whose "tones" array missed
    // one normalizes to an unnamed entry instead of loading with a dangling reference.
    for (const ToneRegion& region : tone_track.regions)
    {
        add_tone(region.tone_document_ref, std::string{});
    }

    return tones;
}

// Reads one arrangement's optional plugin-parameter automation and validates it against the
// structural rules. Musical positions are the persisted truth, so points parse as grid-position
// tokens; the runtime seconds curves are derived later by the editor.
[[nodiscard]] std::expected<std::vector<ToneParameterAutomation>, SongPackageError>
readToneAutomation(const juce::var& arrangement_json, const TempoMap& tempo_map)
{
    std::vector<ToneParameterAutomation> automation;
    const juce::var& automation_json = Json::value(arrangement_json, "toneAutomation");
    if (automation_json.isVoid() || automation_json.isUndefined())
    {
        return automation;
    }

    if (!automation_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "toneAutomation must be an array when present",
        }};
    }

    std::set<std::pair<std::string, std::string>> seen_parameters;
    automation.reserve(static_cast<std::size_t>(automation_json.size()));
    for (const juce::var& entry_json : *automation_json.getArray())
    {
        const auto plugin_id = Json::tryReadString(entry_json, "plugin");
        const auto param_id = Json::tryReadString(entry_json, "param");
        const juce::var& points_json = Json::value(entry_json, "points");
        if (!plugin_id.has_value() || !param_id.has_value() || !points_json.isArray())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation entries require plugin, param, and points fields",
            }};
        }

        ToneParameterAutomation entry{
            .plugin_id = *plugin_id,
            .param_id = *param_id,
            .points = {},
        };
        entry.points.reserve(static_cast<std::size_t>(points_json.size()));
        for (const juce::var& point_json : *points_json.getArray())
        {
            const auto position_text = Json::tryReadString(point_json, "position");
            const auto value = Json::tryReadDouble(point_json, "value");
            if (!position_text.has_value() || !value.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "toneAutomation points require position and value fields",
                }};
            }

            const auto position = parseGridPositionToken(*position_text);
            if (!position.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "toneAutomation point position is not a grid-position token: " + *position_text,
                }};
            }

            entry.points.push_back(
                ToneAutomationPoint{
                    .position = *position,
                    .norm_value = static_cast<float>(*value),
                    .curve_shape =
                        static_cast<float>(Json::tryReadDouble(point_json, "shape").value_or(0.0)),
                });
        }

        if (!isValidToneParameterAutomation(entry, tempo_map))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation entry is invalid for plugin: " + entry.plugin_id,
            }};
        }
        if (!seen_parameters.emplace(entry.plugin_id, entry.param_id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation repeats a plugin/parameter pair: " + entry.plugin_id + "/" +
                    entry.param_id,
            }};
        }
        automation.push_back(std::move(entry));
    }

    return automation;
}

// Reads arrangements from song-document entries into project-owned core values.
[[nodiscard]] std::expected<std::vector<Arrangement>, SongPackageError> readArrangements(
    const std::filesystem::path& directory, const juce::var& song_document,
    const std::unordered_map<std::string, AudioAsset>& audio_assets, const TempoMap& tempo_map)
{
    const juce::var& arrangements_json = Json::value(song_document, "arrangements");
    if (!arrangements_json.isArray() || arrangements_json.size() == 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "song.json must contain at least one arrangement",
        }};
    }

    std::vector<Arrangement> arrangements;
    arrangements.reserve(static_cast<std::size_t>(arrangements_json.size()));
    std::set<std::string> arrangement_ids;

    const juce::Array<juce::var>* const arrangement_array = arrangements_json.getArray();
    for (const juce::var& arrangement_json : *arrangement_array)
    {
        if (!arrangement_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement entries must be objects",
            }};
        }

        const auto id = Json::tryReadString(arrangement_json, "id");
        const auto part_text = Json::tryReadString(arrangement_json, "part");
        const auto audio_id = Json::tryReadString(arrangement_json, "audio");
        if (!id.has_value() || id->empty() || !part_text.has_value() || !audio_id.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement entries require non-empty id, part, and audio fields",
            }};
        }

        if (!isCanonicalPackageId(*id))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement id must be a canonical UUIDv4: " + *id,
            }};
        }

        if (!arrangement_ids.insert(*id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "duplicate arrangement id: " + *id,
            }};
        }

        const auto part = parsePart(*part_text);
        if (!part.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "unsupported arrangement part: " + *part_text,
            }};
        }

        auto tone_track = readToneTrack(directory, arrangement_json, tempo_map);
        if (!tone_track.has_value())
        {
            return std::unexpected{std::move(tone_track.error())};
        }

        auto tone_automation = readToneAutomation(arrangement_json, tempo_map);
        if (!tone_automation.has_value())
        {
            return std::unexpected{std::move(tone_automation.error())};
        }

        std::string chart_ref;
        std::optional<Chart> chart;
        const juce::var& chart_json = Json::value(arrangement_json, "chart");
        if (!chart_json.isVoid() && !chart_json.isUndefined())
        {
            chart_ref = chart_json.toString().toStdString();
            if (!chart_json.isString() || !isCanonicalChartDocumentRef(chart_ref))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chart document path must be charts/<uuid>.chart.json: " + chart_ref,
                }};
            }

            const auto chart_path = resolveExistingFile(directory, chart_ref);
            if (!chart_path.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chart document is missing or unsafe: " + chart_ref,
                }};
            }

            auto loaded_chart = readChartDocument(*chart_path);
            if (!loaded_chart.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chart document is invalid: " + loaded_chart.error().message,
                }};
            }
            if (const auto chart_rules = validateChartRules(*loaded_chart, tempo_map);
                !chart_rules.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chart document violates chart rules: " + chart_rules.error().message,
                }};
            }
            chart = std::move(*loaded_chart);
        }

        const auto audio_asset = audio_assets.find(*audio_id);
        if (audio_asset == audio_assets.end())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement references unknown audio asset: " + *audio_id,
            }};
        }

        auto tones = readToneCatalog(arrangement_json, *tone_track);
        if (!tones.has_value())
        {
            return std::unexpected{std::move(tones.error())};
        }

        arrangements.push_back(
            Arrangement{
                .id = *id,
                .part = *part,
                .difficulty = DifficultyRating{},
                .audio_asset = audio_asset->second,
                .audio_duration = TimeDuration{},
                .tones = std::move(*tones),
                .tone_track = std::move(*tone_track),
                .tone_automation = std::move(*tone_automation),
                .chart_ref = std::move(chart_ref),
                .chart = std::move(chart),
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

    const auto audio_assets = readAudioAssets(directory, song_document);
    if (!audio_assets.has_value())
    {
        return std::unexpected{audio_assets.error()};
    }

    auto tempo_map = readTempoMap(song_document);
    if (!tempo_map.has_value())
    {
        return std::unexpected{std::move(tempo_map.error())};
    }

    auto arrangements = readArrangements(directory, song_document, *audio_assets, *tempo_map);
    if (!arrangements.has_value())
    {
        return std::unexpected{std::move(arrangements.error())};
    }

    Song song;
    song.metadata = readMetadata(song_document);
    song.tempo_map = std::move(*tempo_map);
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

} // namespace rock_hero::common::core
