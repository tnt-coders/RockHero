#include "chart/legato_normalize.h"

#include <array>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>

namespace rock_hero::editor::core
{

void normalizeChartLegato(
    std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map)
{
    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached. Repairs apply before a note is recorded, so a later note
    // judges against the repaired values.
    std::array<
        const common::core::ChartNote*,
        static_cast<std::size_t>(common::core::g_max_chart_strings) + 1>
        last_per_string{};
    for (common::core::ChartNote& note : notes)
    {
        const bool string_in_range =
            note.string >= 1 && note.string <= common::core::g_max_chart_strings;
        const common::core::ChartNote* const source =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string)) : nullptr;
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
                common::core::predecessorHoldReaches(*source, note.position, tempo_map);
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
                common::core::predecessorHoldReaches(*source, note.position, tempo_map);
            note.attack =
                pullable ? common::core::NoteAttack::Pull : common::core::NoteAttack::Pick;
        }
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = &note;
        }
    }
}

} // namespace rock_hero::editor::core
