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
\brief An event occupying a timeline span: an onset and an end, both in absolute seconds.

The shape every seconds-resolved view state shares, and the whole of what the search below needs.
It requires the end as well as the onset because the two are only meaningful together here: the
onset orders the events, and the prefix maximum of the ends is what bounds the range's start.

\tparam Event Seconds-resolved event type exposing `start_seconds` and `end_seconds`.
*/
template <typename Event>
concept SustainedEvent = requires(const Event& event) {
    { event.start_seconds } -> std::convertible_to<double>;
    { event.end_seconds } -> std::convertible_to<double>;
};

/*!
\brief Builds the running maximum of event end times, one entry per event.

Companion table for visibleEventRange: events are sorted by onset but their spans overlap freely,
so the range's lower bound comes from the prefix maximum of the ends rather than from the ends
themselves. Callers whose DISPLAY end differs from the stored end — a strum held for a whole
posture span — pass those ends in place of the events' own.

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

\tparam Event Sustained event type the list holds.
\param events Events sorted by start time.
\param prefix_max_end_seconds Running maximum of end times from makeSustainPrefixMax.
\param span_start_seconds Visible span start.
\param span_end_seconds Visible span end.
\return Half-open [first, last) index range of candidate events.
*/
template <SustainedEvent Event>
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
