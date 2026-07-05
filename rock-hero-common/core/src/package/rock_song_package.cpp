#include "package/rock_song_package.h"

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

constexpr std::string_view g_song_document_name{"song.json"};
constexpr int g_zip_compression_level = 9;

// Anchor seconds are the only absolute time stored in a package, persisted at a fixed three-decimal
// (millisecond) grid. This matches the Song Data Model note in docs/design/architecture.md.
constexpr int g_timing_decimals = 3;

// Decimal quantum (10^g_timing_decimals) used to verify that anchor seconds are exactly on the
// persisted grid. Computed from g_timing_decimals so the on-grid check can never drift from the
// writer's fixed-precision output.
constexpr int g_seconds_grid_units = [] {
    int units = 1;
    for (int decimal = 0; decimal < g_timing_decimals; ++decimal)
    {
        units *= 10;
    }
    return units;
}();

constexpr double g_timing_epsilon = 1.0e-9;

// Lightweight parsed form of a persisted tempo-map anchor address.
struct AnchorPosition
{
    int measure{1};
    int beat{1};
};

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

        const auto normalization =
            readOptionalNormalization(Json::value(asset_json, "normalization"));

        const auto inserted = audio_assets.emplace(
            *id,
            AudioAsset{
                .path = resolved_path->lexically_normal(),
                .normalization = normalization,
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

// Reports whether a timing value can be stored safely in the native format.
[[nodiscard]] bool isValidTimingValue(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

// Rounds a seconds value to the integer quantum used by the persisted three-decimal format.
[[nodiscard]] std::int64_t secondsGridUnits(double value) noexcept
{
    return static_cast<std::int64_t>(
        std::llround(value * static_cast<double>(g_seconds_grid_units)));
}

// Reports whether a seconds value is already on the persisted three-decimal grid.
[[nodiscard]] bool isOnSecondsGrid(double value) noexcept
{
    if (!std::isfinite(value))
    {
        return false;
    }

    const double grid_value =
        static_cast<double>(secondsGridUnits(value)) / static_cast<double>(g_seconds_grid_units);
    return std::abs(value - grid_value) <= g_timing_epsilon;
}

// Parses a tempo-map anchor position token: "<measure>:<beat>".
[[nodiscard]] std::optional<AnchorPosition> parseAnchorPosition(const std::string& text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || text.find('+') != std::string::npos)
    {
        return std::nullopt;
    }

    int measure = 0;
    if (const auto result = std::from_chars(text.data(), text.data() + colon, measure);
        result.ec != std::errc{} || result.ptr != text.data() + colon || measure <= 0)
    {
        return std::nullopt;
    }

    const char* const beat_begin = text.data() + colon + 1;
    const char* const beat_end = text.data() + text.size();

    int beat = 0;
    if (const auto result = std::from_chars(beat_begin, beat_end, beat);
        result.ec != std::errc{} || result.ptr != beat_end || beat <= 0)
    {
        return std::nullopt;
    }

    return AnchorPosition{.measure = measure, .beat = beat};
}

// Reports whether a denominator is a positive power of two, matching conventional meter values.
[[nodiscard]] bool isPowerOfTwoDenominator(int denominator) noexcept
{
    return denominator > 0 && (denominator & (denominator - 1)) == 0;
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

        const auto position = parseAnchorPosition(*position_text);
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
[[nodiscard]] std::expected<void, SongPackageError> validateTempoMap(const TempoMap& tempo_map)
{
    const std::vector<TimeSignatureChange>& time_signatures = tempo_map.timeSignatures();
    if (time_signatures.empty())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.timeSignatures must contain at least one entry",
        }};
    }

    if (time_signatures.front().measure != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.timeSignatures must start at measure 1",
        }};
    }

    int previous_measure = 0;
    for (const TimeSignatureChange& signature : time_signatures)
    {
        if (signature.measure <= previous_measure || signature.numerator <= 0 ||
            !isPowerOfTwoDenominator(signature.denominator))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.timeSignatures must be strictly ordered valid meters",
            }};
        }
        previous_measure = signature.measure;
    }

    const std::vector<BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.anchors must contain start and terminal anchors",
        }};
    }

    if (anchors.front().measure != 1 || anchors.front().beat != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap.anchors must start at measure 1 beat 1",
        }};
    }

    std::int64_t previous_index = -1;
    double previous_seconds = -1.0;
    for (const BeatAnchor& anchor : anchors)
    {
        if (anchor.measure <= 0 || anchor.beat <= 0 || !isValidTimingValue(anchor.seconds))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors must use positive addresses and finite non-negative seconds",
            }};
        }

        if (!isOnSecondsGrid(anchor.seconds))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors seconds must use three-decimal storage precision",
            }};
        }

        if (anchor.beat > tempo_map.beatsPerMeasureAt(anchor.measure))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors beat exceeds the active measure length",
            }};
        }

        const std::int64_t anchor_index = tempo_map.globalBeatIndex(anchor.measure, anchor.beat);
        if (anchor_index <= previous_index || anchor.seconds <= previous_seconds)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors must be strictly ordered by beat and seconds",
            }};
        }

        previous_index = anchor_index;
        previous_seconds = anchor.seconds;
    }

    if (anchors.back().beat != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap terminal anchor must be on a downbeat",
        }};
    }

    return std::expected<void, SongPackageError>{};
}

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

