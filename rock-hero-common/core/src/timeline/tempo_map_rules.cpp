#include "timeline/tempo_map_rules.h"

#include <expected>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

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

    if (anchors.front().position != GridPosition{.measure = 1, .beat = 1, .offset = {}})
    {
        return refuse(TempoMapErrorCode::FirstAnchorLate, "anchors must start at measure 1 beat 1");
    }

    std::optional<BeatAnchor> previous;
    for (const BeatAnchor& anchor : anchors)
    {
        if (!isValidGridPosition(anchor.position, tempo_map) || anchor.milliseconds < 0)
        {
            return refuse(
                TempoMapErrorCode::InvalidAnchor,
                "anchors must stand on a valid grid position (beat inside its measure, offset in "
                "[0, 1) on the tick lattice) at a non-negative time");
        }

        // Grid positions order lexicographically, which is the timeline order once each position
        // is valid, as the check above has just established.
        if (previous.has_value() && (anchor.position <= previous->position ||
                                     anchor.milliseconds <= previous->milliseconds))
        {
            return refuse(
                TempoMapErrorCode::UnorderedAnchors,
                "anchors must be strictly ordered by position and time");
        }

        previous = anchor;
    }

    const GridPosition& terminal = anchors.back().position;
    if (terminal.beat != 1 || terminal.offset != Fraction{})
    {
        return refuse(
            TempoMapErrorCode::TerminalOffDownbeat, "the terminal anchor must be on a downbeat");
    }

    return {};
}

} // namespace rock_hero::common::core
