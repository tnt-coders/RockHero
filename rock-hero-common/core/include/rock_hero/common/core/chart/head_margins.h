/*!
\file head_margins.h
\brief How long the minimum sustain distance is before every head of one chart revision.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The minimum sustain distance before each distinct head position, derived once per revision
by the one authority (\ref minimumSustainDistance) and read by every rule that keeps that distance.

Every margin a scene build keeps is kept before a HEAD — the onset that binds a ring
(\ref chartPresentation), the head that closes a span (\ref deriveChartShapes, the drawn extent),
the onset a hand's light rises toward (\ref NoteViewState::margin_start_seconds) — so the question
has one answer per distinct onset, and chords share it. The table only stops a build asking that
question again: each entry is the authority's own answer, so a margin read here and one asked
directly cannot differ. Keyed by position because the closing head of a span is known by position.

A fret-hand placement is not a head, so its ramp asks the authority directly; a lookup that fell
back to computing would be two paths to one answer.

Built from one revision's saved note stream and its tempo map, and valid for that pair alone.
*/
class HeadMargins
{
public:
    /*! \brief A table of no heads: what a chart with no notes derives. */
    HeadMargins() = default;

    /*!
    \brief Asks the authority once per distinct onset of a note stream.

    \param notes Note stream sorted by position; every onset in it becomes a head.
    \param places Each note's whole-note place (\ref chartNotePlaces), index-parallel to `notes`.
    \param tempo_map Song tempo map supplying the time axis.
    */
    HeadMargins(
        const std::vector<ChartNote>& notes, const std::vector<Fraction>& places,
        const TempoMap& tempo_map);

    /*!
    \brief The margin before a head: \ref minimumSustainDistance at that head.

    \param head An onset of the stream the table was built from; asking anything else is a
           precondition violation.
    \return The margin in whole notes.
    */
    [[nodiscard]] Fraction length(const GridPosition& head) const;

private:
    // One distinct onset and the margin kept before it.
    struct Entry
    {
        GridPosition head;
        Fraction length;
    };

    // The entry for a head the table holds.
    [[nodiscard]] const Entry& entryAt(const GridPosition& head) const;

    // Ascending and distinct by head.
    std::vector<Entry> m_entries;
};

} // namespace rock_hero::common::core
