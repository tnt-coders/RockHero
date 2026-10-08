#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinULP;
using common::core::GridPosition;
using common::core::testing::anchor;
using common::core::testing::beatAt;

// A controller with the eight-measure song loaded: 4/4 at 120 BPM from 1:1 (0 s) to 9:1 (16 s),
// sections at 2:1 and 6:1, notes at 2:1 and 3:1 and hand positions at 1:1 and 3:1.
struct ScaleEditor : SongEditorHarness
{
    ScaleEditor()
    {
        open(makeEightMeasureSong());
    }

    [[nodiscard]] const common::core::Chart& chart() const
    {
        const common::core::Chart* const loaded = chartOrNull(controller);
        REQUIRE(loaded != nullptr);
        if (loaded == nullptr)
        {
            throw std::logic_error{"scale fixture without a chart"};
        }
        return *loaded;
    }

    void scale(const std::string& text)
    {
        controller.onScaleTempoRequested(text);
    }
};

} // namespace

// x2 doubles every span's tempo and every address on the whole-note axis while every time holds:
// the terminal 9:1 lands on 17:1 still at 16 s, the sections and notes on the measures twice as
// far in, and the whole song is one entry. The Tempo Lock, on at every open, does not block it.
TEST_CASE("Scale Tempo lands the whole song as one entry, every time held", "[core][scale-tempo]")
{
    ScaleEditor editor;
    REQUIRE(editor.state().tempo_lock);

    editor.scale("2");
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Scale Tempo");
    CHECK(
        editor.tempoMap() ==
        common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("17:1", 16000)}));

    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].position == beatAt(3, 1));
    CHECK(editor.sections()[1].position == beatAt(11, 1));
    CHECK_THAT(editor.tempoMap().secondsAt(editor.sections()[1].position), WithinAbs(10.0, 1e-9));

    const common::core::Chart& chart = editor.chart();
    REQUIRE(chart.notes.size() == 3);
    CHECK(chart.notes[0].position == beatAt(3, 1));
    CHECK(chart.notes[2].position == beatAt(5, 1));
    // The half-note ring from 3:1 ended at 3:3 (5 s); a whole note from 5:1 ends there still.
    CHECK(chart.notes[2].sustain == common::core::Fraction{1});
    CHECK_THAT(editor.tempoMap().secondsAt(chart.notes[2].position), WithinAbs(4.0, 1e-9));
    REQUIRE(chart.fret_hand_positions.size() == 2);
    CHECK(chart.fret_hand_positions[1].position == beatAt(5, 1));
}

// Undo and redo restore the whole song field-equal, as every whole-song entry does.
TEST_CASE("Scale Tempo's undo and redo restore the whole song", "[core][scale-tempo]")
{
    ScaleEditor editor;
    const common::core::SongTimeline original = editor.timeline();

    editor.scale("3/2");
    const common::core::SongTimeline scaled = editor.timeline();
    REQUIRE_FALSE(scaled == original);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == original);
    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == scaled);
}

// Text that is not a positive factor, a playing transport, and a factor of one all leave the song
// and the history untouched.
TEST_CASE("Scale Tempo refuses bad text and records nothing for no change", "[core][scale-tempo]")
{
    ScaleEditor editor;
    const common::core::SongTimeline original = editor.timeline();

    for (const char* const refused : {"0", "-2", "1.5", "two", ""})
    {
        INFO(refused);
        editor.scale(refused);
        CHECK(editor.timeline() == original);
        CHECK(editor.undoEntryCount() == 0);
    }

    editor.transport.current_state.playing = true;
    editor.scale("2");
    CHECK(editor.timeline() == original);
    editor.transport.current_state.playing = false;

    editor.scale("1");
    CHECK(editor.timeline() == original);
    CHECK(editor.undoEntryCount() == 0);
}

