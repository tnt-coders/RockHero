#include "rock_song_package_format.h"

#include <cmath>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>

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

} // namespace rock_hero::common::core
