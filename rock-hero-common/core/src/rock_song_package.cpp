#include "rock_song_package.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <juce_core/juce_core.h>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/archive_io.h>
#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/audio_normalization.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/juce_path.h>
#include <rock_hero/common/core/package_id.h>
#include <rock_hero/common/core/tempo_map.h>
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
constexpr std::string_view g_arrangement_document_extension{".json"};
constexpr int g_zip_compression_level = 9;

// Anchor seconds are the only absolute time stored in a package, persisted at a fixed three-decimal
// (millisecond) grid. Note offsets and durations are exact beat fractions and carry no decimal grid.
// This matches the Song Data Model note in docs/design/architecture.md.
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

// Finest note subdivision the chart format stores: a note offset or duration reduces to a denominator
// of at most this value (e.g. 1/1024). This bounds authored granularity to musically meaningful
// subdivisions and keeps stored fractions small.
constexpr int g_max_fraction_denominator = 1024;

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

// Builds the canonical package-relative native arrangement document path for a stable ID.
[[nodiscard]] std::filesystem::path arrangementDocumentPath(std::string_view arrangement_id)
{
    return std::filesystem::path{std::string{g_arrangements_directory_name}} /
           (std::string{arrangement_id} + std::string{g_arrangement_document_extension});
}

// Reports whether a package-relative arrangement document path is canonical for its ID.
[[nodiscard]] bool isCanonicalArrangementDocumentRef(
    std::string_view arrangement_id, const std::string& file_ref)
{
    return file_ref == arrangementDocumentPath(arrangement_id).generic_string();
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

// Reports whether a parsed JSON integer fits a non-negative int field.
[[nodiscard]] bool fitsNonNegativeIntField(std::int64_t value) noexcept
{
    return value >= 0 && fitsIntField(value);
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

// Parses a persisted beat fraction written as a whole number ("1") or a reduced ratio ("3/16").
// The denominator must be positive; the whole token must parse with no trailing characters so a
// malformed value is rejected at the read boundary rather than silently truncated.
[[nodiscard]] std::optional<Fraction> parseFractionText(const std::string& text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::size_t slash = text.find('/');

    if (slash == std::string::npos)
    {
        int whole = 0;
        if (const auto result = std::from_chars(begin, end, whole);
            result.ec != std::errc{} || result.ptr != end)
        {
            return std::nullopt;
        }
        return Fraction{whole};
    }

    int numerator = 0;
    if (const auto result = std::from_chars(begin, begin + slash, numerator);
        result.ec != std::errc{} || result.ptr != begin + slash)
    {
        return std::nullopt;
    }

    int denominator = 0;
    if (const auto result = std::from_chars(begin + slash + 1, end, denominator);
        result.ec != std::errc{} || result.ptr != end || denominator <= 0)
    {
        return std::nullopt;
    }

    return Fraction{numerator, denominator};
}

// Reports whether a denominator is a positive power of two, matching conventional meter values.
[[nodiscard]] bool isPowerOfTwoDenominator(int denominator) noexcept
{
    return denominator > 0 && (denominator & (denominator - 1)) == 0;
}

// Reports whether a JSON value represents an absent optional property.
[[nodiscard]] bool isAbsentJsonValue(const juce::var& value) noexcept
{
    return value.isVoid() || value.isUndefined();
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

        const auto measure = Json::tryReadInt64(anchor_json, "measure");
        const auto beat = Json::tryReadInt64(anchor_json, "beat");
        const auto seconds = Json::tryReadDouble(anchor_json, "seconds");
        if (!measure.has_value() || !beat.has_value() || !seconds.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors entries require measure, beat, and seconds",
            }};
        }

        if (!fitsPositiveIntField(*measure) || !fitsPositiveIntField(*beat))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors measure and beat values must be positive integers",
            }};
        }

        anchors.push_back(
            BeatAnchor{
                .measure = static_cast<int>(*measure),
                .beat = static_cast<int>(*beat),
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

// Uniquely identifies one playable string at one exact onset position. The offset is stored as its
// reduced numerator and denominator, so equal positions compare equal regardless of how they were
// written (1/2 and 2/4 both reduce to the same key).
struct NoteStringOnsetKey
{
    std::int64_t global_beat_index{};
    int offset_numerator{};
    int offset_denominator{1};
    int string_number{};

    // Default ordering is sufficient for std::set duplicate detection; it does not need to match the
    // numeric order of the underlying onset, only to give each distinct onset a unique key.
    friend auto operator<=>(const NoteStringOnsetKey& lhs, const NoteStringOnsetKey& rhs) = default;
};

// Builds the exact onset key for a validated note, combining its global beat with its reduced offset.
[[nodiscard]] NoteStringOnsetKey noteStringOnsetKey(
    const NoteEvent& note, const TempoMap& tempo_map)
{
    return NoteStringOnsetKey{
        .global_beat_index = tempo_map.globalBeatIndex(note.measure, note.beat),
        .offset_numerator = note.offset.numerator,
        .offset_denominator = note.offset.denominator,
        .string_number = note.string_number,
    };
}

// Validates a single in-memory note against the core invariants enforced on read, so the writer
// never emits an arrangement document its own reader would reject.
[[nodiscard]] std::expected<void, SongPackageError> validateNoteEvent(
    const NoteEvent& note, const TempoMap& tempo_map)
{
    if (note.measure <= 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note measure must be a positive integer",
        }};
    }

    const int beats_per_measure = tempo_map.beatsPerMeasureAt(note.measure);
    if (note.beat <= 0 || note.beat > beats_per_measure)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note beat must fit inside the active measure",
        }};
    }

    if (note.offset < Fraction{} || note.offset >= Fraction{1})
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note offset must be a beat fraction in [0, 1)",
        }};
    }

    if (note.duration_beats < Fraction{})
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note durationBeats must be a non-negative beat fraction",
        }};
    }

    if (note.offset.denominator > g_max_fraction_denominator ||
        note.duration_beats.denominator > g_max_fraction_denominator)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note offset and durationBeats must reduce to a denominator of at most " +
                std::to_string(g_max_fraction_denominator),
        }};
    }

    if (note.string_number <= 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note string must be a positive integer",
        }};
    }

    if (note.fret < 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note fret must be a non-negative integer",
        }};
    }

    const std::int64_t note_start = tempo_map.globalBeatIndex(note.measure, note.beat);
    const std::int64_t terminal_beat = tempo_map.terminalGlobalBeatIndex();
    if (note_start >= terminal_beat)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note must start before the tempo map terminal anchor",
        }};
    }

    const double note_end =
        static_cast<double>(note_start) + note.offset.toDouble() + note.duration_beats.toDouble();
    if (note_end > static_cast<double>(terminal_beat) + g_timing_epsilon)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "note sustain must end at or before the tempo map terminal anchor",
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Validates cross-note invariants that require seeing the whole arrangement.
[[nodiscard]] std::expected<void, SongPackageError> validateArrangementNoteEvents(
    const std::vector<NoteEvent>& notes, const TempoMap& tempo_map)
{
    std::set<NoteStringOnsetKey> occupied_string_onsets;
    for (const NoteEvent& note : notes)
    {
        if (const auto note_error = validateNoteEvent(note, tempo_map); !note_error.has_value())
        {
            return std::unexpected{note_error.error()};
        }

        const auto inserted_onset =
            occupied_string_onsets.insert(noteStringOnsetKey(note, tempo_map));
        if (!inserted_onset.second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "notes on the same string cannot share the same onset",
            }};
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Reads the note objects from a parsed arrangement document into core note events. Note values are
// validated here, at the read/import boundary, because that is where untrusted note data enters the
// model. The write path repeats only the same core invariants so it never emits a document this
// reader would reject.
[[nodiscard]] std::expected<std::vector<NoteEvent>, SongPackageError> readArrangementNotes(
    const juce::var& arrangement_document, const TempoMap& tempo_map)
{
    const juce::var& notes_json = Json::value(arrangement_document, "notes");
    if (!notes_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "arrangement document notes must be an array",
        }};
    }

    std::vector<NoteEvent> notes;
    notes.reserve(static_cast<std::size_t>(notes_json.size()));

    const juce::Array<juce::var>* const note_array = notes_json.getArray();
    for (const juce::var& note_json : *note_array)
    {
        if (!note_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement document notes must be objects",
            }};
        }

        const auto measure = Json::tryReadInt64(note_json, "measure");
        const auto beat = Json::tryReadInt64(note_json, "beat");
        const auto duration_text = Json::tryReadString(note_json, "durationBeats");
        const auto string_number = Json::tryReadInt64(note_json, "string");
        const auto fret = Json::tryReadInt64(note_json, "fret");
        if (!measure.has_value() || !beat.has_value() || !duration_text.has_value() ||
            !string_number.has_value() || !fret.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "each note requires measure, beat, durationBeats, string, and fret",
            }};
        }

        if (!fitsPositiveIntField(*measure) || !fitsPositiveIntField(*beat))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "note measure and beat must be positive integers",
            }};
        }

        const auto duration_beats = parseFractionText(*duration_text);
        if (!duration_beats.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                R"(note durationBeats must be a beat fraction such as "1" or "1/8")",
            }};
        }

        Fraction offset;
        const juce::var& offset_json = Json::value(note_json, "offset");
        if (!isAbsentJsonValue(offset_json))
        {
            const auto offset_text = Json::tryReadString(note_json, "offset");
            const auto parsed_offset =
                offset_text.has_value() ? parseFractionText(*offset_text) : std::nullopt;
            if (!parsed_offset.has_value())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    R"(note offset must be a beat fraction such as "1/3" when present)",
                }};
            }
            offset = *parsed_offset;
        }

        if (!fitsPositiveIntField(*string_number))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "note string must be a positive integer",
            }};
        }

        if (!fitsNonNegativeIntField(*fret))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "note fret must be a non-negative integer",
            }};
        }

        NoteEvent note{
            .measure = static_cast<int>(*measure),
            .beat = static_cast<int>(*beat),
            .offset = offset,
            .duration_beats = *duration_beats,
            .string_number = static_cast<int>(*string_number),
            .fret = static_cast<int>(*fret),
        };

        notes.push_back(note);
    }

    if (const auto note_error = validateArrangementNoteEvents(notes, tempo_map);
        !note_error.has_value())
    {
        return std::unexpected{note_error.error()};
    }

    return notes;
}

