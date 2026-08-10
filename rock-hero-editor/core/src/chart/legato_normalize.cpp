#include "chart/legato_normalize.h"

#include <array>
#include <cstddef>
#include <limits>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <vector>

namespace rock_hero::editor::core
{

void normalizeChartLegato(
    std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::ChartShape>& shapes, const common::core::TempoMap& tempo_map)
{
    // Held lengths for the hold test, span-extended where the spans imply a held shape. Computed
    // once up front: repairs only ever change an attack, never a position or a sustain, so the
    // table cannot go stale inside the walk.
    const std::vector<common::core::Fraction> effective_sustains =
        common::core::chartEffectiveSustains(notes, shapes, tempo_map);
    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached. Repairs apply before a note is recorded, so a later note
    // judges against the repaired values.
    constexpr std::size_t no_predecessor = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, static_cast<std::size_t>(common::core::g_max_chart_strings) + 1>
        last_per_string{};
    last_per_string.fill(no_predecessor);
    for (std::size_t note_index = 0; note_index < notes.size(); ++note_index)
    {
        common::core::ChartNote& note = notes[note_index];
        const bool string_in_range =
            note.string >= 1 && note.string <= common::core::g_max_chart_strings;
        const std::size_t source_index =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                            : no_predecessor;
        const common::core::ChartNote* const source =
            source_index == no_predecessor ? nullptr : &notes[source_index];
        if (note.attack == common::core::NoteAttack::Pull)
        {
            // A pull-off must release a real press above the pulled note, still held at this
            // onset, and can never sound a harmonic itself. A fret-hand harmonic predecessor
            // touches without pressing; a scrape releases from its slide-out's end and stays a
            // valid source. A predecessor the hold test disproves supports NEITHER direction —
            // hammering after a release is the left-hand tap, Ctrl+H's domain, never derived —
            // so it repairs straight to a plain pick.
            const bool releasable =
                source != nullptr && !common::core::fretHandHarmonic(*source) &&
                common::core::predecessorHoldReaches(
                    source->position, effective_sustains[source_index], note.position, tempo_map);
            const int released = releasable ? common::core::releasedFret(*source) : 0;
            const bool justified =
                releasable && released > note.fret && !note.harmonic_node.has_value();
            if (!justified)
            {
                const bool hammerable = releasable && released < note.fret &&
                                        (note.fret > 0 || note.harmonic_node.has_value());
                note.attack =
                    hammerable ? common::core::NoteAttack::Hammer : common::core::NoteAttack::Pick;
            }
        }
        else if (
            note.attack == common::core::NoteAttack::Hammer && note.fret == 0 &&
            !note.harmonic_node.has_value()
        )
        {
            // Nothing to strike: the pull a still-held higher predecessor justifies, else a
            // plain pick — never an invented articulation.
            const bool pullable =
                source != nullptr && !common::core::fretHandHarmonic(*source) &&
                common::core::releasedFret(*source) > 0 &&
                common::core::predecessorHoldReaches(
                    source->position, effective_sustains[source_index], note.position, tempo_map);
            note.attack =
                pullable ? common::core::NoteAttack::Pull : common::core::NoteAttack::Pick;
        }
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = note_index;
        }
    }
}

} // namespace rock_hero::editor::core
