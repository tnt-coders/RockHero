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
#include <variant>
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

// Parses a grid-position token: "<measure>:<beat>" or "<measure>:<beat>+<fraction>". Measure and
// beat are positive integers; the optional sub-beat offset is a reduced fraction in (0, 1) joined
// with '+'. A zero or whole offset is rejected so the canonical form (offset omitted) is the only
// accepted spelling, and the offset denominator is bounded like every stored note fraction.
[[nodiscard]] std::optional<GridPosition> parseGridPosition(const std::string& text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }

    int measure = 0;
    if (const auto result = std::from_chars(text.data(), text.data() + colon, measure);
        result.ec != std::errc{} || result.ptr != text.data() + colon || measure <= 0)
    {
        return std::nullopt;
    }

    const std::size_t plus = text.find('+', colon + 1);
    const char* const beat_begin = text.data() + colon + 1;
    const char* const beat_end =
        plus == std::string::npos ? text.data() + text.size() : text.data() + plus;

    int beat = 0;
    if (const auto result = std::from_chars(beat_begin, beat_end, beat);
        result.ec != std::errc{} || result.ptr != beat_end || beat <= 0)
    {
        return std::nullopt;
    }

    Fraction offset;
    if (plus != std::string::npos)
    {
        const auto parsed_offset = parseFractionText(text.substr(plus + 1));
        if (!parsed_offset.has_value() || *parsed_offset <= Fraction{} ||
            *parsed_offset >= Fraction{1} ||
            parsed_offset->denominator > g_max_fraction_denominator)
        {
            return std::nullopt;
        }
        offset = *parsed_offset;
    }

    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// Maps a pitch class (0=C..11=B) to its label: both enharmonic spellings for the five accidental
// pitch classes and a single name for naturals, matching the dual-spelling note-label convention.
[[nodiscard]] std::string pitchClassLabel(int pitch_class)
{
    static constexpr std::array<std::string_view, 12> names{
        "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"
    };
    return std::string{names[static_cast<std::size_t>(pitch_class)]};
}

// Renders a MIDI note number as a scientific-pitch label with octave, with middle C = C4.
[[nodiscard]] std::string pitchLabel(int midi)
{
    const int pitch_class = ((midi % 12) + 12) % 12;
    const int octave = (midi - pitch_class) / 12 - 1;
    return pitchClassLabel(pitch_class) + std::to_string(octave);
}

// Parses a canonical scientific-pitch label such as "E2" or "F#/Gb1" into a MIDI note number
// (middle C = C4 = 60). Accidental pitch classes use the same dual-spelling label that the writer
// emits, keeping persisted tuning entries independent of any standard-tuning baseline.
[[nodiscard]] std::optional<int> parsePitchToMidi(const std::string& text)
{
    for (int pitch_class = 0; pitch_class < 12; ++pitch_class)
    {
        const std::string pitch_class_text = pitchClassLabel(pitch_class);
        if (!text.starts_with(pitch_class_text))
        {
            continue;
        }

        const char* const octave_begin = text.data() + pitch_class_text.size();
        const char* const octave_end = text.data() + text.size();
        int octave = 0;
        if (const auto result = std::from_chars(octave_begin, octave_end, octave);
            result.ec == std::errc{} && result.ptr == octave_end)
        {
            return (octave + 1) * 12 + pitch_class;
        }
    }

    return std::nullopt;
}

// Derives the display pitch label for a single note from the arrangement tuning. The label is
// writer-owned: string and fret are the source of truth, so readers validate it but never store it.
// Returns empty only for an out-of-range string or invalid tuning, which validation rejects.
[[nodiscard]] std::string noteLabel(const Tuning& tuning, int string_number, int fret)
{
    const auto index = static_cast<std::size_t>(string_number - 1);
    if (string_number <= 0 || index >= tuning.open_strings.size())
    {
        return {};
    }

    const auto open_midi = parsePitchToMidi(tuning.open_strings[index]);
    if (!open_midi.has_value())
    {
        return {};
    }

    return pitchLabel(*open_midi + fret);
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

        const auto position_text = Json::tryReadString(anchor_json, "position");
        const auto seconds = Json::tryReadDouble(anchor_json, "seconds");
        if (!position_text.has_value() || !seconds.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidSongDocument,
                "tempoMap.anchors entries require position and seconds",
            }};
        }

        // An anchor pins a whole beat, so its token is always on the beat with no sub-beat offset.
        const auto position = parseGridPosition(*position_text);
        if (!position.has_value() || position->offset != Fraction{})
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

