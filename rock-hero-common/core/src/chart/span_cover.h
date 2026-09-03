/*!
\file span_cover.h
\brief WHICH hand-posture span covers an instant, and which FIGURE that span stands in.

Private to rock_hero_common_core. Three rules are measured against this same coverage and none of
them may answer it differently: a span member with no tail of its own is HELD to the span's reach
(\ref chartHolds), a ring the FIGURE accounts for draws no ribbon (\ref presentedChartNotes, the
tail law), and a bare tap's held stop DEFAULTS to the grip the covering span states
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
2026-09-04). All three rules here ask about the HAND — is it still down, is it still holding this
grip, is this ring still the figure's — and rule 12a's margin is about ink. While the stored extent
carried the trim, every one of them stopped one margin early: the hold ended a margin before the
hand did, the bracket clip needed a margin-back probe to reconstruct the close it had lost, and the
held default fell back to the open string in the last margin of the very span it was standing under.
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

    /*!
    \brief WHICH FIGURE the reaching span stands in: a maximal run of spans abutting at their
    musical closes.

    THE DISPLAY UNIT the tail law is written against. A figure is what a reader meets as one piece
    of furniture — one statement about where the hand is, carried across the seams where the grip
    changed — so "does the furniture account for this whole ring" is a question about the run and
    never about the one span the ring happens to start under. Runs are what let the founding pair,
    the ring carried in from the span before, and the ring outliving the figure all fall out of one
    comparison instead of being three rulings.

    ABUTMENT IS EXACT, with no tolerance of any kind: a span opens a new figure exactly when it
    starts strictly past the reach already standing. That is only decidable because
    \ref ChartShape::sustain stores the MUSICAL CLOSE — while it carried rule 12a's display trim,
    a growth split closed one margin before its successor's own start and no seam in the chart
    abutted at all.

    Runs are contiguous index ranges, which is what makes the interval question free: two instants
    in the same run have the whole stretch between them covered, because a run tiles with no gap by
    its own construction.
    */
    std::size_t figure{0};
};

/*!
\brief The FURTHEST-REACHING span started at or before an instant.

The highway's chord grouping asks the same question with a different rule — the LATEST-STARTING one
— and the two agree because SPANS NEVER OVERLAP: a closing event ends a span at or before its own
instant, a growth split divides one extent, and a landing successor opens exactly where its
predecessor closes, so the ends are non-decreasing and the last start is also the furthest reach.
Pinned by "Chart shape derivation never overlaps two spans" rather than assumed at either site
(review N12).

The FURTHEST end any span already started reaches, never the latest-STARTING span's: an earlier one
running longer covers the same strum just as well. Tracking the latest starter let a long shape be
shadowed by a short one that began inside it, so a held chord silently lost its extension and the
legato that extension justified was repaired away.

A prefix table over the span list rather than a forward cursor, because the bracket clip asks it at
RING ENDS, which do not ascend the way onsets do — a long ring beside a short one ends later while
starting earlier. One O(spans) build serves every query at O(log spans), stateless, so the ascending
and the non-ascending caller read the same authority.
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
        std::size_t figure = 0;
        for (std::size_t span = 0; span < shapes.size(); ++span)
        {
            const GridPosition span_end =
                advanceGridPosition(tempo_map, shapes[span].position, shapes[span].sustain);
            // A span starting STRICTLY PAST the reach already standing opens a new figure; one
            // starting exactly at that reach ABUTS and inherits. Exact, because the stored close is
            // the musical one and abutting spans therefore tile in the data and not merely in the
            // walk's reasoning.
            if (best.has_value() && best->end < shapes[span].position)
            {
                ++figure;
            }
            // A span that OPENED a figure always becomes the reaching one — it starts past the
            // standing reach, so it ends past it too — which is why the figure below is never
            // stale: where this does not fire, the span joined the run the standing reach is
            // already labelled with.
            if (!best.has_value() || best->end < span_end)
            {
                best = SpanCoverage{.span = span, .end = span_end, .figure = figure};
            }
            m_best.push_back(*best);
        }
    }

    /*!
    \brief The span covering an instant, or nothing where none reaches it.

    \param at Instant the caller is asking about.

    \return The furthest-reaching span started at or before `at` whose own reach is not behind it.
    */
    [[nodiscard]] std::optional<SpanCoverage> reaching(const GridPosition& at) const
    {
        const auto after =
            std::ranges::upper_bound(m_shapes, at, std::ranges::less{}, &ChartShape::position);
        if (after == m_shapes.begin())
        {
            return std::nullopt;
        }
        const SpanCoverage& best =
            m_best[static_cast<std::size_t>(std::distance(m_shapes.begin(), after)) - 1];
        if (best.end < at)
        {
            return std::nullopt;
        }
        return best;
    }

private:
    const std::vector<ChartShape>& m_shapes;

    // Per prefix of the span list: the furthest-reaching span among the first N.
    std::vector<SpanCoverage> m_best;
};

} // namespace rock_hero::common::core
