#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

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

// The measure-anchored note-value lattice at one measure: line 0 is the downbeat, line k sits k
// steps after it, and the count restarts at the next downbeat, which is a line of its own even when
// the measure length is not a multiple of the step. Both lattice queries — the nearest line and the
// adjacent line — read their lines from here, so which lines EXIST is stated once.
struct MeasureLattice
{
    // One grid step in beats: the note value (a fraction of a whole note) scaled by how many beats
    // one whole note spans in this measure, the signature denominator.
    Fraction step;

    // The measure's length in beats, which is where the next downbeat sits.
    Fraction length;

    // Index of the last line at or before a beat count from the downbeat.
    [[nodiscard]] std::int64_t lineIndexAtOrBefore(Fraction beats_from_downbeat) const
    {
        return floorDivide(
            static_cast<std::int64_t>(beats_from_downbeat.numerator) * step.denominator,
            static_cast<std::int64_t>(beats_from_downbeat.denominator) * step.numerator);
    }

    // Beats from the downbeat to the line at an index. Unbounded on purpose: an index past the
    // measure's last line names a multiple at or beyond the length, and linePosition below is what
    // turns that into the next downbeat.
    [[nodiscard]] Fraction lineAt(std::int64_t index) const
    {
        return makeFraction(index * step.numerator, static_cast<std::int64_t>(step.denominator));
    }

    // Index of the last line strictly inside the measure: the one the next downbeat follows.
    [[nodiscard]] std::int64_t lastLineIndex() const
    {
        const std::int64_t at_or_before = lineIndexAtOrBefore(length);
        return lineAt(at_or_before) == length ? at_or_before - 1 : at_or_before;
    }
};

// The lattice at a measure, or nothing when there is none to address — a non-positive note value
// or a degenerate signature — in which case every lattice query returns its input unchanged.
[[nodiscard]] std::optional<MeasureLattice> measureLatticeAt(
    const TempoMap& tempo_map, int measure, Fraction note_value)
{
    if (note_value <= Fraction{})
    {
        return std::nullopt;
    }
    const TimeSignatureChange signature = tempo_map.timeSignatureAt(measure);
    if (signature.denominator <= 0 || signature.numerator <= 0)
    {
        return std::nullopt;
    }
    return MeasureLattice{
        .step = Fraction{note_value.numerator * signature.denominator, note_value.denominator},
        .length = Fraction{signature.numerator},
    };
}

// Beats from a position's downbeat to the position itself.
[[nodiscard]] Fraction beatsFromDownbeat(GridPosition position)
{
    return Fraction{position.beat - 1} + position.offset;
}

// The grid position of a measure's line at an index, or the next measure's downbeat when the index
// reaches past the measure's last line — the restart that keeps every downbeat on the lattice.
[[nodiscard]] GridPosition linePosition(MeasureLattice lattice, int measure, std::int64_t index)
{
    const Fraction line = lattice.lineAt(index);
    if (line >= lattice.length)
    {
        return GridPosition{.measure = measure + 1, .beat = 1, .offset = {}};
    }
    return positionInMeasure(measure, line);
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

GridPosition marginBefore(const TempoMap& tempo_map, const GridPosition position)
{
    const TimeSignatureChange signature = tempo_map.timeSignatureAt(position.measure);
    const Fraction margin = minimumSustainDistanceBeats(signature.denominator);
    return advanceGridPosition(
        tempo_map, position, Fraction{-margin.numerator, margin.denominator});
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

// The gap and the ring both live on the predecessor's beat frame, so the whole test is one
// exact-rational comparison.
bool predecessorHoldReaches(
    const GridPosition& predecessor, const Fraction sustain, const GridPosition& onset,
    const TempoMap& tempo_map)
{
    return sustain >= beatDistance(tempo_map, predecessor, onset);
}

// Mirrors the editor timeline grid's semantics exactly (tempo_grid_geometry.h): measure-anchored
// note-value steps, downbeats always lines, ties to the earlier line, exact rational results.
GridPosition snapGridPosition(const TempoMap& tempo_map, GridPosition position, Fraction note_value)
{
    const std::optional<MeasureLattice> lattice =
        measureLatticeAt(tempo_map, position.measure, note_value);
    if (!lattice.has_value())
    {
        return position;
    }

    // The lines on either side of the position. The count restarts at every downbeat, so the line
    // above never passes the next measure's downbeat — which is itself always a line, even when the
    // measure length is not a multiple of the step.
    const Fraction beats = beatsFromDownbeat(position);
    const std::int64_t index = lattice->lineIndexAtOrBefore(beats);
    const Fraction lower_line = lattice->lineAt(index);
    const Fraction upper_line = std::min(lattice->lineAt(index + 1), lattice->length);

    // Ties resolve to the earlier line, matching nearestTempoGridTime's stable-click rule.
    const bool upper_nearer = upper_line - beats < beats - lower_line;
    return linePosition(*lattice, position.measure, upper_nearer ? index + 1 : index);
}

// The neighbouring line is read straight off the lattice, never found by stepping and re-snapping:
// a re-snap picks the NEAREST line to wherever the step landed, which is the wrong line whenever a
// measure's last line sits exactly half a step short of the next downbeat (a two-beat step in 7/8
// leaves beat 7 one beat before the downbeat; stepping back two from the downbeat lands one beat
// past beat 5 and one short of beat 7, and the tie-to-earlier rule then skips beat 7 entirely). The
// walk is an involution on the lattice because each direction names the adjacent index outright.
GridPosition adjacentGridPosition(
    const TempoMap& tempo_map, GridPosition position, Fraction note_value, bool later)
{
    const std::optional<MeasureLattice> lattice =
        measureLatticeAt(tempo_map, position.measure, note_value);
    if (!lattice.has_value())
    {
        return position;
    }

    const Fraction beats = beatsFromDownbeat(position);
    const std::int64_t index = lattice->lineIndexAtOrBefore(beats);
    if (later)
    {
        // The line after the last one at or before the position is the first strictly beyond it,
        // whether the position sits on a line or between two.
        return linePosition(*lattice, position.measure, index + 1);
    }

    // Earlier: the last line at or before the position — unless the position sits ON it, where the
    // line before that is wanted. Below the downbeat the walk leaves this measure.
    const std::int64_t earlier = lattice->lineAt(index) < beats ? index : index - 1;
    if (earlier >= 0)
    {
        return positionInMeasure(position.measure, lattice->lineAt(earlier));
    }
    // From a downbeat the earlier line is the previous measure's last, on THAT measure's lattice:
    // its meter can differ, so its step and length are read afresh. The grid origin has no earlier
    // line at all, and collapsing onto the input is the documented refusal.
    if (position.measure <= 1)
    {
        return position;
    }
    const std::optional<MeasureLattice> previous =
        measureLatticeAt(tempo_map, position.measure - 1, note_value);
    if (!previous.has_value())
    {
        return position;
    }
    return positionInMeasure(position.measure - 1, previous->lineAt(previous->lastLineIndex()));
}

} // namespace rock_hero::common::core
