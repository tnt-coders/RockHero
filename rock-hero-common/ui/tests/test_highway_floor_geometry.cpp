#include "highway/highway_floor_geometry.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <utility>

namespace rock_hero::common::ui
{

namespace
{

// One note reduced to what the floor geometry reads: an onset, an end, a string and a fret.
[[nodiscard]] common::core::NoteViewState noteSpanning(
    const double start_seconds, const double end_seconds)
{
    common::core::NoteViewState note;
    note.string = 1;
    note.fret = 5;
    note.start_seconds = start_seconds;
    note.end_seconds = end_seconds;
    return note;
}

} // namespace

// The clamp every drawn span obeys. Stated once here so two marks over the same note cannot
// disagree about where its span begins; inside the draw pass, where it used to live twice, neither
// copy had a witness.
TEST_CASE("Highway visible span clamps to the hit line and the horizon", "[ui][highway]")
{
    // A note fully inside the window keeps both of its own ends.
    const std::optional<HighwaySpan> inside = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(inside.has_value());
    if (inside.has_value())
    {
        CHECK_THAT(inside->from, Catch::Matchers::WithinULP(2.0, 0));
        CHECK_THAT(inside->to, Catch::Matchers::WithinULP(3.0, 0));
    }

    // Playback consumes the span from below: past the onset, the hit line is the start.
    const std::optional<HighwaySpan> consumed = highwayVisibleSpan(2.0, 8.0, 5.0, 10.0);
    REQUIRE(consumed.has_value());
    if (consumed.has_value())
    {
        CHECK_THAT(consumed->from, Catch::Matchers::WithinULP(5.0, 0));
        CHECK_THAT(consumed->to, Catch::Matchers::WithinULP(8.0, 0));
    }

    // The horizon clips the far end without moving the near one.
    const std::optional<HighwaySpan> clipped = highwayVisibleSpan(2.0, 40.0, 1.0, 10.0);
    REQUIRE(clipped.has_value());
    if (clipped.has_value())
    {
        CHECK_THAT(clipped->from, Catch::Matchers::WithinULP(2.0, 0));
        CHECK_THAT(clipped->to, Catch::Matchers::WithinULP(10.0, 0));
    }
}

// The span never inverts, and it stops existing the moment the hit line passes the end it was
// asked about — the three "nothing to draw" conditions the tail pass used to spell out beside the
// clamp, with no second condition at any call site to keep in step with this one.
TEST_CASE("Highway visible span never inverts and empties once now passes", "[ui][highway]")
{
    // A tail the drop rule emptied: its end IS its onset, so there is no span at all.
    CHECK_FALSE(highwayVisibleSpan(2.0, 2.0, 1.0, 10.0).has_value());

    // Now exactly on the end, and past it: nothing left either way.
    CHECK_FALSE(highwayVisibleSpan(2.0, 5.0, 5.0, 10.0).has_value());
    CHECK_FALSE(highwayVisibleSpan(2.0, 5.0, 7.0, 10.0).has_value());

    // Still beyond the horizon, and a horizon that has fallen behind the hit line entirely.
    CHECK_FALSE(highwayVisibleSpan(12.0, 14.0, 1.0, 10.0).has_value());
    CHECK_FALSE(highwayVisibleSpan(2.0, 8.0, 5.0, 4.0).has_value());
}

// Where a floor mark under one note lies. Both cases in one helper because a mark and the tail
// above it would otherwise each spell out the anchor and the open-string margin — two copies of a
// rule that has to agree exactly, since they are drawn on top of one another.
TEST_CASE("Highway floor footprint centres on the note or spans the window", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    // The window is unread for a fretted note; a deliberately absurd one proves it.
    const std::optional<HighwayFloorFootprint> fretted = highwayFloorFootprint(
        noteSpanning(1.0, 2.0), 0.25, std::pair{-100.0, 100.0}, metrics, false);
    REQUIRE(fretted.has_value());
    if (fretted.has_value())
    {
        CHECK_THAT(
            fretted->center_x,
            Catch::Matchers::WithinULP(common::core::highwayNoteCenterX(5, metrics, false), 0));
        CHECK_THAT(fretted->half_width, Catch::Matchers::WithinULP(0.25, 0));
    }

    // An open string ignores the asked-for width entirely and takes the window inset by its own
    // margin at each end, the treatment its bar and its tail already have.
    common::core::NoteViewState open = noteSpanning(1.0, 2.0);
    open.fret = 0;
    const std::optional<HighwayFloorFootprint> spanning =
        highwayFloorFootprint(open, 0.25, std::pair{2.0, 6.0}, metrics, false);
    REQUIRE(spanning.has_value());
    if (spanning.has_value())
    {
        CHECK_THAT(spanning->center_x, Catch::Matchers::WithinULP(4.0, 0));
        CHECK_THAT(spanning->half_width, Catch::Matchers::WithinAbs(1.8, 1e-12));
    }

    // A tapered neck's window can narrow past those insets mid-morph, and then an open mark has
    // nowhere to lie rather than an inside-out one.
    CHECK_FALSE(highwayFloorFootprint(open, 0.25, std::pair{3.0, 3.2}, metrics, false).has_value());
}

} // namespace rock_hero::common::ui