// Identifies one struck string at one exact grid onset. The offset is stored as its reduced numerator
// and denominator, so equal positions compare equal regardless of how they were written (1/2 and 2/4
// reduce to the same key).
struct StringOnsetKey
{
    std::int64_t global_beat_index{};
    int offset_numerator{};
    int offset_denominator{1};
    int string_number{};

    // Default ordering is sufficient for std::set duplicate detection; it only needs to give each
    // distinct (onset, string) a unique key, not to match the numeric order of the onset.
    friend auto operator<=>(const StringOnsetKey& lhs, const StringOnsetKey& rhs) = default;
};

// Builds the onset key for one struck string at a grid position.
[[nodiscard]] StringOnsetKey stringOnsetKey(
    const GridPosition& position, int string_number, const TempoMap& tempo_map)
{
    return StringOnsetKey{
        .global_beat_index = tempo_map.globalBeatIndex(position.measure, position.beat),
        .offset_numerator = position.offset.numerator,
        .offset_denominator = position.offset.denominator,
        .string_number = string_number,
    };
}

// Locates a grid position on the global beat timeline as an exact (global beat index, offset) pair, so
// terminal-bound and start/end ordering checks compare positions exactly rather than through seconds.
[[nodiscard]] std::pair<std::int64_t, Fraction> globalBeatPosition(
    const GridPosition& position, const TempoMap& tempo_map)
{
    return {tempo_map.globalBeatIndex(position.measure, position.beat), position.offset};
}