// Opens, parses, and validates an arrangement document file, returning its note events.
[[nodiscard]] std::expected<std::vector<NoteEvent>, SongPackageError> readArrangementDocumentFile(
    const std::filesystem::path& document_path, const TempoMap& tempo_map)
{
    juce::FileInputStream document_file{juceFileFromPath(document_path)};
    if (document_file.failedToOpen())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "Could not open arrangement document: " + document_path.string(),
        }};
    }

    const juce::String document_text = document_file.readEntireStreamAsString();
    auto parsed_document = Json::parseDocument(document_text);
    if (!parsed_document.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "Could not parse arrangement document: " + parsed_document.error().message,
        }};
    }

    const juce::var arrangement_document = std::move(*parsed_document);
    const auto format_version = Json::readOptionalInt(arrangement_document, "formatVersion", 0);
    if (!arrangement_document.isObject() || format_version != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "Unsupported arrangement document formatVersion",
        }};
    }

    return readArrangementNotes(arrangement_document, tempo_map);
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
        const auto arrangement_document = Json::tryReadString(arrangement_json, "file");
        const auto audio_id = Json::tryReadString(arrangement_json, "audio");
        std::string tone_document_ref;
        if (!id.has_value() || id->empty() || !part_text.has_value() ||
            !arrangement_document.has_value() || !audio_id.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement entries require non-empty id, part, file, and audio fields",
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

        if (!isCanonicalArrangementDocumentRef(*id, *arrangement_document))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement document must match arrangement id: " + *arrangement_document,
            }};
        }

        const auto resolved_arrangement_document =
            resolveExistingFile(directory, *arrangement_document);
        if (!resolved_arrangement_document.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement document is missing or unsafe: " + *arrangement_document,
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

        auto note_events = readArrangementDocumentFile(*resolved_arrangement_document, tempo_map);
        if (!note_events.has_value())
        {
            return std::unexpected{std::move(note_events.error())};
        }

        // Difficulty is intentionally not read here. It is a value derived from the chart (see
        // docs/todo/arrangement-difficulty-derivation-plan.md), not authored persisted data, so it
        // stays at its Unknown default until the difficulty calculator is added.
        arrangements.push_back(
            Arrangement{
                .id = *id,
                .part = *part,
                .difficulty = DifficultyRating{},
                .audio_asset = audio_asset->second,
                .audio_duration = TimeDuration{},
                .tone_document_ref = std::move(tone_document_ref),
                .note_events = std::move(*note_events),
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

// Renders a beat fraction as compact text: a whole number ("0", "4") or a reduced ratio ("1/8").
[[nodiscard]] std::string fractionToText(Fraction value)
{
    if (value.denominator == 1)
    {
        return std::to_string(value.numerator);
    }

    return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
}

// Renders a beat fraction as a quoted JSON string token so the chart stores exact subdivisions.
[[nodiscard]] std::string quotedFractionText(Fraction value)
{
    return "\"" + fractionToText(value) + "\"";
}

// Widths of the note fields, so rendered note lines align into readable columns.
struct NoteColumnWidths
{
    std::size_t measure{0};
    std::size_t beat{0};
    std::size_t offset{0};
    std::size_t duration_beats{0};
    std::size_t fret{0};
};

// Measures note fields before writing so the arrangement chart scans like a compact table.
[[nodiscard]] NoteColumnWidths measureNoteColumns(const std::vector<NoteEvent>& notes)
{
    NoteColumnWidths widths;
    for (const NoteEvent& note : notes)
    {
        widths.measure = std::max(widths.measure, std::to_string(note.measure).size());
        widths.beat = std::max(widths.beat, std::to_string(note.beat).size());
        if (note.offset != Fraction{})
        {
            widths.offset = std::max(widths.offset, quotedFractionText(note.offset).size());
        }
        widths.duration_beats =
            std::max(widths.duration_beats, quotedFractionText(note.duration_beats).size());
        widths.fret = std::max(widths.fret, std::to_string(note.fret).size());
    }

    return widths;
}

// Renders one note as a single compact JSON object line, right-padding the core fields into fixed
// columns so the chart scans as a table. Offset and durationBeats are quoted fraction tokens (digits
// and a slash only, so no JSON escaping is required); the remaining fields are plain integers.
[[nodiscard]] std::string formatNoteLine(const NoteEvent& note, const NoteColumnWidths& widths)
{
    std::string line = "{ \"measure\": ";
    line += std::format("{:>{}}", note.measure, widths.measure);
    line += ", \"beat\": ";
    line += std::format("{:>{}}", note.beat, widths.beat);
    if (note.offset != Fraction{})
    {
        line += ", \"offset\": ";
        line += std::format("{:>{}}", quotedFractionText(note.offset), widths.offset);
    }
    line += ", \"durationBeats\": ";
    line += std::format("{:>{}}", quotedFractionText(note.duration_beats), widths.duration_beats);
    line += ", \"string\": ";
    line += std::to_string(note.string_number);
    line += ", \"fret\": ";
    line += std::format("{:>{}}", note.fret, widths.fret);
    line += " }";

    return line;
}

// Builds the JSON text of an arrangement document: a format version and the one-line-per-note chart.
[[nodiscard]] std::string arrangementDocumentContents(const Arrangement& arrangement)
{
    const NoteColumnWidths widths = measureNoteColumns(arrangement.note_events);

    std::string contents = "{\n  \"formatVersion\": 1,\n  \"notes\": [";
    for (std::size_t index = 0; index < arrangement.note_events.size(); ++index)
    {
        contents += (index == 0 ? "\n    " : ",\n    ");
        contents += formatNoteLine(arrangement.note_events[index], widths);
    }
    contents += arrangement.note_events.empty() ? "]\n}\n" : "\n  ]\n}\n";

    return contents;
}

// Widths of time-signature fields used by song document formatting.
struct TimeSignatureColumnWidths
{
    std::size_t measure{0};
    std::size_t numerator{0};
    std::size_t denominator{0};
};

// Measures time-signature fields before writing one JSON object per line.
[[nodiscard]] TimeSignatureColumnWidths measureTimeSignatureColumns(
    const std::vector<TimeSignatureChange>& time_signatures)
{
    TimeSignatureColumnWidths widths;
    for (const TimeSignatureChange& signature : time_signatures)
    {
        widths.measure = std::max(widths.measure, std::to_string(signature.measure).size());
        widths.numerator = std::max(widths.numerator, std::to_string(signature.numerator).size());
        widths.denominator =
            std::max(widths.denominator, std::to_string(signature.denominator).size());
    }

    return widths;
}

// Renders one time-signature change as a compact object line.
[[nodiscard]] std::string formatTimeSignatureLine(
    const TimeSignatureChange& signature, const TimeSignatureColumnWidths& widths)
{
    std::string line = "{ \"measure\": ";
    line += std::format("{:>{}}", signature.measure, widths.measure);
    line += ", \"numerator\": ";
    line += std::format("{:>{}}", signature.numerator, widths.numerator);
    line += ", \"denominator\": ";
    line += std::format("{:>{}}", signature.denominator, widths.denominator);
    line += " }";

    return line;
}

// Widths of anchor fields used by song document formatting.
struct AnchorColumnWidths
{
    std::size_t measure{0};
    std::size_t beat{0};
    std::size_t seconds{0};
};

// Measures anchor fields before writing one JSON object per line.
[[nodiscard]] AnchorColumnWidths measureAnchorColumns(const std::vector<BeatAnchor>& anchors)
{
    AnchorColumnWidths widths;
    for (const BeatAnchor& anchor : anchors)
    {
        widths.measure = std::max(widths.measure, std::to_string(anchor.measure).size());
        widths.beat = std::max(widths.beat, std::to_string(anchor.beat).size());
        widths.seconds = std::max(widths.seconds, formatTimingValue(anchor.seconds).size());
    }

    return widths;
}

// Renders one beat anchor as a compact object line.
[[nodiscard]] std::string formatAnchorLine(
    const BeatAnchor& anchor, const AnchorColumnWidths& widths)
{
    std::string line = "{ \"measure\": ";
    line += std::format("{:>{}}", anchor.measure, widths.measure);
    line += ", \"beat\": ";
    line += std::format("{:>{}}", anchor.beat, widths.beat);
    line += ", \"seconds\": ";
    line += std::format("{:>{}}", formatTimingValue(anchor.seconds), widths.seconds);
    line += " }";

    return line;
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
    std::string file;
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
        line += ", \"normalization\": { \"gainDb\": ";
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
    line += ", \"file\": ";
    line += jsonString(entry.file);
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
    const TimeSignatureColumnWidths time_signature_widths =
        measureTimeSignatureColumns(tempo_map.timeSignatures());
    const AnchorColumnWidths anchor_widths = measureAnchorColumns(tempo_map.anchors());

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
        contents +=
            formatTimeSignatureLine(tempo_map.timeSignatures()[index], time_signature_widths);
    }
    contents += tempo_map.timeSignatures().empty() ? "],\n" : "\n    ],\n";
    contents += "    \"anchors\": [";
    for (std::size_t index = 0; index < tempo_map.anchors().size(); ++index)
    {
        contents += (index == 0 ? "\n      " : ",\n      ");
        contents += formatAnchorLine(tempo_map.anchors()[index], anchor_widths);
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

// Writes an arrangement document with the arrangement's current notes, overwriting any prior
// document so the in-memory song is the source of truth on save.
[[nodiscard]] std::expected<void, SongPackageError> writeArrangementDocument(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& relative_path,
    const Arrangement& arrangement)
{
    const std::filesystem::path arrangement_path = workspace_directory / relative_path;
    std::error_code error;
    std::filesystem::create_directories(arrangement_path.parent_path(), error);
    if (error)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Could not create arrangement directory: " + error.message(),
        }};
    }

    std::ofstream arrangement_document{arrangement_path};
    if (!arrangement_document.is_open())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Could not write arrangement document: " + arrangement_path.string(),
        }};
    }

    arrangement_document << arrangementDocumentContents(arrangement);
    if (!arrangement_document.good())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::CouldNotWriteSongDocument,
            "Could not write arrangement document: " + arrangement_path.string(),
        }};
    }

    return std::expected<void, SongPackageError>{};
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
        // validation failure does not leave copied audio or a written arrangement document behind.
        if (const auto tone_error =
                validateArrangementToneReference(workspace_directory, arrangement);
            !tone_error.has_value())
        {
            return std::unexpected{tone_error.error()};
        }
        if (const auto note_error =
                validateArrangementNoteEvents(arrangement.note_events, song.tempo_map);
            !note_error.has_value())
        {
            return std::unexpected{note_error.error()};
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

        const std::filesystem::path arrangement_document_path =
            arrangementDocumentPath(*arrangement_id);

        if (const auto arrangement_error = writeArrangementDocument(
                workspace_directory, arrangement_document_path, arrangement);
            !arrangement_error.has_value())
        {
            return std::unexpected{arrangement_error.error()};
        }

        arrangements.push_back(
            ArrangementDocumentEntry{
                .id = *arrangement_id,
                .part = partName(arrangement.part),
                .file = arrangement_document_path.generic_string(),
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

    std::ofstream song_document_file{song_directory / g_song_document_name};
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
