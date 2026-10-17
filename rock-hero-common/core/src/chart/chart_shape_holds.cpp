#include "chart/chart_shape_holds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <span>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The pressed stop a fretting-hand note is struck at: what a hold states on the note's string.
// Nothing for a picking-hand onset, the open string or a natural harmonic's node.
[[nodiscard]] std::optional<int> pressedStopStruck(const ChartNote& note)
{
    if (pickingHandStopsString(note.attack, note.harmonic_node))
    {
        return std::nullopt;
    }
    return pressedFret(frettingStopAt(note, note.fret).fret);
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
// still sounds there.
Fraction shapeHoldNormalEnd(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold)
{
    const Fraction end = tempo_map.wholeNotePositionAt(hold.end);
    if (std::holds_alternative<NoArpeggio>(hold.grip))
    {
        return end;
    }
    const Fraction start = tempo_map.wholeNotePositionAt(hold.start);
    const auto place_of = [&connections](const std::size_t index) {
        return connections.places[index];
    };

    std::optional<Fraction> conflict;
    // The earliest instant a stated stop sounding across the end falls silent.
    std::optional<Fraction> silent_at;
    bool stated_stop_silent = false;
    std::array<bool, static_cast<std::size_t>(g_max_chart_strings)> stated{};
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
        stated.at(string_index) = true;
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
        if (released < end)
        {
            conflict = std::min(conflict.value_or(released), released);
        }
        else if (
            end < released && pressedStopStruck(connections.saved_notes[last]) == statement->fret
        )
        {
            silent_at = std::min(silent_at.value_or(released), released);
        }
        else
        {
            stated_stop_silent = true;
        }
    }
    if (conflict.has_value())
    {
        return *conflict;
    }
    if (stated_stop_silent || !silent_at.has_value())
    {
        return end;
    }

    // Every stated stop sounds across the end, so the hold extends to where the first falls silent,
    // or sooner where a string first struck inside the extension states a stop that does.
    Fraction extended = *silent_at;
    for (std::size_t string_index = 0; string_index < connections.string_notes.size();
         ++string_index)
    {
        if (stated.at(string_index))
        {
            continue;
        }
        const std::vector<std::size_t>& notes = connections.string_notes[string_index];
        const auto from = std::ranges::lower_bound(notes, end, std::ranges::less{}, place_of);
        const auto until =
            std::ranges::lower_bound(from, notes.end(), extended, std::ranges::less{}, place_of);
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

// Valid exactly where the hold is its own normal form.
bool shapeHoldIsValid(
    const ChartConnections& connections, const TempoMap& tempo_map, const ShapeHold& hold)
{
    return shapeHoldNormalEnd(connections, tempo_map, hold) ==
           tempo_map.wholeNotePositionAt(hold.end);
}

} // namespace rock_hero::common::core
