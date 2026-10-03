#include "timeline/tempo_map_rules.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

constexpr double g_timing_epsilon = 1.0e-9;

[[nodiscard]] bool isValidTimingValue(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

// Reports whether a seconds value is already on the millisecond grid.
[[nodiscard]] bool isOnSecondsGrid(double value) noexcept
{
    return std::isfinite(value) &&
           std::abs(value - nearestAnchorSeconds(value)) <= g_timing_epsilon;
}

// A meter's beat is a power-of-two note value, as notation writes meters, and one that is a whole
// number of ticks: a 512th-note beat would leave every position in its measure, downbeats
// included, between two ticks, which the chart rules refuse.
[[nodiscard]] bool isRepresentableDenominator(int denominator) noexcept
{
    return denominator > 0 && (denominator & (denominator - 1)) == 0 &&
           g_tick_quantum_denominator % denominator == 0;
}

[[nodiscard]] std::unexpected<TempoMapError> refuse(TempoMapErrorCode code, std::string message)
{
    return std::unexpected{TempoMapError{.code = code, .message = std::move(message)}};
}

} // namespace

double nearestAnchorSeconds(double seconds) noexcept
{
    const auto units = static_cast<double>(g_anchor_seconds_grid_units);
    return std::round(seconds * units) / units;
}

std::expected<void, TempoMapError> validateTempoMapRules(const TempoMap& tempo_map)
{
    const std::vector<TimeSignatureChange>& time_signatures = tempo_map.timeSignatures();
    if (time_signatures.empty())
    {
        return refuse(
            TempoMapErrorCode::MissingTimeSignature,
            "time signatures must contain at least one entry");
    }

    if (time_signatures.front().measure != 1)
    {
        return refuse(
            TempoMapErrorCode::FirstTimeSignatureLate, "time signatures must start at measure 1");
    }

    int previous_measure = 0;
    for (const TimeSignatureChange& signature : time_signatures)
    {
        if (signature.measure <= previous_measure || signature.numerator <= 0 ||
            !isRepresentableDenominator(signature.denominator))
        {
            return refuse(
                TempoMapErrorCode::InvalidTimeSignature,
                "time signatures must be strictly ordered meters with a power-of-two denominator "
                "of at most 256");
        }
        previous_measure = signature.measure;
    }

    const std::vector<BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return refuse(
            TempoMapErrorCode::MissingAnchors, "anchors must contain start and terminal anchors");
    }

    if (anchors.front().measure != 1 || anchors.front().beat != 1)
    {
        return refuse(TempoMapErrorCode::FirstAnchorLate, "anchors must start at measure 1 beat 1");
    }

    std::int64_t previous_index = -1;
    double previous_seconds = -1.0;
    for (const BeatAnchor& anchor : anchors)
    {
        if (anchor.measure <= 0 || anchor.beat <= 0 || !isValidTimingValue(anchor.seconds))
        {
            return refuse(
                TempoMapErrorCode::InvalidAnchor,
                "anchors must use positive addresses and finite non-negative seconds");
        }

        if (!isOnSecondsGrid(anchor.seconds))
        {
            return refuse(
                TempoMapErrorCode::AnchorOffSecondsGrid,
                "anchor seconds must use three-decimal storage precision");
        }

        if (anchor.beat > tempo_map.beatsPerMeasureAt(anchor.measure))
        {
            return refuse(
                TempoMapErrorCode::AnchorBeatOutsideMeasure,
                "anchor beat exceeds the active measure length");
        }

        const std::int64_t anchor_index = tempo_map.globalBeatIndex(anchor.measure, anchor.beat);
        if (anchor_index <= previous_index || anchor.seconds <= previous_seconds)
        {
            return refuse(
                TempoMapErrorCode::UnorderedAnchors,
                "anchors must be strictly ordered by beat and seconds");
        }

        previous_index = anchor_index;
        previous_seconds = anchor.seconds;
    }

    if (anchors.back().beat != 1)
    {
        return refuse(
            TempoMapErrorCode::TerminalOffDownbeat, "the terminal anchor must be on a downbeat");
    }

    return {};
}

} // namespace rock_hero::common::core
