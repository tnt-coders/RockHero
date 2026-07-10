// Library-private projection from the chart domain model to the highway view state. Public per
// the placement procedure only once a renderer target consumes it directly; today the tests and
// future product-core composition are the consumers.
#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

// Resolves an arrangement's chart to seconds through the tempo map, mirroring the editor's tab
// projection discipline so both products read identical times for identical inputs. Returns an
// empty state when the arrangement has no chart.
[[nodiscard]] HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map, HighwayDisplayOptions options);

} // namespace rock_hero::common::core
