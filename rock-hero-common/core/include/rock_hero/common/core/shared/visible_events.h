/*!
\file visible_events.h
\brief Visible-range search shared by every surface that draws sustained timeline events.
*/

#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Tolerance for matching an onset to another onset or a shape-span boundary: the one
answer, on either surface, to "are these two chart times one moment".

A true rounding tolerance and nothing more: a nanosecond is four orders above the double rounding
error at song scale and six orders below the finest grid the editor offers (a 1/128 note is 15 ms at
120 BPM), so it can only ever absorb arithmetic noise, never join two musically distinct events.

It was 1e-4 s, on the stated grounds that a note onset and a shape boundary resolve through
different tempo-map paths and so land a rounding epsilon apart. They do not: the forward cursor is
documented as returning bit-identical results and computes the same expression against the same
anchor span as the plain resolver, so equal grid positions resolve to equal seconds. The oversized
value was the sole reason the display's simultaneity rule could group notes at DISTINCT musical
positions that the chart-side rule (the onset grouping inside `chartHolds`) refuses — a divergence
`grid_arithmetic.h` recorded as deliberate. With the tolerance honest, the two rules agree.
*/
inline constexpr double g_onset_match_epsilon = 1.0e-9;

/*!
\brief An event placed on the timeline: an onset in absolute seconds, and nothing more.

The whole of what \ref visibleEventRange reads off an event. The search orders by the onset and
takes every end through the prefix table handed to it, which is exactly why an event whose DISPLAY
end differs from any single stored one — a hand-posture span, which carries a drawn extent and a
musical close and lets the surface pick — can be searched at all.

\tparam Event Seconds-resolved event type exposing `start_seconds`.
*/
template <typename Event>
concept TimedEvent = requires(const Event& event) {
    { event.start_seconds } -> std::convertible_to<double>;
};

/*!
\brief An event occupying a timeline span: an onset and an end, both in absolute seconds.

The narrower shape, required only where the end is actually read: the convenience overload of
\ref makeSustainPrefixMax below, which builds its table off the events themselves. An event with
more than one end names the one it means instead and uses the projecting overload.

\tparam Event Seconds-resolved event type exposing `start_seconds` and `end_seconds`.
*/
template <typename Event>
concept SustainedEvent = TimedEvent<Event> && requires(const Event& event) {
    { event.end_seconds } -> std::convertible_to<double>;
};

/*!
\brief Builds the running maximum of event end times, one entry per event.

Companion table for visibleEventRange: events are sorted by onset but their spans overlap freely,
so the range's lower bound comes from the prefix maximum of the ends rather than from the ends
themselves. Callers whose DISPLAY end differs from the one the event carries — a strum held for a
whole posture span, or a span whose furniture the editor's reveal runs on to its musical close —
pass those ends in place of the events' own, and must pass the FURTHEST they may draw to: the table
only ever tightens the range's start, so a conservative end costs candidates and a short one drops
something on screen.

\tparam Ends Sized range of end times in seconds, ordered like the events they describe.
\param end_seconds End times to accumulate.
\return Non-decreasing prefix maximum of the entries, sized like the input.
*/
template <std::ranges::sized_range Ends>
    requires std::convertible_to<std::ranges::range_value_t<Ends>, double>
[[nodiscard]] std::vector<double> makeSustainPrefixMax(const Ends& end_seconds)
{
    std::vector<double> prefix_max;
    prefix_max.reserve(static_cast<std::size_t>(std::ranges::size(end_seconds)));
    double running = -std::numeric_limits<double>::infinity();
    for (const double end : end_seconds)
    {
        running = std::max(running, end);
        prefix_max.push_back(running);
    }
    return prefix_max;
}

/*!
\brief Builds the running maximum of event end times, one entry per event.

Overload for the ordinary case, where the events carry the ends themselves, so no call site has to
spell the projection out.

\tparam Events Sized range of sustained events, ordered by onset.
\param events Events whose ends to accumulate.
\return Non-decreasing prefix maximum of the events' ends, sized like the input.
*/
template <std::ranges::sized_range Events>
    requires SustainedEvent<std::ranges::range_value_t<Events>>
[[nodiscard]] std::vector<double> makeSustainPrefixMax(const Events& events)
{
    using Event = std::ranges::range_value_t<Events>;
    return makeSustainPrefixMax(events | std::views::transform(&Event::end_seconds));
}

/*!
\brief Returns the event index range that can intersect a visible time span.

Sorted onsets bound the range's end; the non-decreasing prefix maximum of ends bounds its start,
because every event before the first index whose running maximum reaches the span ends strictly
before the span. The range is a tight superset — callers still intersect each event individually
because an early short event inside the range may end before the span begins.

\tparam Event Timed event type the list holds; only its onset is read here.
\param events Events sorted by start time.
\param prefix_max_end_seconds Running maximum of end times from makeSustainPrefixMax.
\param span_start_seconds Visible span start.
\param span_end_seconds Visible span end.
\return Half-open [first, last) index range of candidate events.
*/
template <TimedEvent Event>
[[nodiscard]] std::pair<std::size_t, std::size_t> visibleEventRange(
    const std::vector<Event>& events, const std::vector<double>& prefix_max_end_seconds,
    double span_start_seconds, double span_end_seconds) noexcept
{
    const auto begin_it = std::ranges::lower_bound(prefix_max_end_seconds, span_start_seconds);
    const auto end_it = std::ranges::upper_bound(
        events, span_end_seconds, std::ranges::less{}, &Event::start_seconds);

    const auto first =
        static_cast<std::size_t>(std::distance(prefix_max_end_seconds.begin(), begin_it));
    const auto last = static_cast<std::size_t>(std::distance(events.begin(), end_it));
    return {std::min(first, last), last};
}

} // namespace rock_hero::common::core
