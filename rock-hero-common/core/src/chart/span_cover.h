/*!
\file span_cover.h
\brief WHICH hand-posture span covers an instant.

Private to rock_hero_common_core. Three rules are measured against this same coverage and none of
them may answer it differently: a span member with no tail of its own is HELD to the span's reach
(\ref chartHolds), a ribbon is CURTAINED exactly on the stretches a span stands over it
(\ref presentedChartNotes, the tail law), and a bare tap's held stop DEFAULTS to the grip the
covering span states (\ref chartHeldStops). Three walks over the same spans would be one rule
spelled three times and free to drift, which is why the walk lives here rather than in the files
that ask.
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
\brief A stretch of the timeline span furniture stands over.

Half-open — `from` inclusive, `to` exclusive — like the ribbon it is measured against, and already
MERGED across a junction: two spans tiling exactly at their closes leave no gap for the curtain to
lift in, so they answer as ONE stretch rather than as two the caller would have to join back
together (user ruling 2026-09-07, the curtain lifting at a span's close).
*/
struct SpanStretch
{
    /*! \brief Where the furniture takes the ribbon. */
    GridPosition from{};

    /*! \brief Where it lets the ribbon go, exclusive. */
    GridPosition to{};
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
        m_ends.reserve(shapes.size());
        m_best.reserve(shapes.size());
        std::size_t best = 0;
        for (std::size_t span = 0; span < shapes.size(); ++span)
        {
            m_ends.push_back(
                advanceGridPosition(tempo_map, shapes[span].position, shapes[span].sustain));
            if (m_ends[best] < m_ends[span])
            {
                best = span;
            }
            m_best.push_back(best);
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
        const std::size_t best =
            m_best[static_cast<std::size_t>(std::distance(m_shapes.begin(), first_excluded)) - 1];
        if (m_ends[best] < at)
        {
            return std::nullopt;
        }
        return SpanCoverage{.span = best, .end = m_ends[best]};
    }

    /*!
    \brief THE STRETCHES of a ribbon that span furniture stands over, ascending and never abutting.

    THE TAIL LAW's coverage question in its final form (user ruling 2026-09-07): a ribbon is
    curtained exactly on the stretches a span stands over it and drawn at full everywhere else, so
    the answer is not one instant but a set of them. A ring struck under a span opens its first
    stretch at its own head; a ring struck on open board that rings into a later bracket opens one
    at that bracket's front; a ring crossing a gap between two brackets is answered twice. A span
    opening exactly AT the ribbon's end covers none of it — `to` is exclusive — and a span whose
    reach lands exactly on the instant it is asked about covers nothing either, since a stretch of
    no length curtains no pixel.

    Written into the caller's buffer rather than returned, so one buffer serves a whole stream
    instead of a heap allocation per note.

    \param from Where the ribbon starts (the note's onset).
    \param to Where the ribbon ends, exclusive.
    \param out Cleared, then filled with the covered stretches in ascending order.
    */
    void covering(
        const GridPosition& from, const GridPosition& to, std::vector<SpanStretch>& out) const
    {
        out.clear();
        if (!(from < to))
        {
            return;
        }
        // The span already standing at the ribbon's start opens the first stretch there — the same
        // question \ref reaching answers for every other rule, asked once rather than restated.
        if (const std::optional<SpanCoverage> at_start = reaching(from); at_start.has_value())
        {
            if (const GridPosition end = std::min(at_start->end, to); from < end)
            {
                out.push_back(SpanStretch{.from = from, .to = end});
            }
        }
        // ...and every span opening strictly inside the ribbon opens one of its own. Spans never
        // overlap ("Chart shape derivation never overlaps two spans"), so each one's own reach is
        // the whole of what it covers and the walk needs no running maximum.
        std::size_t span = static_cast<std::size_t>(std::distance(
            m_shapes.begin(),
            std::ranges::upper_bound(m_shapes, from, std::ranges::less{}, &ChartShape::position)));
        for (; span < m_shapes.size() && m_shapes[span].position < to; ++span)
        {
            const GridPosition end = std::min(m_ends[span], to);
            if (!(m_shapes[span].position < end))
            {
                continue;
            }
            if (!out.empty() && !(out.back().to < m_shapes[span].position))
            {
                // A JUNCTION: this span opens where the last one closed, so the curtain never
                // lifts between them and a member ring crossing the seam stays curtained straight
                // through — one stretch, not two touching ones.
                if (out.back().to < end)
                {
                    out.back().to = end;
                }
                continue;
            }
            out.push_back(SpanStretch{.from = m_shapes[span].position, .to = end});
        }
    }

private:
    const std::vector<ChartShape>& m_shapes;

    // Each span's own reach, the one place a span's end is computed.
    std::vector<GridPosition> m_ends;

    // Per prefix of the span list: WHICH of the first N spans reaches furthest, as an index into
    // the ends above — the end itself is never copied here, so the two tables cannot disagree.
    std::vector<std::size_t> m_best;
};

} // namespace rock_hero::common::core