// Validates a grid position's fields against the tempo map: a positive measure, a beat inside the
// active meter, and an in-range, denominator-bounded sub-beat offset. Used on both read and save, so
// it cannot rely on the read-time token parser having already checked these.
[[nodiscard]] std::expected<void, SongPackageError> validateGridPosition(
    const GridPosition& position, const TempoMap& tempo_map)
{
    if (position.measure <= 0 || position.beat <= 0 ||
        position.beat > tempo_map.beatsPerMeasureAt(position.measure))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event position must use a positive measure and a beat inside the active meter",
        }};
    }

    // Guards the save path against a directly mutated Fraction whose denominator broke the value
    // type's positive-denominator invariant; the range comparison below relies on it.
    if (position.offset.denominator <= 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event position offset must have a positive denominator",
        }};
    }

    if (position.offset < Fraction{} || position.offset >= Fraction{1} ||
        position.offset.denominator > g_max_fraction_denominator)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event position offset must be a beat fraction in [0, 1)",
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Validates an optional sustain end against its event start and the grid terminal boundary.
[[nodiscard]] std::expected<void, SongPackageError> validateEventEnd(
    const GridPosition& end, const std::pair<std::int64_t, Fraction>& start_position,
    const std::pair<std::int64_t, Fraction>& terminal_position, const TempoMap& tempo_map)
{
    if (const auto end_error = validateGridPosition(end, tempo_map); !end_error.has_value())
    {
        return std::unexpected{std::move(end_error.error())};
    }

    const auto end_position = globalBeatPosition(end, tempo_map);
    if (!(end_position > start_position))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event end must come after its start",
        }};
    }

    if (end_position > terminal_position)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event end must be at or before the tempo map terminal anchor",
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Validates a chord template's voicing: non-empty id/name/voicing, in-range unique strings,
// non-negative frets, and fingers in 1-4. Runs on both read and save so neither path can emit a
// template the reader would reject.
[[nodiscard]] std::expected<void, SongPackageError> validateChordTemplate(
    const ChordTemplate& chord_template, std::size_t string_count)
{
    if (chord_template.id.empty() || chord_template.name.empty() || chord_template.voicing.empty())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "chord template requires a non-empty id, name, and voicing",
        }};
    }

    std::set<int> voicing_strings;
    for (const ChordVoicingString& voiced : chord_template.voicing)
    {
        if (voiced.string_number <= 0 ||
            static_cast<std::size_t>(voiced.string_number) > string_count || voiced.fret < 0 ||
            (voiced.finger.has_value() && (*voiced.finger < 1 || *voiced.finger > 4)))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "chord voicing needs strings present in the tuning, non-negative frets, and "
                "fingers in 1-4",
            }};
        }

        if (!voicing_strings.insert(voiced.string_number).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "chord voicing repeats a string",
            }};
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Validates one chart event against the grid, the chord templates, and the tuning, recording every
// (onset, string) it strikes so duplicate same-string onsets are rejected across the arrangement. A
// single note carries its own string/fret; a chord strikes every string in its referenced template.
[[nodiscard]] std::expected<void, SongPackageError> validateChartEvent(
    const ChartEvent& event,
    const std::unordered_map<std::string, const ChordTemplate*>& templates_by_id,
    std::size_t string_count, const TempoMap& tempo_map, std::set<StringOnsetKey>& occupied_onsets)
{
    if (const auto start_error = validateGridPosition(event.start, tempo_map);
        !start_error.has_value())
    {
        return std::unexpected{std::move(start_error.error())};
    }

    const std::pair<std::int64_t, Fraction> terminal_position{
        tempo_map.terminalGlobalBeatIndex(), Fraction{}
    };
    const auto start_position = globalBeatPosition(event.start, tempo_map);
    if (!(start_position < terminal_position))
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event must start before the tempo map terminal anchor",
        }};
    }

    if (event.end.has_value())
    {
        if (const auto end_error =
                validateEventEnd(*event.end, start_position, terminal_position, tempo_map);
            !end_error.has_value())
        {
            return std::unexpected{std::move(end_error.error())};
        }
    }

    // Records one struck string at the event onset, rejecting a second strike of the same string.
    const auto occupy_string = [&](int string_number) -> std::expected<void, SongPackageError> {
        if (!occupied_onsets.insert(stringOnsetKey(event.start, string_number, tempo_map)).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "two events cannot strike the same string at the same onset",
            }};
        }
        return std::expected<void, SongPackageError>{};
    };

    if (const auto* single_note = std::get_if<SingleNote>(&event.content))
    {
        if (single_note->string_number <= 0 ||
            static_cast<std::size_t>(single_note->string_number) > string_count)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "single-note string must be a string present in the tuning",
            }};
        }

        if (single_note->fret < 0)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "single-note fret must be a non-negative integer",
            }};
        }

        return occupy_string(single_note->string_number);
    }

    const ChordInstance& chord = std::get<ChordInstance>(event.content);
    const auto template_entry = templates_by_id.find(chord.template_id);
    if (template_entry == templates_by_id.end())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "chord event references an unknown chord template: " + chord.template_id,
        }};
    }

    std::set<int> voicing_strings;
    for (const ChordVoicingString& voiced : template_entry->second->voicing)
    {
        voicing_strings.insert(voiced.string_number);
        if (const auto occupied = occupy_string(voiced.string_number); !occupied.has_value())
        {
            return std::unexpected{std::move(occupied.error())};
        }
    }

    std::set<int> deviating_strings;
    for (const ChordStringDeviation& deviation : chord.string_deviations)
    {
        if (!voicing_strings.contains(deviation.string_number) ||
            !deviating_strings.insert(deviation.string_number).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "chord string deviation must name a distinct string of the template voicing",
            }};
        }

        if (deviation.end.has_value())
        {
            if (const auto end_error =
                    validateEventEnd(*deviation.end, start_position, terminal_position, tempo_map);
                !end_error.has_value())
            {
                return std::unexpected{std::move(end_error.error())};
            }
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Validates a whole arrangement chart: the tuning, the chord templates, and every event (including
// cross-event duplicate-onset detection). This is the single semantic gate run on both read and save,
// so the writer never emits a chart its own reader would reject.
[[nodiscard]] std::expected<void, SongPackageError> validateArrangementEvents(
    const std::vector<ChartEvent>& events, const std::vector<ChordTemplate>& chord_templates,
    const Tuning& tuning, const TempoMap& tempo_map)
{
    if (tuning.open_strings.empty())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "arrangement tuning must list at least one open string",
        }};
    }

    for (const std::string& open_string : tuning.open_strings)
    {
        const auto open_midi = parsePitchToMidi(open_string);
        if (!open_midi.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement tuning has an invalid note name: " + open_string,
            }};
        }

        if (open_string != pitchLabel(*open_midi))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "arrangement tuning note name must use canonical spelling: " +
                    pitchLabel(*open_midi),
            }};
        }
    }

    std::unordered_map<std::string, const ChordTemplate*> templates_by_id;
    for (const ChordTemplate& chord_template : chord_templates)
    {
        if (const auto template_error =
                validateChordTemplate(chord_template, tuning.open_strings.size());
            !template_error.has_value())
        {
            return std::unexpected{std::move(template_error.error())};
        }

        if (!templates_by_id.emplace(chord_template.id, &chord_template).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "duplicate chord template id: " + chord_template.id,
            }};
        }
    }

    std::set<StringOnsetKey> occupied_onsets;
    for (const ChartEvent& event : events)
    {
        if (const auto event_error = validateChartEvent(
                event, templates_by_id, tuning.open_strings.size(), tempo_map, occupied_onsets);
            !event_error.has_value())
        {
            return std::unexpected{std::move(event_error.error())};
        }
    }

    return std::expected<void, SongPackageError>{};
}

