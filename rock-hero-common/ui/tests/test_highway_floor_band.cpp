#include "highway/highway_floor_band.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/highway/highway_diagnostics_options.h>
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
    const std::optional<HighwaySpan> inside = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(inside.has_value());
    CHECK_THAT(inside->from, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(inside->to, Catch::Matchers::WithinULP(3.0, 0));

    // Playback consumes the span from below: past the onset, the hit line is the start.
    const std::optional<HighwaySpan> consumed = highwayVisibleSpan(2.0, 8.0, 5.0, 10.0);
    REQUIRE(consumed.has_value());
    CHECK_THAT(consumed->from, Catch::Matchers::WithinULP(5.0, 0));
    CHECK_THAT(consumed->to, Catch::Matchers::WithinULP(8.0, 0));

    // The horizon clips the far end without moving the near one.
    const std::optional<HighwaySpan> clipped = highwayVisibleSpan(2.0, 40.0, 1.0, 10.0);
    REQUIRE(clipped.has_value());
    CHECK_THAT(clipped->from, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(clipped->to, Catch::Matchers::WithinULP(10.0, 0));
}

// Whether the span's own end is what stopped it — the fact a band's cap and a light's far fade
// both claim an ENDING from. Reported by the clamp rather than re-derived beside it, because a
// mark that answered it for itself could put an end mark where the horizon merely cut the span.
TEST_CASE("Highway visible span reports whether the end is the span's own", "[ui][highway]")
{
    // Ending well inside the window, and exactly ON the horizon: both are genuine ends.
    const std::optional<HighwaySpan> ends = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(ends.has_value());
    CHECK(ends->ends_inside);

    const std::optional<HighwaySpan> at_horizon = highwayVisibleSpan(2.0, 10.0, 1.0, 10.0);
    REQUIRE(at_horizon.has_value());
    CHECK(at_horizon->ends_inside);

    // Clipped by the horizon: the drawn far edge is the board's, not the note's.
    const std::optional<HighwaySpan> clipped = highwayVisibleSpan(2.0, 40.0, 1.0, 10.0);
    REQUIRE(clipped.has_value());
    CHECK_FALSE(clipped->ends_inside);

    // Playback consuming the near end does not change the answer about the far one.
    const std::optional<HighwaySpan> consumed = highwayVisibleSpan(2.0, 8.0, 5.0, 10.0);
    REQUIRE(consumed.has_value());
    CHECK(consumed->ends_inside);
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
    const std::optional<HighwaySpan> band = highwayVisibleSpan(2.0, 3.0, 1.0, 10.0);
    REQUIRE(band.has_value());
    CHECK_THAT(band->from, Catch::Matchers::WithinULP(2.0, 0));
    CHECK_THAT(band->to, Catch::Matchers::WithinULP(3.0, 0));
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

// The light's soft ends. On a ring long enough the ramp is the one asked for; on a short ring it is
// half the ring, so the rise and the fall meet at the midpoint instead of crossing. The regression
// this pins: with a fixed ramp, a ring of one ramp or less evaluated to zero at both of its only
// two exact times — the onset and the end — and the light for exactly the shortest rings (a dead
// note's, a chug's) was drawn at alpha zero everywhere.
TEST_CASE("Highway floor light ramp clamps to half a short ring", "[ui][highway]")
{
    // A ring of a second takes the asked-for 50 ms; a 40 ms ring takes 20 ms. The ring's length
    // is a difference of absolute times, so the clamped value is tolerant, not exact.
    CHECK_THAT(highwayFloorLightRamp(10.0, 11.0, 0.05), Catch::Matchers::WithinULP(0.05, 0));
    CHECK_THAT(highwayFloorLightRamp(10.0, 10.04, 0.05), Catch::Matchers::WithinAbs(0.02, 1e-9));

    // Two ramps long: the corners coincide and the clamp changes nothing.
    CHECK_THAT(highwayFloorLightRamp(10.0, 10.1, 0.05), Catch::Matchers::WithinAbs(0.05, 1e-9));
}

// The envelope itself: nothing at the ends, the peak between the two corners, and the peak reached
// on a short ring at its midpoint — the value the fixed-ramp arithmetic never produced.
TEST_CASE("Highway floor light envelope peaks between its corners", "[ui][highway]")
{
    // A long ring: zero at the onset, full from 50 ms in, full again until 50 ms before the end,
    // and the rise is linear in between.
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, true, 0.05, 10.0),
        Catch::Matchers::WithinULP(0.0, 0));
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, true, 0.05, 10.025),
        Catch::Matchers::WithinAbs(0.5, 1e-9));
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, true, 0.05, 10.05),
        Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, true, 0.05, 10.95),
        Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, true, 0.05, 11.0),
        Catch::Matchers::WithinULP(0.0, 0));

    // A 40 ms ring reaches the full peak at its midpoint.
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 10.04, true, 0.05, 10.02),
        Catch::Matchers::WithinAbs(1.0, 1e-9));

    // Clipped by the horizon, the far end does not fade: full right up to the drawn edge.
    CHECK_THAT(
        highwayFloorLightEnvelope(10.0, 11.0, false, 0.05, 11.0),
        Catch::Matchers::WithinAbs(1.0, 1e-9));
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

