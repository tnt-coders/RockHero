#include "timeline/tempo_map_rules.h"

#include <expected>
#include <format>
#include <optional>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/time_signature_text.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A meter's beat is a power-of-two note value, as notation writes meters, and one that is a whole
// number of ticks: a 512th-note beat would leave every position in its measure, downbeats
// included, between two ticks, which the chart rules refuse. The same rule bounds where a meter may
// START (isOnMeterLattice): the measure a change cuts short is itself a meter, so the remainder it
// leaves must be a whole number of some beat a meter may state.
[[nodiscard]] bool isRepresentableDenominator(int denominator) noexcept
{
    return denominator > 0 && (denominator & (denominator - 1)) == 0 &&
           g_tick_quantum_denominator % denominator == 0;
}

[[nodiscard]] std::unexpected<TempoMapError> refuse(
    TempoMapErrorCode code, std::string message, std::vector<GridPosition> anchor_positions,
    std::vector<Fraction> meter_places)
{
    return std::unexpected{TempoMapError{
        .code = code,
        .message = std::move(message),
        .anchor_positions = std::move(anchor_positions),
        .meter_places = std::move(meter_places),
    }};
}

// "the 7/8 at 6:1": how every signature rule names the change it refuses, where the charter sees
// it.
[[nodiscard]] std::string changeText(const TempoMap& tempo_map, const TimeSignatureChange& change)
{
    return std::format(
        "the {} at {}",
        timeSignatureText(change.meter),
        formatGridPositionToken(tempo_map.gridPositionAtWholeNote(change.place)));
}

} // namespace

bool isOnMeterLattice(const Fraction place) noexcept
{
    return isRepresentableDenominator(place.denominator);
}

std::expected<void, TempoMapError> validateTempoMapRules(const TempoMap& tempo_map)
{
    const std::vector<TimeSignatureChange>& time_signatures = tempo_map.timeSignatures();
    if (time_signatures.empty())
    {
        return refuse(
            TempoMapErrorCode::MissingTimeSignature,
            "time signatures must contain at least one entry",
            {},
            {});
    }

    if (time_signatures.front().place != Fraction{})
    {
        return refuse(
            TempoMapErrorCode::FirstTimeSignatureLate,
            "time signatures must start at the song's start",
            {},
            {time_signatures.front().place});
    }

    const TimeSignatureChange* previous = nullptr;
    for (const TimeSignatureChange& signature : time_signatures)
    {
        if (signature.meter.numerator <= 0 ||
            !isRepresentableDenominator(signature.meter.denominator))
        {
            return refuse(
                TempoMapErrorCode::InvalidTimeSignature,
                changeText(tempo_map, signature) +
                    " must have a positive numerator and a power-of-two denominator of at most "
                    "256",
                {},
                {signature.place});
        }
        if (!isOnMeterLattice(signature.place))
        {
            return refuse(
                TempoMapErrorCode::InvalidTimeSignature,
                changeText(tempo_map, signature) +
                    " must start a whole number of 1/256 notes from the song's start, so the "
                    "measure it cuts short is itself a meter",
                {},
                {signature.place});
        }
        if (previous != nullptr)
        {
            if (signature.place <= previous->place)
            {
                return refuse(
                    TempoMapErrorCode::InvalidTimeSignature,
                    "time signatures must be strictly ascending: " +
                        changeText(tempo_map, signature) + " does not follow " +
                        changeText(tempo_map, *previous),
                    {},
                    {previous->place, signature.place});
            }
            // The reign fills whole measures and leaves a short bar; a reign shorter than one
            // measure has no whole measure to fill, and its chip and its short bar would stand at
            // one downbeat.
            if (signature.place - previous->place < previous->meter.measureWholeNotes())
            {
                return refuse(
                    TempoMapErrorCode::ReignTooShort,
                    changeText(tempo_map, *previous) + " governs no whole measure before " +
                        changeText(tempo_map, signature),
                    {},
                    {previous->place, signature.place});
            }
        }
        previous = &signature;
    }

    const std::vector<BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return refuse(
            TempoMapErrorCode::MissingAnchors,
            "anchors must contain start and terminal anchors",
            {},
            {});
    }

    if (anchors.front().position != measureDownbeat(1))
    {
        return refuse(
            TempoMapErrorCode::FirstAnchorLate,
            "anchors must start at measure 1 beat 1",
            {anchors.front().position},
            {});
    }

    std::optional<BeatAnchor> previous_anchor;
    for (const BeatAnchor& anchor : anchors)
    {
        if (!isValidGridPosition(anchor.position, tempo_map) || anchor.milliseconds < 0)
        {
            return refuse(
                TempoMapErrorCode::InvalidAnchor,
                "anchors must stand on a valid grid position (beat inside its measure, offset in "
                "[0, 1) on the tick lattice) at a non-negative time",
                {anchor.position},
                {});
        }

        // Grid positions order lexicographically, which is the timeline order once each position
        // is valid, as the check above has just established.
        if (previous_anchor.has_value() && (anchor.position <= previous_anchor->position ||
                                            anchor.milliseconds <= previous_anchor->milliseconds))
        {
            return refuse(
                TempoMapErrorCode::UnorderedAnchors,
                "anchors must be strictly ordered by position and time",
                {previous_anchor->position, anchor.position},
                {});
        }

        previous_anchor = anchor;
    }

    return {};
}

} // namespace rock_hero::common::core