// Reads an event's optional sustain-end token. Absent means a non-sustained event; a present but
// malformed token is rejected at the read boundary.
[[nodiscard]] std::expected<std::optional<GridPosition>, SongPackageError> readOptionalEnd(
    const juce::var& event_json)
{
    if (isAbsentJsonValue(Json::value(event_json, "end")))
    {
        return std::optional<GridPosition>{};
    }

    const auto end_text = Json::tryReadString(event_json, "end");
    const auto end = end_text.has_value() ? parseGridPosition(*end_text) : std::nullopt;
    if (!end.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            R"(event end must be a grid-position token such as "12:3" or "12:3+1/2")",
        }};
    }

    return std::optional<GridPosition>{*end};
}

// Reads an optional techniques object. Absent means no articulations.
[[nodiscard]] std::expected<Techniques, SongPackageError> readTechniques(
    const juce::var& techniques_json)
{
    Techniques techniques;
    if (isAbsentJsonValue(techniques_json))
    {
        return techniques;
    }

    if (!techniques_json.isObject())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "event techniques must be an object",
        }};
    }

    techniques.vibrato = Json::readOptionalBool(techniques_json, "vibrato", false);
    techniques.palm_mute = Json::readOptionalBool(techniques_json, "palmMute", false);

    if (!isAbsentJsonValue(Json::value(techniques_json, "bend")))
    {
        const auto bend_text = Json::tryReadString(techniques_json, "bend");
        const auto bend = bend_text.has_value() ? parseFractionText(*bend_text) : std::nullopt;
        if (!bend.has_value() || *bend < Fraction{} ||
            bend->denominator > g_max_fraction_denominator)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                R"(technique bend must be a non-negative fraction such as "1" or "1/2")",
            }};
        }
        techniques.bend = *bend;
    }

    return techniques;
}

// Reads the arrangement tuning: a non-empty array of open-string note names. The names are validated
// (parsed to pitches) by validateArrangementEvents, which gates both the read and save paths.
[[nodiscard]] std::expected<Tuning, SongPackageError> readTuning(
    const juce::var& arrangement_document)
{
    const juce::var& tuning_json = Json::value(arrangement_document, "tuning");
    if (!tuning_json.isArray() || tuning_json.size() == 0)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "arrangement document tuning must be a non-empty array of note names",
        }};
    }

    Tuning tuning;
    tuning.open_strings.reserve(static_cast<std::size_t>(tuning_json.size()));
    for (const juce::var& open_string_json : *tuning_json.getArray())
    {
        if (!open_string_json.isString())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tuning entries must be note-name strings",
            }};
        }
        tuning.open_strings.push_back(open_string_json.toString().toStdString());
    }

    return tuning;
}