// Where a floor mark under one note lies. Both cases in one helper because the tail's band, the
// diagnostics band and the diagnostics light each need them and would otherwise each spell out the
// anchor and the open-string margin — three copies of a rule that has to agree exactly, since the
// marks are drawn on top of one another.
TEST_CASE("Highway floor footprint centres on the note or spans the window", "[ui][highway]")
{
    const common::core::HighwayMetrics metrics;
    // The window is unread for a fretted note; a deliberately absurd one proves it.
    const std::optional<HighwayFloorFootprint> fretted = highwayFloorFootprint(
        noteSpanning(1.0, 2.0), 0.25, std::pair{-100.0, 100.0}, metrics, false);
    REQUIRE(fretted.has_value());
    CHECK_THAT(
        fretted->center_x,
        Catch::Matchers::WithinULP(common::core::highwayNoteCenterX(5, metrics, false), 0));
    CHECK_THAT(fretted->half_width, Catch::Matchers::WithinULP(0.25, 0));

    // An open string ignores the asked-for width entirely and takes the window inset by its own
    // margin at each end, the treatment its bar and its tail already have.
    common::core::NoteViewState open = noteSpanning(1.0, 2.0);
    open.fret = 0;
    const std::optional<HighwayFloorFootprint> spanning =
        highwayFloorFootprint(open, 0.25, std::pair{2.0, 6.0}, metrics, false);
    REQUIRE(spanning.has_value());
    CHECK_THAT(spanning->center_x, Catch::Matchers::WithinULP(4.0, 0));
    CHECK_THAT(spanning->half_width, Catch::Matchers::WithinAbs(1.8, 1e-12));

    // A tapered neck's window can narrow past those insets mid-morph, and then an open mark has
    // nowhere to lie rather than an inside-out one.
    CHECK_FALSE(highwayFloorFootprint(open, 0.25, std::pair{3.0, 3.2}, metrics, false).has_value());
}

// The per-note gate, across every combination of the two filters. One predicate for all three
// forms of the mark: the filters say WHICH notes are marked, so a light and a band could not
// disagree about a note even if their draw code did.
TEST_CASE("Actual-ring mark gate answers the two filters for every form", "[ui][highway]")
{
    constexpr std::size_t chord_members = 3;
    constexpr std::size_t single_note = 1;

    // Off is the whole answer, whatever the filters say.
    CHECK_FALSE(highwayRingMarkApplies(single_note, false, HighwayDiagnosticsOptions{}));

    // The defaults mark everything the rig can reach.
    const HighwayDiagnosticsOptions everything{
        .actual_ring = ActualRingLook::Light,
        .actual_ring_marks_tailed_notes = true,
        .actual_ring_marks_chord_members = true,
    };
    CHECK(highwayRingMarkApplies(single_note, false, everything));
    CHECK(highwayRingMarkApplies(single_note, true, everything));
    CHECK(highwayRingMarkApplies(chord_members, false, everything));
    CHECK(highwayRingMarkApplies(chord_members, true, everything));

    // Tailed notes off: only the notes whose ring the presented form does not already draw.
    const HighwayDiagnosticsOptions untailed_only{
        .actual_ring = ActualRingLook::Fill,
        .actual_ring_marks_tailed_notes = false,
        .actual_ring_marks_chord_members = true,
    };
    CHECK(highwayRingMarkApplies(single_note, false, untailed_only));
    CHECK_FALSE(highwayRingMarkApplies(single_note, true, untailed_only));

    // Chord members off: a box already states its posture's hold, so its members drop out — and a
    // single note keeps its mark whether or not it has a tail.
    const HighwayDiagnosticsOptions singles_only{
        .actual_ring = ActualRingLook::Outline,
        .actual_ring_marks_tailed_notes = true,
        .actual_ring_marks_chord_members = false,
    };
    CHECK(highwayRingMarkApplies(single_note, true, singles_only));
    CHECK_FALSE(highwayRingMarkApplies(chord_members, true, singles_only));

    // Both off: a tailed chord member fails on either filter alone, and only an untailed single
    // note is left.
    const HighwayDiagnosticsOptions both_off{
        .actual_ring = ActualRingLook::Light,
        .actual_ring_marks_tailed_notes = false,
        .actual_ring_marks_chord_members = false,
    };
    CHECK(highwayRingMarkApplies(single_note, false, both_off));
    CHECK_FALSE(highwayRingMarkApplies(single_note, true, both_off));
    CHECK_FALSE(highwayRingMarkApplies(chord_members, false, both_off));
    CHECK_FALSE(highwayRingMarkApplies(chord_members, true, both_off));

    // The chord filter reads the chord BOX's own rule, so the boundary is the box's: a dyad is a
    // chord, a single fretting-hand member is not.
    CHECK_FALSE(highwayRingMarkApplies(2, true, singles_only));
    CHECK(highwayRingMarkApplies(1, true, singles_only));
}

} // namespace rock_hero::common::ui
