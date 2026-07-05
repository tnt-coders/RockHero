#include "rock_song_package_format.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/package_id.h>
#include <set>
#include <string>
#include <system_error>

namespace rock_hero::common::core
{

namespace
{

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

[[nodiscard]] bool isPowerOfTwoDenominator(int denominator) noexcept
{
    return denominator > 0 && (denominator & (denominator - 1)) == 0;
}

} // namespace

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

// Parses a grid position token: "<measure>:<beat>".
std::optional<BeatPositionToken> parseBeatPositionToken(const std::string& text)
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

    return BeatPositionToken{.measure = measure, .beat = beat};
}

std::string formatBeatPositionToken(int measure, int beat)
{
    return std::to_string(measure) + ":" + std::to_string(beat);
}

// Validates the structural tone-track rules shared by package read and write.
std::expected<void, SongPackageError> validateToneTrack(
    const ToneTrack& tone_track, const TempoMap& tempo_map)
{
    const std::int64_t terminal_beat_index = tempo_map.terminalGlobalBeatIndex();
    std::set<std::string> region_ids;
    std::int64_t previous_end_index = 0;
    bool has_previous_region = false;

    for (const ToneRegion& region : tone_track.regions)
    {
        if (!isCanonicalPackageId(region.id))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone region id must be a canonical UUIDv4: " + region.id,
            }};
        }

        if (!region_ids.insert(region.id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "duplicate tone region id: " + region.id,
            }};
        }

        for (const ToneGridPosition& endpoint : {region.start, region.end})
        {
            if (endpoint.measure < 1 || endpoint.beat < 1 ||
                endpoint.beat > tempo_map.beatsPerMeasureAt(endpoint.measure))
            {
                return std::unexpected{SongPackageError{
                    SongPackageErrorCode::InvalidArrangement,
                    "tone region endpoint is not a valid beat position: " +
                        formatBeatPositionToken(endpoint.measure, endpoint.beat),
                }};
            }
        }

        const std::int64_t start_index =
            tempo_map.globalBeatIndex(region.start.measure, region.start.beat);
        const std::int64_t end_index =
            tempo_map.globalBeatIndex(region.end.measure, region.end.beat);
        if (start_index >= end_index)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone region start must be before its end: " + region.id,
            }};
        }

        if (end_index > terminal_beat_index)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone region ends past the tempo-map terminal anchor: " + region.id,
            }};
        }

        if (has_previous_region && start_index < previous_end_index)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone regions must be sorted and must not overlap: " + region.id,
            }};
        }

        if (!isCanonicalToneDocumentRef(region.tone_document_ref))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "tone region document path must be tones/<uuid>/tone.json: " +
                    region.tone_document_ref,
            }};
        }

        previous_end_index = end_index;
        has_previous_region = true;
    }

    return std::expected<void, SongPackageError>{};
}

} // namespace rock_hero::common::core
