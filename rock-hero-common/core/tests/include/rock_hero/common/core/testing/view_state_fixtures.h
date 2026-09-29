/*!
\file view_state_fixtures.h
\brief Hand-built view-state helpers, stated once for every suite that paints or hits a note.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core::testing
{

/*!
\brief The keyframes of a hand-built note whose every keyframe states a position: one per stop,
each wearing that stop's mark at the stop's instant.

The projection publishes a note's stops and its keyframes from one walk; a fixture that states its
stops by hand derives the keyframes here, so the two lists cannot disagree. Each offset is the
stop's one-based ordinal in beats: a hand-built view resolves no tempo map, and only a selection
keys a keyframe by its offset.

\param slides The note's stops, in time order.
\return One keyframe per stop, naming it.
*/
[[nodiscard]] inline std::vector<KeyframeViewState> stopKeyframes(
    const std::vector<SlideStopViewState>& slides)
{
    std::vector<KeyframeViewState> keyframes;
    keyframes.reserve(slides.size());
    for (std::size_t index = 0; index < slides.size(); ++index)
    {
        keyframes.push_back(
            KeyframeViewState{
                .seconds = slides[index].seconds,
                .offset = Fraction{static_cast<int>(index) + 1},
                .mark = KeyframeStopMark{.stop = index},
                .bend_point = std::nullopt,
            });
    }
    return keyframes;
}

/*!
\brief A hand-built note given its stops, and the keyframes stating them (\ref stopKeyframes).
\param note The note, with no stops of its own yet.
\param stops The stops to give it, in time order.
\return The note carrying both lists.
*/
[[nodiscard]] inline NoteViewState withStops(
    NoteViewState note, std::vector<SlideStopViewState> stops)
{
    note.keyframes = stopKeyframes(stops);
    note.slides = std::move(stops);
    return note;
}

} // namespace rock_hero::common::core::testing
