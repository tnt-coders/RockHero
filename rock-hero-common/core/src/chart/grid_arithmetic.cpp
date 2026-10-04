#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Floor division for a signed numerator over a positive denominator: C++ integer division
// truncates toward zero, which would round a negative quotient the wrong way when finding the
// lattice line at or before a count.
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
//
// EVERY LINE IS A TICK: line k is the exact k steps ROUNDED onto the tick lattice (nearestTick), so
// a grid whose step no tick divides — a septuplet's — still only ever names positions a chart may
// store, and names exactly the tick an imported septuplet note rounded onto. A step is at least one
// tick for every note value the grid admits, and rounding moves a line less than half a tick while
// adding a whole tick to its input adds one to its output, so rounded lines stay strictly
// ascending and never collapse into one another.
struct MeasureLattice
{
    // One grid step in beats: the note value (a fraction of a whole note) scaled by how many beats
    // one whole note spans in this measure, the signature denominator.
    Fraction step;

    // The measure's length in beats, which is where the next downbeat sits.
    Fraction length;

    // Ticks in one beat at this measure's meter.
    std::int64_t ticks_per_beat;

    // Index of the last line at or before a beat count from the downbeat. The exact quotient names
    // the last EXACT line at or before it; rounding can carry that line past the count, or carry
    // the next one back onto it, by less than half a tick, which a step of at least a tick bounds
    // to a single index either way.
    [[nodiscard]] std::int64_t lineIndexAtOrBefore(Fraction beats_from_downbeat) const
    {
        std::int64_t index = floorDivide(
            static_cast<std::int64_t>(beats_from_downbeat.numerator) * step.denominator,
            static_cast<std::int64_t>(beats_from_downbeat.denominator) * step.numerator);
        if (beats_from_downbeat < lineAt(index))
        {
            --index;
        }
        else if (!(beats_from_downbeat < lineAt(index + 1)))
        {
            ++index;
        }
        return index;
    }

    // Beats from the downbeat to the line at an index. Unbounded on purpose: an index past the
    // measure's last line names a multiple at or beyond the length, and linePosition below is what
    // turns that into the next downbeat.
    [[nodiscard]] Fraction lineAt(std::int64_t index) const
    {
        const std::int64_t tick =
            nearestTick(index * step.numerator * ticks_per_beat, step.denominator);
        return makeFraction(tick, ticks_per_beat);
    }

    // Index of the last line strictly inside the measure: the one the next downbeat follows.
    [[nodiscard]] std::int64_t lastLineIndex() const
    {
        const std::int64_t at_or_before = lineIndexAtOrBefore(length);
        return lineAt(at_or_before) == length ? at_or_before - 1 : at_or_before;
    }
};

// The lattice at a measure, or nothing when there is none to address — a note value under a tick,
// whose rounded lines would collapse onto one another, or a degenerate signature — in which case
// every lattice query returns its input unchanged. Guarded here, once, for the two queries.
[[nodiscard]] std::optional<MeasureLattice> measureLatticeAt(
    const TempoMap& tempo_map, int measure, Fraction note_value)
{
    if (note_value < g_tick_quantum_note_value)
    {
        return std::nullopt;
    }
    const TimeSignature signature = tempo_map.timeSignatureAt(measure).meter;
    if (signature.denominator <= 0 || signature.numerator <= 0 ||
        g_tick_quantum_denominator % signature.denominator != 0)
    {
        return std::nullopt;
    }
    return MeasureLattice{
        .step = Fraction{note_value.numerator * signature.denominator, note_value.denominator},
        .length = Fraction{signature.numerator},
        .ticks_per_beat = g_tick_quantum_denominator / signature.denominator,
    };
}

// Beats from a position's downbeat to the position itself.
[[nodiscard]] Fraction beatsFromDownbeat(GridPosition position)
{
    return Fraction{position.beat - 1} + position.offset;
}

// How far past a tick line a count may sit and still be taken as ON it. The seconds round trip
// behind tickPlaceReached is exact to a part in about 1e15, so a stretch that lands squarely on a
// tick can come back a hair off it; rounding that raw would give a whole tick away. The window is
// six orders of magnitude above the round trip's own error and six below one tick, so it can only
// ever recover a line the arithmetic meant to hit.
constexpr double g_tick_round_tolerance_ticks = 1.0e-6;

// The grid position of a measure's line at an index, or the next measure's downbeat when the index
// reaches past the measure's last line — the restart that keeps every downbeat on the lattice.
[[nodiscard]] GridPosition linePosition(MeasureLattice lattice, int measure, std::int64_t index)
{
    const Fraction line = lattice.lineAt(index);
    if (line >= lattice.length)
    {
        return measureDownbeat(measure + 1);
    }
    return positionInMeasure(measure, line);
}

} // namespace

// The stretch leaves the whole-note axis for the map's time axis and comes back, so every tempo
// anchor between the two instants is part of the round trip. The min and max are the map's own
// clamping outside the authored anchors showing through — a place past the terminal resolves to a
// time the inverse cannot carry any further, and one at the first anchor to a time it cannot carry
// any earlier — and they keep the postconditions every reader relies on: back never later than
// `from` nor before the origin, forward at least one tick on.
Fraction tickPlaceReached(const TempoMap& tempo_map, const Fraction from, const double seconds)
{
    assert(isOnTickLattice(from) && "a stretch is measured from a lattice place");
    const std::int64_t from_tick =
        static_cast<std::int64_t>(from.numerator) * (g_tick_quantum_denominator / from.denominator);
    const double reached_ticks =
        tempo_map.wholeNotePositionAtSeconds(tempo_map.secondsAtWholeNote(from) + seconds) *
        static_cast<double>(g_tick_quantum_denominator);
    const std::int64_t tick =
        seconds > 0.0 ? std::max(
                            static_cast<std::int64_t>(
                                std::ceil(reached_ticks - g_tick_round_tolerance_ticks)),
                            from_tick + 1)
                      : std::clamp(
                            static_cast<std::int64_t>(
                                std::floor(reached_ticks + g_tick_round_tolerance_ticks)),
                            std::int64_t{0},
                            from_tick);
    return Fraction{static_cast<int>(tick), g_tick_quantum_denominator};
}

// The margin's start measured back to the onset: one authority for where it begins, and no second
// answer for how long it is.
Fraction minimumSustainDistance(const TempoMap& tempo_map, const Fraction onset)
{
    return onset - tickPlaceReached(tempo_map, onset, -g_minimum_sustain_distance_seconds);
}

Fraction wholeNoteDistance(
    const TempoMap& tempo_map, const GridPosition from, const GridPosition to)
{
    return tempo_map.wholeNotePositionAt(to) - tempo_map.wholeNotePositionAt(from);
}

// The whole-note axis is the tempo map's own; this only addresses its answer back onto the grid.
GridPosition advanceGridPositionByWholeNotes(
    const TempoMap& tempo_map, const GridPosition position, const Fraction whole_notes)
{
    return tempo_map.gridPositionAtWholeNote(tempo_map.wholeNotePositionAt(position) + whole_notes);
}

std::vector<Fraction> chartNotePlaces(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    std::vector<Fraction> places;
    places.reserve(notes.size());
    for (const ChartNote& note : notes)
    {
        places.push_back(tempo_map.wholeNotePositionAt(note.position));
    }
    return places;
}

// Mirrors the editor timeline grid's semantics exactly (tempo_grid_geometry.h): measure-anchored
// note-value steps, downbeats always lines, ties to the earlier line, every line a tick.
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