// Reads the optional chord-template library. Charts with no chords may omit it. Structural validity
// (unique ids, voicing shape) is checked by validateArrangementEvents.
[[nodiscard]] std::expected<std::vector<ChordTemplate>, SongPackageError> readChordTemplates(
    const juce::var& arrangement_document)
{
    std::vector<ChordTemplate> chord_templates;
    const juce::var& templates_json = Json::value(arrangement_document, "chordTemplates");
    if (isAbsentJsonValue(templates_json))
    {
        return chord_templates;
    }

    if (!templates_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "chordTemplates must be an array",
        }};
    }

    chord_templates.reserve(static_cast<std::size_t>(templates_json.size()));
    for (const juce::var& template_json : *templates_json.getArray())
    {
        if (!template_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "chordTemplates entries must be objects",
            }};
        }

        const auto id = Json::tryReadString(template_json, "id");
        const auto name = Json::tryReadString(template_json, "name");
        const juce::var& voicing_json = Json::value(template_json, "voicing");
        if (!id.has_value() || id->empty() || !name.has_value() || !voicing_json.isArray())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "chord template requires a non-empty id, a name, and a voicing array",
            }};
        }

        ChordTemplate chord_template{.id = *id, .name = *name, .voicing = {}};
        for (const juce::var& voiced_json : *voicing_json.getArray())
        {
            if (!voiced_json.isObject())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chord voicing entries must be objects",
                }};
            }

            const auto string_number = Json::tryReadInt64(voiced_json, "string");
            const auto fret = Json::tryReadInt64(voiced_json, "fret");
            if (!string_number.has_value() || !fret.has_value() ||
                !fitsPositiveIntField(*string_number) || !fitsNonNegativeIntField(*fret))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chord voicing entry requires a positive string and non-negative fret",
                }};
            }

            ChordVoicingString voiced{
                .string_number = static_cast<int>(*string_number),
                .fret = static_cast<int>(*fret),
                .finger = std::nullopt,
            };
            if (!isAbsentJsonValue(Json::value(voiced_json, "finger")))
            {
                const auto finger = Json::tryReadInt64(voiced_json, "finger");
                if (!finger.has_value() || !fitsIntField(*finger))
                {
                    return std::unexpected{SongPackageError{
                        SongPackageErrorCode::InvalidArrangement,
                        "chord voicing finger must be an integer",
                    }};
                }
                voiced.finger = static_cast<int>(*finger);
            }

            chord_template.voicing.push_back(voiced);
        }

        chord_templates.push_back(std::move(chord_template));
    }

    return chord_templates;
}

// Reads the chart events. Each event is discriminated by its fields: a "chord" reference makes it a
// chord instance, an inline "string" makes it a single note; both or neither is an error. The
// display-only "note" label on single notes is rejected when stale, but string and fret remain the
// stored model fields.
[[nodiscard]] std::expected<std::vector<ChartEvent>, SongPackageError> readEvents(
    const juce::var& arrangement_document, const Tuning& tuning)
{
    const juce::var& events_json = Json::value(arrangement_document, "events");
    if (!events_json.isArray())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            "arrangement document events must be an array",
        }};
    }

    std::vector<ChartEvent> events;
    events.reserve(static_cast<std::size_t>(events_json.size()));
    for (const juce::var& event_json : *events_json.getArray())
    {
        if (!event_json.isObject())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "events entries must be objects",
            }};
        }

        const auto start_text = Json::tryReadString(event_json, "start");
        const auto start = start_text.has_value() ? parseGridPosition(*start_text) : std::nullopt;
        if (!start.has_value())
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                R"(event start must be a grid-position token such as "12:1" or "12:2+1/2")",
            }};
        }

        auto end = readOptionalEnd(event_json);
        if (!end.has_value())
        {
            return std::unexpected{std::move(end.error())};
        }

        const bool has_chord = !isAbsentJsonValue(Json::value(event_json, "chord"));
        const bool has_string = !isAbsentJsonValue(Json::value(event_json, "string"));
        if (has_chord == has_string)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "each event must be either a single note (string + fret) or a chord reference",
            }};
        }

        ChartEvent event{.start = *start, .end = *end, .content = {}};
        if (has_chord)
        {
            const auto template_id = Json::tryReadString(event_json, "chord");
            if (!template_id.has_value() || template_id->empty())
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "chord event chord must be a non-empty template id",
                }};
            }

            ChordInstance chord{.template_id = *template_id, .string_deviations = {}};
            const juce::var& deviations_json = Json::value(event_json, "strings");
            if (!isAbsentJsonValue(deviations_json))
            {
                if (!deviations_json.isArray())
                {
                    return std::unexpected{SongPackageError{
                        SongPackageErrorCode::InvalidArrangement,
                        "chord event strings must be an array of per-string deviations",
                    }};
                }

                for (const juce::var& deviation_json : *deviations_json.getArray())
                {
                    if (!deviation_json.isObject())
                    {
                        return std::unexpected{SongPackageError{
                            SongPackageErrorCode::InvalidArrangement,
                            "chord string deviation must be an object",
                        }};
                    }

                    const auto string_number = Json::tryReadInt64(deviation_json, "string");
                    if (!string_number.has_value() || !fitsPositiveIntField(*string_number))
                    {
                        return std::unexpected{SongPackageError{
                            SongPackageErrorCode::InvalidArrangement,
                            "chord string deviation string must be a positive integer",
                        }};
                    }

                    auto deviation_end = readOptionalEnd(deviation_json);
                    if (!deviation_end.has_value())
                    {
                        return std::unexpected{std::move(deviation_end.error())};
                    }

                    auto deviation_techniques =
                        readTechniques(Json::value(deviation_json, "techniques"));
                    if (!deviation_techniques.has_value())
                    {
                        return std::unexpected{std::move(deviation_techniques.error())};
                    }

                    chord.string_deviations.push_back(
                        ChordStringDeviation{
                            .string_number = static_cast<int>(*string_number),
                            .end = *deviation_end,
                            .techniques = *deviation_techniques,
                        });
                }
            }

            event.content = std::move(chord);
        }
        else
        {
            const auto string_number = Json::tryReadInt64(event_json, "string");
            const auto fret = Json::tryReadInt64(event_json, "fret");
            if (!string_number.has_value() || !fret.has_value() ||
                !fitsPositiveIntField(*string_number) || !fitsNonNegativeIntField(*fret))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "single-note event requires a positive string and a non-negative fret",
                }};
            }

            const juce::var& note_json = Json::value(event_json, "note");
            if (!isAbsentJsonValue(note_json))
            {
                const auto note_text = Json::tryReadString(event_json, "note");
                const std::string expected_note =
                    noteLabel(tuning, static_cast<int>(*string_number), static_cast<int>(*fret));
                if (!note_text.has_value() ||
                    (!expected_note.empty() && *note_text != expected_note))
                {
                    return std::unexpected{SongPackageError{
                        SongPackageErrorCode::InvalidArrangement,
                        "single-note event note label does not match its string, fret, and tuning",
                    }};
                }
            }

            auto techniques = readTechniques(Json::value(event_json, "techniques"));
            if (!techniques.has_value())
            {
                return std::unexpected{std::move(techniques.error())};
            }

            event.content = SingleNote{
                .string_number = static_cast<int>(*string_number),
                .fret = static_cast<int>(*fret),
                .techniques = *techniques,
            };
        }

        events.push_back(std::move(event));
    }

    return events;
}

