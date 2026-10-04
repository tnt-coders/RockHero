/*!
\file span_cover.h
\brief WHICH hand-posture span covers an instant.

Private to rock_hero_common_core. Two rules are measured against this same coverage and neither may
answer it differently: a span member with no tail of its own is HELD to the span's reach (\ref
chartHolds), and a bare tap's held stop DEFAULTS to the grip the covering span states (\ref
chartHeldStops). The TAIL LAW asks nothing here: the curtain is UNIVERSAL (\ref chartPresentation
rule 4), so a tail showing no technique rests whether or not a span stands over it. Two walks over
the same spans would be one rule spelled twice and free to drift, which is why the walk lives here
rather than in the files that ask.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The span reaching an instant, and how far its statement reaches.

WHICH span reaches is part of the answer, not a second query: the hold reads only the distance, and
the held default asks that same covering span what grip it states — a caller that fetched the two
apart could pair a reach with a statement that did not make it.

THE MUSICAL CLOSE is what reaches (\ref ChartShape::sustain), never a drawn extent. Every rule
here asks about the HAND — is it still down, is it still holding this grip — and rule 12a's margin
is about ink, so measuring against a trimmed extent would stop every one of them one margin early:
the hold would end a margin before the hand does, and the held default would fall back to the open
string in the last margin of the very span it stands under.
*/
struct SpanCoverage
{
    /*!
    \brief The reaching span, as an index into the caller's own shape list.

    Which is what the span-parallel answers beside it — the arrival class, the posture table — are
    indexed by.
    */
    std::size_t span{0};

    /*! \brief How far that span's furniture reaches, as a place on the whole-note axis. */
    Fraction end{};
};

/*!
\brief WHICH span covers an instant: the FURTHEST-REACHING one already started when it arrives.

HALF-OPEN: a span covers its own front and not its close. An onset at a seam — one span closing
where the next opens — stands in the grip that ARRIVED: a note struck there is a member of the new
shape and not of the one it replaced, which is the seam-ownership half of the grip-tenure law. An
onset at a close where nothing arrives stands in no grip at all: the span ended there because the
chart stopped proving the grip, so a tap splitting it holds nothing the span stated. The tail law
asks nothing here at all, the curtain being universal, so this one query is the whole coverage
vocabulary.

The highway's chord grouping asks the same question with a different rule — the LATEST-STARTING one
— and the two agree because SPANS NEVER OVERLAP: a closing event ends a span at or before its own
instant, and a landing successor opens exactly where its predecessor closes, so the ends are
non-decreasing and the last start is also the furthest reach. Pinned by "Chart shape derivation
never overlaps two spans" rather than assumed at either site.

The FURTHEST end any span already started reaches, never the latest-STARTING span's: an earlier one
running longer covers the same strum just as well. Tracking the latest starter would let a long
shape be shadowed by a short one that began inside it, so a held chord would silently lose its
extension and the legato that extension justified would be repaired away.

A prefix table over the span list rather than a forward cursor, because the holds walk asks it per
onset group while the held-stop default asks it per tap — one O(spans) build serves every query at
O(log spans), stateless, so every caller reads the same authority. Every instant is a whole-note
place, so a reach is the span's own place plus its stored close and nothing is re-addressed.
*/
class SpanCover
{
public:
    /*!
    \brief Builds the prefix table over one revision's spans.

    \param shapes Spans sorted by place.
    */
    explicit SpanCover(const std::vector<ChartShape>& shapes)
    {
        m_starts.reserve(shapes.size());
        m_best.reserve(shapes.size());
        std::optional<SpanCoverage> best;
        for (std::size_t span = 0; span < shapes.size(); ++span)
        {
            const Fraction start = shapes[span].place;
            const Fraction span_end = start + shapes[span].sustain;
            if (!best.has_value() || best->end < span_end)
            {
                best = SpanCoverage{.span = span, .end = span_end};
            }
            m_starts.push_back(start);
            m_best.push_back(*best);
        }
    }

    /*!
    \brief The span an instant stands in: the furthest-reaching span started at or before it.

    \param at Whole-note place of the instant the caller is asking about.

    \return The furthest-reaching span started at or before `at` whose reach is still ahead of it.
    */
    [[nodiscard]] std::optional<SpanCoverage> reaching(const Fraction at) const
    {
        const auto first_excluded = std::ranges::upper_bound(m_starts, at);
        if (first_excluded == m_starts.begin())
        {
            return std::nullopt;
        }
        const SpanCoverage& best =
            m_best[static_cast<std::size_t>(std::distance(m_starts.begin(), first_excluded)) - 1];
        if (best.end <= at)
        {
            return std::nullopt;
        }
        return best;
    }

private:
    // Each span's front on the whole-note axis, ascending with the span list.
    std::vector<Fraction> m_starts;

    // Per prefix of the span list: the furthest-reaching span among the first N.
    std::vector<SpanCoverage> m_best;
};

} // namespace rock_hero::common::core