// Reads arrangements from song-document entries into project-owned core values.
[[nodiscard]] std::expected<std::vector<Arrangement>, SongPackageError> readArrangements(
    const std::filesystem::path& directory, const juce::var& song_document,
    const std::unordered_map<std::string, AudioAsset>& audio_assets)
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
        std::string tone_document_ref;
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

        const juce::var& tone_document_json = Json::value(arrangement_json, "toneDocument");
        if (!tone_document_json.isVoid() && !tone_document_json.isUndefined())
        {
            if (!tone_document_json.isString() || tone_document_json.toString().isEmpty())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "arrangement toneDocument must be a non-empty string when present",
                }};
            }

            tone_document_ref = tone_document_json.toString().toStdString();
            if (!isCanonicalToneDocumentRef(tone_document_ref))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "tone document path must be tones/<uuid>/tone.json: " + tone_document_ref,
                }};
            }

            if (!resolveExistingFile(directory, tone_document_ref).has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "tone document is missing or unsafe: " + tone_document_ref,
                }};
            }
        }

        const auto audio_asset = audio_assets.find(*audio_id);
        if (audio_asset == audio_assets.end())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement references unknown audio asset: " + *audio_id,
            }};
        }

        arrangements.push_back(
            Arrangement{
                .id = *id,
                .part = *part,
                .difficulty = DifficultyRating{},
                .audio_asset = audio_asset->second,
                .audio_duration = TimeDuration{},
                .tone_document_ref = std::move(tone_document_ref),
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
    return R"({ "position": ")" + std::to_string(anchor.measure) + ":" +
           std::to_string(anchor.beat) + R"(", "seconds": )" + formatTimingValue(anchor.seconds) +
           " }";
}

// Song-document audio entry retained between validation and final JSON formatting.
struct AudioAssetDocumentEntry
{
    std::string id;
    std::string path;
    std::optional<AudioNormalization> normalization;
};

// Song-document arrangement entry retained between validation and final JSON formatting.
struct ArrangementDocumentEntry
{
    std::string id;
    std::string part;
    std::string audio;
    std::string tone_document;
};

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
    if (!entry.tone_document.empty())
    {
        line += ", \"toneDocument\": ";
        line += jsonString(entry.tone_document);
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

// Validates an arrangement's tone document reference (canonical, safe, present) without writing
// anything, so a bad reference fails a save before any side effect occurs.
[[nodiscard]] std::expected<void, SongPackageError> validateArrangementToneReference(
    const std::filesystem::path& workspace_directory, const Arrangement& arrangement)
{
    if (arrangement.tone_document_ref.empty())
    {
        return std::expected<void, SongPackageError>{};
    }

    const std::filesystem::path tone_document_path{arrangement.tone_document_ref};
    if (!isSafeRelativePath(tone_document_path) ||
        !isCanonicalToneDocumentRef(arrangement.tone_document_ref))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a non-canonical tone document path: " + arrangement.tone_document_ref,
        }};
    }

    std::error_code tone_document_error;
    const std::filesystem::path resolved_tone_document_path =
        (workspace_directory / tone_document_path).lexically_normal();
    if (!std::filesystem::is_regular_file(resolved_tone_document_path, tone_document_error))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Cannot save a missing tone document: " + arrangement.tone_document_ref,
        }};
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
                validateArrangementToneReference(workspace_directory, arrangement);
            !tone_error.has_value())
        {
            return std::unexpected{tone_error.error()};
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
                });
            audio_id = audio_ids_by_path.emplace(relative_audio_name, generated_id).first;
        }

        arrangements.push_back(
            ArrangementDocumentEntry{
                .id = *arrangement_id,
                .part = partName(arrangement.part),
                .audio = audio_id->second,
                .tone_document = arrangement.tone_document_ref,
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

    auto arrangements = readArrangements(directory, song_document, *audio_assets);
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
