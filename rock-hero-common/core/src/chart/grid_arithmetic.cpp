#include <cstdint>
#include <numeric>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Floor division for a signed numerator over a positive denominator: C++ integer division
// truncates toward zero, which would round negative totals the wrong way when splitting a beat
// delta into whole beats plus a non-negative sub-beat remainder.
[[nodiscard]] std::int64_t floorDivide(std::int64_t numerator, std::int64_t denominator)
{
    const std::int64_t quotient = numerator / denominator;
    return (numerator % denominator != 0 && (numerator < 0)) ? quotient - 1 : quotient;
}

// Reduces an int64 rational back into a Fraction through the normalizing constructor. Products
// stay well inside int64 for the bounded terms the chart grammar produces; the narrowing back to
// int mirrors Fraction's own headless-value stance.
[[nodiscard]] Fraction makeFraction(std::int64_t numerator, std::int64_t denominator)
{
    const std::int64_t divisor = std::gcd(numerator, denominator);
    if (divisor == 0)
    {
        return Fraction{};
    }
    return Fraction{static_cast<int>(numerator / divisor), static_cast<int>(denominator / divisor)};
}

// Converts a non-negative in-measure beat quantity (beats from the downbeat) into the one-based
// beat plus sub-beat offset a GridPosition stores.
[[nodiscard]] GridPosition positionInMeasure(int measure, Fraction beats_from_downbeat)
{
    const std::int64_t whole =
        floorDivide(beats_from_downbeat.numerator, beats_from_downbeat.denominator);
    const Fraction offset = beats_from_downbeat - Fraction{static_cast<int>(whole)};
    return GridPosition{
        .measure = measure,
        .beat = 1 + static_cast<int>(whole),
        .offset = offset,
    };
}

} // namespace

// Splits the delta into whole beats (carried on the tempo map's global beat axis, which already
// encodes every signature change) and a non-negative sub-beat remainder that becomes the offset.
GridPosition advanceGridPosition(const TempoMap& tempo_map, GridPosition position, Fraction beats)
{
    const Fraction total = position.offset + beats;
    const std::int64_t whole_beats = floorDivide(total.numerator, total.denominator);
    const Fraction offset = total - Fraction{static_cast<int>(whole_beats)};

    const std::int64_t beat_index =
        tempo_map.globalBeatIndex(position.measure, position.beat) + whole_beats;
    if (beat_index < 0)
    {
        // The grid has no positions before measure 1 beat 1; the clamp swallows the fractional
        // remainder too — the origin is the earliest representable position.
        return GridPosition{};
    }

    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(beat_index);
    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// The global beat axis makes the whole-beat part a plain index difference; song-scale indexes fit
// int comfortably, so the narrowing into Fraction's int terms is safe.
Fraction beatDistance(const TempoMap& tempo_map, GridPosition from, GridPosition to)
{
    const std::int64_t index_delta = tempo_map.globalBeatIndex(to.measure, to.beat) -
                                     tempo_map.globalBeatIndex(from.measure, from.beat);
    return Fraction{static_cast<int>(index_delta)} + (to.offset - from.offset);
}

// A zero sustain ends at the onset; everything else is beat advancement.
GridPosition sustainEndPosition(const TempoMap& tempo_map, const ChartNote& note)
{
    return advanceGridPosition(tempo_map, note.position, note.sustain);
}

// Mirrors the editor timeline grid's semantics exactly (tempo_grid_geometry.h): measure-anchored
// note-value steps, downbeats always lines, ties to the earlier line, exact rational results.
GridPosition snapGridPosition(const TempoMap& tempo_map, GridPosition position, Fraction note_value)
{
    if (note_value <= Fraction{})
    {
        return position;
    }

    // One step in beats: the note value is a fraction of a whole note, and the signature
    // denominator names how many beats one whole note spans in this measure.
    const TimeSignatureChange signature = tempo_map.timeSignatureAt(position.measure);
    if (signature.denominator <= 0 || signature.numerator <= 0)
    {
        return position;
    }
    const Fraction step{note_value.numerator * signature.denominator, note_value.denominator};

    // Beats from this measure's downbeat, and the grid-line multiples on either side.
    const Fraction beats_from_downbeat = Fraction{position.beat - 1} + position.offset;
    const std::int64_t step_count = floorDivide(
        static_cast<std::int64_t>(beats_from_downbeat.numerator) * step.denominator,
        static_cast<std::int64_t>(beats_from_downbeat.denominator) * step.numerator);
    const Fraction lower_line =
        makeFraction(step_count * step.numerator, static_cast<std::int64_t>(step.denominator));
    const Fraction upper_line = makeFraction(
        (step_count + 1) * step.numerator, static_cast<std::int64_t>(step.denominator));

    // The count restarts at every downbeat, so the line above never passes the next measure's
    // downbeat — which is itself always a line, even when the measure length is not a multiple
    // of the step.
    const Fraction measure_length{signature.numerator};
    const bool upper_is_next_downbeat = upper_line >= measure_length;
    const Fraction upper_candidate = upper_is_next_downbeat ? measure_length : upper_line;

    // Ties resolve to the earlier line, matching nearestTempoGridTime's stable-click rule.
    const Fraction distance_down = beats_from_downbeat - lower_line;
    const Fraction distance_up = upper_candidate - beats_from_downbeat;
    if (distance_up < distance_down)
    {
        return upper_is_next_downbeat
                   ? GridPosition{.measure = position.measure + 1, .beat = 1, .offset = {}}
                   : positionInMeasure(position.measure, upper_line);
    }
    return positionInMeasure(position.measure, lower_line);
}

} // namespace rock_hero::common::core