// Scale Tempo holds every time, so the transport is left exactly where it stood — no seek, even for
// a cursor between two ticks, whose place rounds onto the new lattice by less than the cursor's
// trust tolerance — and the commit is no keyboard move, so the window has nothing to follow. It
// publishes once, after the commit has settled. The selection is released: every address it held
// now names other music, even the anchor at 1:1, which still stands at 1:1.
TEST_CASE("Scale Tempo leaves the cursor's time alone and selects nothing", "[core][scale-tempo]")
{
    for (const char* const factor : {"2", "1/2", "3/2"})
    {
        INFO(factor);
        ScaleEditor editor;
        editor.seek(5.0003);
        editor.controller.onTempoAnchorSelected(beatAt(1, 1));
        REQUIRE(editor.state().selected_tempo_anchor == std::optional{beatAt(1, 1)});
        const int seeks = editor.transport.seek_call_count;
        const int publishes = editor.view.set_state_call_count;
        const std::uint64_t moves = editor.state().keyboard_move_count;

        editor.scale(factor);
        REQUIRE(editor.undoEntryCount() == 1);
        CHECK_FALSE(editor.state().selected_tempo_anchor.has_value());
        CHECK(editor.transport.seek_call_count == seeks);
        CHECK_THAT(editor.transport.position().seconds, WithinULP(5.0003, 0));
        CHECK(editor.view.set_state_call_count == publishes + 1);
        CHECK(editor.state().keyboard_move_count == moves);
    }
}

// An armed caret standing away from the transport dissolves onto its own time: one seek, to the
// music it stood on, and never to what its old address names under the new map.
TEST_CASE("Scale Tempo dissolves an armed caret onto its own time", "[core][scale-tempo]")
{
    ScaleEditor editor;
    editor.seek(13.25);
    editor.armCaretAtCursor();
    // A caret step never seeks, so the caret now stands a grid step from the transport.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ChartCaretViewState* const caret = caretOrNull(editor.state().chart_edit);
    REQUIRE(caret != nullptr);
    const double caret_seconds = caret != nullptr ? caret->seconds : 0.0;
    REQUIRE(caret_seconds - editor.transport.position().seconds > 0.1);
    const int seeks = editor.transport.seek_call_count;
    const int publishes = editor.view.set_state_call_count;
    const std::uint64_t moves = editor.state().keyboard_move_count;

    editor.scale("1/2");
    CHECK(caretOrNull(editor.state().chart_edit) == nullptr);
    CHECK(editor.transport.seek_call_count == seeks + 1);
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(caret_seconds, 1e-9));
    CHECK(editor.view.set_state_call_count == publishes + 1);
    CHECK(editor.state().keyboard_move_count == moves);
}

// The cursor is carried as one more slice, so the marker verbs author on the note it stood on even
// where the terminal re-seats. Nine measures of 4/4 at about 240 BPM end at 9004 ms; halved they
// end on 5:3 and re-seat on 6:1, whose 10004.4 ms rounds to 10004, re-timing the last span by more
// than half a tick near its end. The note at 9:4 lands on 5:2+1/2, about 0.4 ms from where it
// sounded: inside the cursor's trust tolerance, so the transport stays and the cursor's column is
// the note's.
TEST_CASE("Scale Tempo carries the cursor onto its note in a re-seated span", "[core][scale-tempo]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("10:1", 9004)});
    REQUIRE(song.arrangements.front().chart.has_value());
    if (song.arrangements.front().chart.has_value())
    {
        song.arrangements.front().chart->notes.push_back(makeTestNote(beatAt(9, 4), 1, 5));
    }
    SongEditorHarness editor;
    editor.open(std::move(song));
    const double sounded = editor.tempoMap().secondsAt(beatAt(9, 4));
    editor.seek(sounded);
    const int seeks = editor.transport.seek_call_count;

    editor.controller.onScaleTempoRequested("1/2");
    REQUIRE(editor.tempoMap().anchors().back() == anchor("6:1", 10004));
    const GridPosition landed{.measure = 5, .beat = 2, .offset = common::core::Fraction{1, 2}};
    const common::core::Chart* const chart = chartOrNull(editor.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 4);
        CHECK(chart->notes.back().position == landed);
    }
    CHECK(editor.transport.seek_call_count == seeks);
    CHECK_THAT(editor.transport.position().seconds, WithinULP(sounded, 0));
    CHECK(
        editor.state().tempo_chord_target ==
        TempoChordTarget{InsertTempoAnchorTarget{.position = landed}});
}

} // namespace rock_hero::editor::core