// Chart data read from one arrangement document before it is stored on an Arrangement.
struct ArrangementChart
{
    Tuning tuning;
    std::vector<ChordTemplate> chord_templates;
    std::vector<ChartEvent> events;
};

// Opens, parses, and validates an arrangement document file, returning its tuning, chord templates,
// and events. Untrusted chart data enters the model here, so validateArrangementEvents runs at this
// boundary; the write path runs the same gate so it never emits a document this reader would reject.
[[nodiscard]] std::expected<ArrangementChart, SongPackageError> readArrangementDocumentFile(
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

    auto tuning = readTuning(arrangement_document);
    if (!tuning.has_value())
    {
        return std::unexpected{std::move(tuning.error())};
    }

    auto chord_templates = readChordTemplates(arrangement_document);
    if (!chord_templates.has_value())
    {
        return std::unexpected{std::move(chord_templates.error())};
    }

    auto events = readEvents(arrangement_document, *tuning);
    if (!events.has_value())
    {
        return std::unexpected{std::move(events.error())};
    }

    if (const auto validation =
            validateArrangementEvents(*events, *chord_templates, *tuning, tempo_map);
        !validation.has_value())
    {
        return std::unexpected{std::move(validation.error())};
    }

    return ArrangementChart{
        .tuning = std::move(*tuning),
        .chord_templates = std::move(*chord_templates),
        .events = std::move(*events),
    };
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

        auto chart = readArrangementDocumentFile(*resolved_arrangement_document, tempo_map);
        if (!chart.has_value())
        {
            return std::unexpected{std::move(chart.error())};
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
                .tuning = std::move(chart->tuning),
                .chord_templates = std::move(chart->chord_templates),
                .events = std::move(chart->events),
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

// Renders a grid position as its token: "<measure>:<beat>" or "<measure>:<beat>+<fraction>".
[[nodiscard]] std::string gridPositionToken(const GridPosition& position)
{
    std::string token = std::to_string(position.measure) + ":" + std::to_string(position.beat);
    if (position.offset != Fraction{})
    {
        token += "+" + fractionToText(position.offset);
    }

    return token;
}

// Renders a techniques set as a compact object, or empty text when no technique is active.
[[nodiscard]] std::string formatTechniques(const Techniques& techniques)
{
    if (!techniques.any())
    {
        return {};
    }

    std::string fields;
    const auto append = [&fields](std::string field) {
        fields += fields.empty() ? "" : ", ";
        fields += std::move(field);
    };

    if (techniques.vibrato)
    {
        append(R"("vibrato": true)");
    }
    if (techniques.palm_mute)
    {
        append(R"("palmMute": true)");
    }
    if (techniques.bend.has_value())
    {
        append(R"("bend": )" + quotedFractionText(*techniques.bend));
    }

    return "{ " + fields + " }";
}

// Renders one voicing string of a chord template as a compact object.
[[nodiscard]] std::string formatVoicingString(const ChordVoicingString& voiced)
{
    std::string text = R"({ "string": )" + std::to_string(voiced.string_number) + R"(, "fret": )" +
                       std::to_string(voiced.fret);
    if (voiced.finger.has_value())
    {
        text += R"(, "finger": )" + std::to_string(*voiced.finger);
    }
    text += " }";

    return text;
}

// Maps in-memory chord template ids to writer-generated ids.
struct GeneratedChordIds
{
    std::vector<std::string> template_ids;
    std::unordered_map<std::string, std::string> event_ids_by_source_id;
};

// Builds normalized chord template ids for a whole arrangement document write without mutating the
// in-memory arrangement. Validation has already ensured event references resolve.
[[nodiscard]] GeneratedChordIds generateChordIds(const Arrangement& arrangement)
{
    std::unordered_map<std::string, std::size_t> template_indices_by_id;
    template_indices_by_id.reserve(arrangement.chord_templates.size());
    for (std::size_t index = 0; index < arrangement.chord_templates.size(); ++index)
    {
        template_indices_by_id.emplace(arrangement.chord_templates[index].id, index);
    }

    std::vector<std::size_t> ordered_indices;
    std::vector<char> included(arrangement.chord_templates.size(), false);
    const auto include_template = [&](std::size_t index) {
        if (!included[index])
        {
            included[index] = true;
            ordered_indices.push_back(index);
        }
    };

    for (const ChartEvent& event : arrangement.events)
    {
        if (const auto* chord = std::get_if<ChordInstance>(&event.content))
        {
            if (const auto found = template_indices_by_id.find(chord->template_id);
                found != template_indices_by_id.end())
            {
                include_template(found->second);
            }
        }
    }

    for (std::size_t index = 0; index < arrangement.chord_templates.size(); ++index)
    {
        include_template(index);
    }

    std::unordered_map<std::string, int> counts_by_name;
    for (const ChordTemplate& chord_template : arrangement.chord_templates)
    {
        ++counts_by_name[chord_template.name];
    }

    std::unordered_map<std::string, int> ordinals_by_name;
    std::set<std::string> used_generated_ids;
    GeneratedChordIds generated{
        .template_ids = std::vector<std::string>(arrangement.chord_templates.size()),
        .event_ids_by_source_id = {},
    };
    generated.event_ids_by_source_id.reserve(arrangement.chord_templates.size());

    for (const std::size_t index : ordered_indices)
    {
        const ChordTemplate& chord_template = arrangement.chord_templates[index];
        const int ordinal = ++ordinals_by_name[chord_template.name];
        std::string generated_id = chord_template.name;
        if (counts_by_name[chord_template.name] > 1)
        {
            generated_id += "-" + std::to_string(ordinal);
        }

        const std::string base_id = generated_id;
        int collision_ordinal = 1;
        while (!used_generated_ids.insert(generated_id).second)
        {
            generated_id = base_id + "-" + std::to_string(++collision_ordinal);
        }

        generated.template_ids[index] = generated_id;
        generated.event_ids_by_source_id.emplace(chord_template.id, std::move(generated_id));
    }

    return generated;
}

// Renders one chord template as a single object line with its writer-generated id and voicing
// inline.
[[nodiscard]] std::string formatChordTemplate(
    const ChordTemplate& chord_template, const std::string& generated_id)
{
    std::string line = R"({ "id": )" + jsonString(generated_id) + R"(, "name": )" +
                       jsonString(chord_template.name) + R"(, "voicing": [)";
    for (std::size_t index = 0; index < chord_template.voicing.size(); ++index)
    {
        line += index == 0 ? " " : ", ";
        line += formatVoicingString(chord_template.voicing[index]);
    }
    line += " ] }";

    return line;
}

// Renders the tuning as a single-line JSON array of open-string note names.
[[nodiscard]] std::string formatTuning(const Tuning& tuning)
{
    std::string line = "[";
    for (std::size_t index = 0; index < tuning.open_strings.size(); ++index)
    {
        line += index == 0 ? " " : ", ";
        line += jsonString(tuning.open_strings[index]);
    }
    line += " ]";

    return line;
}

// Renders one chord string deviation as a compact object.
[[nodiscard]] std::string formatStringDeviation(const ChordStringDeviation& deviation)
{
    std::string text = R"({ "string": )" + std::to_string(deviation.string_number);
    if (deviation.end.has_value())
    {
        text += R"(, "end": ")" + gridPositionToken(*deviation.end) + "\"";
    }
    if (const std::string techniques = formatTechniques(deviation.techniques); !techniques.empty())
    {
        text += R"(, "techniques": )" + techniques;
    }
    text += " }";

    return text;
}

// Renders one chart event as a single compact JSON object line. Single notes carry a writer-derived
// pitch label; chords reference a template id and list only their deviating strings.
[[nodiscard]] std::string formatEvent(
    const ChartEvent& event, const Tuning& tuning,
    const std::unordered_map<std::string, std::string>& generated_chord_ids)
{
    std::string line = R"({ "start": ")" + gridPositionToken(event.start) + "\"";
    if (event.end.has_value())
    {
        line += R"(, "end": ")" + gridPositionToken(*event.end) + "\"";
    }

    if (const auto* single_note = std::get_if<SingleNote>(&event.content))
    {
        line += R"(, "string": )" + std::to_string(single_note->string_number);
        line += R"(, "fret": )" + std::to_string(single_note->fret);
        if (const std::string label =
                noteLabel(tuning, single_note->string_number, single_note->fret);
            !label.empty())
        {
            line += R"(, "note": )" + jsonString(label);
        }
        if (const std::string techniques = formatTechniques(single_note->techniques);
            !techniques.empty())
        {
            line += R"(, "techniques": )" + techniques;
        }
    }
    else
    {
        const ChordInstance& chord = std::get<ChordInstance>(event.content);
        const auto generated_id = generated_chord_ids.find(chord.template_id);
        line += R"(, "chord": )" + jsonString(
                                       generated_id == generated_chord_ids.end()
                                           ? chord.template_id
                                           : generated_id->second);
        if (!chord.string_deviations.empty())
        {
            line += R"(, "strings": [)";
            for (std::size_t index = 0; index < chord.string_deviations.size(); ++index)
            {
                line += index == 0 ? " " : ", ";
                line += formatStringDeviation(chord.string_deviations[index]);
            }
            line += " ]";
        }
    }

    line += " }";

    return line;
}

