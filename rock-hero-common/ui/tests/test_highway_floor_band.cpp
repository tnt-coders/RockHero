#include "highway/highway_floor_band.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// One note reduced to what both culls read: an onset and a PRESENTED end. Its actual ring travels
// separately, exactly as it does through the projection (ChartViewState::actual_end_seconds is a
// parallel array, deliberately not a NoteViewState field).
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

// The clamp both the sustain tail and the diagnostics band obey. Stated once here so a diagnostic
// and the tail directly above it cannot disagree about where the same note's span begins; inside
// the draw pass, where it used to live twice, neither copy had a witness.
TEST_CASE("Highway visible span clamps to the hit line and the horizon", "[ui][highway]")
{
    // A note fully inside the window keeps both of its own ends.
    const std::optional<std::pair<double, double>> inside = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(inside.has_value());
    CHECK_THAT(inside->first, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(inside->second, Catch::Matchers::WithinULP(3.0, 0));

    // Playback consumes the span from below: past the onset, the hit line is the start.
    const std::optional<std::pair<double, double>> consumed =
        highwayVisibleSpan(2.0, 8.0, 5.0, 10.0);
    REQUIRE(consumed.has_value());
    CHECK_THAT(consumed->first, Catch::Matchers::WithinULP(5.0, 0));
    CHECK_THAT(consumed->second, Catch::Matchers::WithinULP(8.0, 0));

    // The horizon clips the far end without moving the near one.
    const std::optional<std::pair<double, double>> clipped =
        highwayVisibleSpan(2.0, 40.0, 1.0, 10.0);
    REQUIRE(clipped.has_value());
    CHECK_THAT(clipped->first, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(clipped->second, Catch::Matchers::WithinULP(10.0, 0));
}

// The band keys on the ACTUAL ring, which is the whole reason it exists: a note the presentation
// rules left with no tail at all still has a ring to draw, and the span must come out of the same
// helper the tail asks with a different end.
TEST_CASE("Highway visible span reports a band where no tail is presented", "[ui][highway]")
{
    // A dropped tail: the presented end sits exactly on the onset, so the tail's own question has
    // no answer...
    CHECK_FALSE(highwayVisibleSpan(2.0, 2.0, 1.0, 10.0).has_value());

    // ...while the ring the string actually sounds for still spans a second of board.
    const std::optional<std::pair<double, double>> band = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(band.has_value());
    CHECK_THAT(band->first, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(band->second, Catch::Matchers::WithinULP(3.0, 0));
}

// The span never inverts, and it stops existing the moment the hit line passes the end it was
// asked about — the band's own "skip once now is past the actual end", with no second condition
// at the call site to keep in step with this one.
TEST_CASE("Highway visible span never inverts and empties once now passes", "[ui][highway]")
{
    // Now exactly on the end, and past it: nothing left either way.
    CHECK_FALSE(highwayVisibleSpan(2.0, 5.0, 5.0, 10.0).has_value());
    CHECK_FALSE(highwayVisibleSpan(2.0, 5.0, 7.0, 10.0).has_value());

    // Still beyond the horizon, and a horizon that has fallen behind the hit line entirely.
    CHECK_FALSE(highwayVisibleSpan(12.0, 14.0, 1.0, 10.0).has_value());
    CHECK_FALSE(highwayVisibleSpan(2.0, 8.0, 5.0, 4.0).has_value());
}

// The cull. The band needs a SECOND prefix maximum, over the actual ends, because the note pass's
// table is keyed on the presented tails: a note whose tail the drop rule emptied leaves that range
// immediately after its onset while its ring is still running across the visible window. Sharing
// one table would silently cut the band off at exactly the notes it exists to show.
TEST_CASE("Actual-ring prefix maximum keeps notes the presented table drops", "[ui][highway]")
{
    // Two short, tail-less notes early in the song whose rings run long, then a later note that
    // keeps the presented range non-empty over the window under test.
    const std::vector<common::core::NoteViewState> notes{
        noteSpanning(1.0, 1.0),
        noteSpanning(2.0, 2.0),
        noteSpanning(20.0, 21.0),
    };
    const std::vector<double> actual_end_seconds{6.0, 7.0, 21.0};

    const std::vector<double> presented_prefix = common::core::makeSustainPrefixMax(notes);
    const std::vector<double> actual_prefix =
        common::core::makeSustainPrefixMax(actual_end_seconds);

    // A window opening after both onsets but inside both rings.
    constexpr double span_start = 5.0;
    constexpr double span_end = 8.0;

    const auto [presented_first, presented_last] =
        common::core::visibleEventRange(notes, presented_prefix, span_start, span_end);
    const auto [actual_first, actual_last] =
        common::core::visibleEventRange(notes, actual_prefix, span_start, span_end);

    // Both ranges end at the same place — onsets bound the far end, and the third note has not
    // arrived yet — but the presented table has already walked past the two ringing notes.
    CHECK(presented_last == static_cast<std::size_t>(2));
    CHECK(actual_last == static_cast<std::size_t>(2));
    CHECK(presented_first == static_cast<std::size_t>(2));
    CHECK(actual_first == static_cast<std::size_t>(0));

    // Stated as the property rather than as indices: the rings are in range, the tails are not.
    CHECK(presented_first >= presented_last);
    CHECK(actual_first < actual_last);
}

} // namespace rock_hero::common::ui
