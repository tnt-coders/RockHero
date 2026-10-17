#include "chart/chart_shape_holds.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <span>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The pressed stop a fretting-hand note is struck at: what a hold states on the note's string.
[[nodiscard]] std::optional<int> pressedStopStruck(const ChartNote& note)
{
    return fretHandStopAt(note, std::nullopt, Fraction{}).and_then([](const ChartStop& stop) {
        return pressedFret(stop.fret);
    });
}

// The note stating a string's stop, and the stop.
struct StopStatement
{
    std::size_t note{0};
    int fret{0};
};

// The first of `notes` that states a pressed stop.
[[nodiscard]] std::optional<StopStatement> firstStatement(
    const ChartConnections& connections, const std::span<const std::size_t> notes)
{
    for (const std::size_t index : notes)
    {
        if (const std::optional<int> fret = pressedStopStruck(connections.saved_notes[index]);
            fret.has_value())
        {
            return StopStatement{.note = index, .fret = *fret};
        }
    }
    return std::nullopt;
}

// Where a note's ring ends on the whole-note axis.
[[nodiscard]] Fraction ringEndOf(const ChartConnections& connections, const std::size_t index)
{
    return connections.places[index] + connections.saved_notes[index].sustain;
}

} // namespace

// Judges each string inside the hold, then searches past the end only where every stated stop
// still sounds there; the answer never passes the song's end, past which nothing sounds.
Fraction shapeHoldNormalEnd(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold)
{
    const Fraction end = tempo_map.wholeNotePositionAt(hold.end);
    if (std::holds_alternative<NoneGrip>(hold.grip))
    {
        return end;
    }
    const Fraction start = tempo_map.wholeNotePositionAt(hold.start);
    const auto place_of = [&connections](const std::size_t index) {
        return connections.places[index];
    };

    // The earliest instant a stated stop falls silent: at a conflict, at the end where it has
    // stopped sounding there, or past the end where it still sounds across it.
    std::optional<Fraction> first_silent;
    for (std::size_t string_index = 0; string_index < connections.string_notes.size();
         ++string_index)
    {
        const std::vector<std::size_t>& notes = connections.string_notes[string_index];
        const auto inside = std::ranges::lower_bound(notes, start, std::ranges::less{}, place_of);
        const auto past_end =
            std::ranges::lower_bound(inside, notes.end(), end, std::ranges::less{}, place_of);
        const std::optional<StopStatement> statement =
            firstStatement(connections, std::span<const std::size_t>{inside, past_end});
        if (!statement.has_value())
        {
            continue;
        }
        // The last note struck before the end, the only one on the string that can sound across it.
        const std::size_t last = *std::prev(past_end);
        // Judged up to that note's ring end where it rings past the end, so the one search finds
        // both a conflict inside the hold and where a stop still sounding there falls silent.
        const Fraction released = stringReleasesStopAt(
            connections,
            string_index,
            statement->fret,
            place_of(statement->note),
            std::max(end, ringEndOf(connections, last)));
        // Only a last note gripping the stated stop sounds it past the end: a higher fret struck
        // after it has ended its sound.
        const bool grips = pressedStopStruck(connections.saved_notes[last]) == statement->fret;
        const Fraction silent = grips ? released : std::min(released, end);
        first_silent = std::min(first_silent.value_or(silent), silent);
    }

    // Every stated stop sounds across the end, so the hold extends to where the first falls silent,
    // or sooner where a string first struck inside the extension states a stop that does. A string
    // stated inside the hold has no note there: its next onset waits for its ring to end.
    if (first_silent.has_value() && end < *first_silent)
    {
        Fraction extended =
            std::min(*first_silent, tempo_map.wholeNotePositionAt(terminalGridPosition(tempo_map)));
        for (std::size_t string_index = 0; string_index < connections.string_notes.size();
             ++string_index)
        {
            const std::vector<std::size_t>& notes = connections.string_notes[string_index];
            const auto from = std::ranges::lower_bound(notes, end, std::ranges::less{}, place_of);
            const auto until = std::ranges::lower_bound(
                from, notes.end(), extended, std::ranges::less{}, place_of);
            const std::optional<StopStatement> statement =
                firstStatement(connections, std::span<const std::size_t>{from, until});
            if (statement.has_value())
            {
                extended = std::min(
                    extended,
                    stringReleasesStopAt(
                        connections,
                        string_index,
                        statement->fret,
                        place_of(statement->note),
                        ringEndOf(connections, statement->note)));
            }
        }
        return extended;
    }
    return first_silent.value_or(end);
}

// Fits exactly where the hold is its own normal form.
bool shapeHoldFitsNotes(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold)
{
    return shapeHoldNormalEnd(connections, tempo_map, hold) ==
           tempo_map.wholeNotePositionAt(hold.end);
}

} // namespace rock_hero::common::core