// Builds the JSON text of an arrangement document: the format version, the tuning, the chord
// template library, and the one-line-per-event chart.
[[nodiscard]] std::string arrangementDocumentContents(const Arrangement& arrangement)
{
    const GeneratedChordIds generated_chord_ids = generateChordIds(arrangement);

    std::string contents = "{\n  \"formatVersion\": 1,\n";
    contents += "  \"tuning\": " + formatTuning(arrangement.tuning) + ",\n";

    contents += "  \"chordTemplates\": [";
    for (std::size_t index = 0; index < arrangement.chord_templates.size(); ++index)
    {
        contents += index == 0 ? "\n    " : ",\n    ";
        contents += formatChordTemplate(
            arrangement.chord_templates[index], generated_chord_ids.template_ids[index]);
    }
    contents += arrangement.chord_templates.empty() ? "],\n" : "\n  ],\n";

    contents += "  \"events\": [";
    for (std::size_t index = 0; index < arrangement.events.size(); ++index)
    {
        contents += index == 0 ? "\n    " : ",\n    ";
        contents += formatEvent(
            arrangement.events[index],
            arrangement.tuning,
            generated_chord_ids.event_ids_by_source_id);
    }
    contents += arrangement.events.empty() ? "]\n}\n" : "\n  ]\n}\n";

    return contents;
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
    return R"({ "position": ")" +
           gridPositionToken(GridPosition{.measure = anchor.measure, .beat = anchor.beat}) +
           R"(", "seconds": )" + formatTimingValue(anchor.seconds) + " }";
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

    std::ofstream arrangement_document{arrangement_path, std::ios::binary};
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
        if (const auto chart_error = validateArrangementEvents(
                arrangement.events,
                arrangement.chord_templates,
                arrangement.tuning,
                song.tempo_map);
            !chart_error.has_value())
        {
            return std::unexpected{chart_error.error()};
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
