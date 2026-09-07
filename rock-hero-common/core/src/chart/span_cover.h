/*!
\file span_cover.h
\brief WHICH hand-posture span covers an instant.

Private to rock_hero_common_core. Three rules are measured against this same coverage and none of
them may answer it differently: a span member with no tail of its own is HELD to the span's reach
(\ref chartHolds), a ring dying at its own span's close draws no ribbon (\ref presentedChartNotes,
the tail law), and a bare tap's held stop DEFAULTS to the grip the covering span states
(\ref chartHeldStops). Three walks over the same spans would be one rule spelled three times and
free to drift, which is why the walk lives here rather than in the files that ask.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The span reaching an instant, and how far its statement reaches.

WHICH span reaches is part of the answer, not a second query: the hold reads only the distance, the
bracket clip asks the covering span what class it arrives as, and the held default asks it what
grip it states — a caller that fetched the two apart could pair a reach with a statement that did
not make it.

THE MUSICAL CLOSE is what reaches (\ref ChartShape::sustain), never a drawn extent (user ruling
2026-09-04). Every rule here asks about the HAND — is it still down, is it still holding this
grip — and rule 12a's margin is about ink. While the stored extent carried the trim, every one of
them stopped one margin early: the hold ended a margin before the hand did, and the held default
fell back to the open string in the last margin of the very span it was standing under.
*/
struct SpanCoverage
{
    /*!
    \brief The reaching span, as an index into the caller's own shape list.

    Which is what the span-parallel answers beside it — the arrival class, the posture table — are
    indexed by.
    */
    std::size_t span{0};

    /*! \brief How far that span's furniture reaches. */
    GridPosition end{};
};

/*!
\brief WHICH span covers an instant: the FURTHEST-REACHING one already started when it arrives.

An onset at a seam — one span closing where the next opens — stands in the grip that ARRIVED: a
note struck there is a member of the new shape and not of the one it replaced, which is the seam
ownership half the grip-tenure law kept (user ruling 2026-09-04). The tail law's own-span form
asks nothing at ring ends any more — a ring is judged against the one span standing at its ONSET —
so this one query is the whole coverage vocabulary.

The highway's chord grouping asks the same question with a different rule — the LATEST-STARTING one
— and the two agree because SPANS NEVER OVERLAP: a closing event ends a span at or before its own
instant, and a landing successor opens exactly where its predecessor closes, so the ends are
non-decreasing and the last start is also the furthest reach. Pinned by "Chart shape derivation
never overlaps two spans" rather than assumed at either site (review N12).

The FURTHEST end any span already started reaches, never the latest-STARTING span's: an earlier one
running longer covers the same strum just as well. Tracking the latest starter let a long shape be
shadowed by a short one that began inside it, so a held chord silently lost its extension and the
legato that extension justified was repaired away.

A prefix table over the span list rather than a forward cursor, because the tail law asks it per
stroke while the holds walk asks it per onset group — one O(spans) build serves every query at
O(log spans), stateless, so every caller reads the same authority.
*/
class SpanCover
{
public:
    /*!
    \brief Builds the prefix table over one revision's spans.

    \param shapes Spans sorted by position; borrowed, so it must outlive this cover.
    \param tempo_map Song tempo map supplying the beat axis each span's extent is advanced along.
    */
    SpanCover(const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
        : m_shapes{shapes}
    {
        m_best.reserve(shapes.size());
        std::optional<SpanCoverage> best;
        for (std::size_t span = 0; span < shapes.size(); ++span)
        {
            const GridPosition span_end =
                advanceGridPosition(tempo_map, shapes[span].position, shapes[span].sustain);
            if (!best.has_value() || best->end < span_end)
            {
                best = SpanCoverage{.span = span, .end = span_end};
            }
            m_best.push_back(*best);
        }
    }

    /*!
    \brief The span an instant stands in: the furthest-reaching span started at or before it.

    \param at Instant the caller is asking about.

    \return The furthest-reaching span started at or before `at` whose own reach is not behind it.
    */
    [[nodiscard]] std::optional<SpanCoverage> reaching(const GridPosition& at) const
    {
        const auto first_excluded =
            std::ranges::upper_bound(m_shapes, at, std::ranges::less{}, &ChartShape::position);
        if (first_excluded == m_shapes.begin())
        {
            return std::nullopt;
        }
        const SpanCoverage& best =
            m_best[static_cast<std::size_t>(std::distance(m_shapes.begin(), first_excluded)) - 1];
        if (best.end < at)
        {
            return std::nullopt;
        }
        return best;
    }

    /*!
    \brief Where a ribbon first runs under a span: its own start where a span reaches that, else
    the front of the first span opening strictly inside it, else nothing.

    THE TAIL LAW's coverage question in its generalized form (user ruling 2026-09-07): a ribbon is
    judged not by the span standing at its ONSET alone but by where any part of it runs under a
    span, so a ring struck on open board that rings into a later bracket rests from that bracket's
    front. For a ribbon whose start a span reaches the answer is the start itself, which is the
    question \ref reaching answered before and every rested tail keeps its verdict. A span opening
    exactly AT the ribbon's end covers none of it and answers nothing — `to` is exclusive.

    \param from Where the ribbon starts (the note's onset).
    \param to Where the ribbon ends, exclusive.

    \return The instant the ribbon first stands under a span, or nothing where none stands over it.
    */
    [[nodiscard]] std::optional<GridPosition> firstCovered(
        const GridPosition& from, const GridPosition& to) const
    {
        if (reaching(from).has_value())
        {
            return from;
        }
        const auto next =
            std::ranges::upper_bound(m_shapes, from, std::ranges::less{}, &ChartShape::position);
        if (next == m_shapes.end() || !(next->position < to))
        {
            return std::nullopt;
        }
        return next->position;
    }

private:
    const std::vector<ChartShape>& m_shapes;

    // Per prefix of the span list: the furthest-reaching span among the first N.
    std::vector<SpanCoverage> m_best;
};

} // namespace rock_hero::common::core
