/*!
\file tempo_map_fixtures.h
\brief Tempo-map test fixtures: anchors spelled as the file spells them, and the common-time map.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::core::testing
{

/*!
\brief An anchor as the package file spells it: a grid token and whole milliseconds.

Milliseconds rather than seconds on purpose: the fixture states the stored unit, so no rounding
hides inside a helper and a time between two milliseconds cannot be written. A test spells a
\ref BeatAnchor by hand only when the anchor's shape is its subject.

\param position Grid token such as `"3:1"` or `"2:3+1/2"`; a malformed token throws
       `std::invalid_argument`, so a typo fails the test loudly instead of pinning `1:1`.
\param milliseconds Whole milliseconds from the timeline origin.
\return The anchor.
*/
[[nodiscard]] inline BeatAnchor anchor(std::string_view position, std::int64_t milliseconds)
{
    const std::optional<GridPosition> parsed = parseGridPositionToken(std::string{position});
    if (!parsed.has_value())
    {
        throw std::invalid_argument{"malformed grid token: " + std::string{position}};
    }
    return BeatAnchor{.position = *parsed, .milliseconds = milliseconds};
}

/*!
\brief The 4/4 meter from measure 1 that nearly every suite's tempo map starts from.
\return A single common-time signature at measure 1.
*/
[[nodiscard]] inline std::vector<TimeSignatureChange> commonTime()
{
    return {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}};
}

/*!
\brief A common-time tempo map over the given anchors.

Not validated, so a rules test can build the map it means to refuse.

\param anchors Anchors in the order the map stores them.
\return The tempo map.
*/
[[nodiscard]] inline TempoMap commonTimeMap(std::vector<BeatAnchor> anchors)
{
    return TempoMap{commonTime(), std::move(anchors)};
}

} // namespace rock_hero::common::core::testing
