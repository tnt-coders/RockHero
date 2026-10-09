#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinULP;
using common::core::GridPosition;
using common::core::SongSection;
using common::core::TimeSignatureChange;
using common::core::testing::beatAt;
using common::core::testing::metersAt;

// The eight-measure song with a second arrangement, so a meter edit has a non-current one to
// re-address too: the bass holds a note whose ring crosses measure 3 and one off the beat.
[[nodiscard]] common::core::Song makeMeterSong()
{
    common::core::Song song = makeEightMeasureSong();
    common::core::Arrangement bass = song.arrangements.front();
    bass.id = "bass";
    common::core::Chart bass_chart = makeTestChart();
    bass_chart.notes = {
        makeTestNote(beatAt(2, 4), 1, 3, common::core::Fraction{1, 2}),
        makeTestNote(
            GridPosition{.measure = 5, .beat = 3, .offset = common::core::Fraction{1, 2}}, 2, 5),
    };
    bass.chart = std::move(bass_chart);
    song.arrangements.push_back(std::move(bass));
    return song;
}

// A controller with the meter song loaded and its rig ready.
struct MeterEditor : SongEditorHarness
{
    MeterEditor()
    {
        open(makeMeterSong());
        REQUIRE(controller.session().arrangements().size() == 2);
    }

    [[nodiscard]] std::vector<TimeSignatureChange> meters() const
    {
        return tempoMap().timeSignatures();
    }

    [[nodiscard]] TimeSignatureChordTarget chord() const
    {
        return state().time_signature_chord_target;
    }

    void set(int measure, const std::string& text)
    {
        controller.onTimeSignatureSetRequested(measure, text);
    }
};

// The time of every object the song stores on the grid, in a fixed order: sections, then per
// arrangement its note onsets, ring ends and hand placements. What a meter edit must not move.
[[nodiscard]] std::vector<double> resolvedTimes(const common::core::Song& song)
{
    const common::core::TempoMap& tempo_map = song.tempo_map;
    std::vector<double> seconds;
    for (const SongSection& section : song.sections)
    {
        seconds.push_back(tempo_map.secondsAt(section.position));
    }
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        if (!arrangement.chart.has_value())
        {
            continue;
        }
        const common::core::Chart& chart = *arrangement.chart;
        for (const common::core::ChartNote& note : chart.notes)
        {
            seconds.push_back(tempo_map.secondsAt(note.position));
            seconds.push_back(tempo_map.secondsAt(
                common::core::advanceGridPositionByWholeNotes(
                    tempo_map, note.position, note.sustain)));
        }
        for (const common::core::FretHandPosition& placement : chart.fret_hand_positions)
        {
            seconds.push_back(tempo_map.secondsAt(placement.position));
        }
    }
    return seconds;
}

} // namespace

// The chord publishes its whole decision as the verb: an insert on the downbeat of the measure the
// cursor is in, opened on the meter governing it, and — once that insert has landed and kept the
// cursor in place — the restate of the meter standing there, so a second press opens the prompt.
TEST_CASE("The meter chord inserts at the cursor's measure, then restates", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(5.0); // inside measure 3

    const TimeSignatureChordTarget first = editor.chord();
    const auto* const insert = std::get_if<InsertTimeSignatureTarget>(&first);
    REQUIRE(insert != nullptr);
    CHECK(insert->measure == 3);
    CHECK(insert->meter_text == "4/4");

    editor.set(3, "3/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{3});
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Add Time Signature");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(5.0, 1e-9));

    const SetTimeSignatureTarget expected{.measure = 3, .meter_text = "3/4"};
    const TimeSignatureChordTarget second = editor.chord();
    const auto* const restate = std::get_if<SetTimeSignatureTarget>(&second);
    REQUIRE(restate != nullptr);
    CHECK(*restate == expected);
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTimeSignatureTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == expected);

    editor.set(3, " 6 / 8 ");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 6, 8}}));
    CHECK(editor.undoEntryCount() == 2);
    CHECK(editor.undoTopLabel() == "Set Time Signature");
}

// Inert exactly where the commit would refuse: on the terminal's measure, while the transport
// plays, and with no song.
TEST_CASE("The meter chord is nothing where no meter can start", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(16.0);
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));

    editor.transport.current_state.playing = true;
    editor.seek(4.0);
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));

    editor.transport.current_state.playing = false;
    editor.controller.onCloseRequested();
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));
}

// The prompt's text is parsed in the core: anything that is not two positive whole numbers around
// a slash is refused before any edit, and a meter the tempo-map rules refuse (a beat no power of
// two) is refused by the commit. Neither records anything; inserting the meter already governing
// a measure is no change at all, since the commit drops a meter restating its predecessor.
TEST_CASE("A meter edit refuses bad text and records nothing for no change", "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline before = editor.timeline();
    for (const char* const text : {"7", "x/8", "0/4", "4/0", "3/4/4", "", "4/3"})
    {
        INFO(text);
        editor.set(3, text);
        CHECK(editor.timeline() == before);
        CHECK(editor.undoEntryCount() == 0);
    }
    editor.set(3, "4/4");
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == 0);
}

// The measure-1 meter cannot be deleted or moved. Another one is deleted whole, the meter before it
// governing its measures again, and the row's previous chip is selected as every marker delete
// leaves it.
TEST_CASE("Delete removes a meter and refuses the first", "[core][meters]")
{
    MeterEditor editor;
    editor.controller.onTimeSignatureSelected(1);
    editor.controller.onSelectionDeleteRequested();
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
    CHECK(editor.undoEntryCount() == 0);

    editor.set(3, "3/4");
    REQUIRE(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}}));
    CHECK(editor.undoTopLabel() == "Delete Time Signature");
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});

    editor.controller.onUndoRequested();
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
}

// Alt+arrows step a meter one measure, re-addressing everything after it, and a held run is ONE
// undo entry. The tempo lock guards anchor times only, and a meter edit moves no anchor in time,
// so the lock every open turns on blocks none of this.
TEST_CASE("A meter moves a measure per press and a held run is one entry", "[core][meters]")
{
    MeterEditor editor;
    REQUIRE(editor.state().tempo_lock);
    editor.set(3, "2/4");
    const common::core::SongTimeline inserted = editor.timeline();

    editor.pressAlt(ChartStepDirection::Right, 2);
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"5:1", 2, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});
    CHECK(editor.undoEntryCount() == 2);
    CHECK(editor.undoTopLabel() == "Move Time Signature");

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == inserted);
}

// Each press of a run is built from the run's origin, because a re-address that re-seated the
// terminal is not undone by the step back: a run walked back lands the origin exactly and leaves no
// entry.
TEST_CASE("A meter run walked back restores the song exactly", "[core][meters]")
{
    MeterEditor editor;
    // 3/4 from measure 7 leaves the terminal's whole note 8 inside a measure, so it re-seats.
    editor.set(7, "3/4");
    REQUIRE(common::core::terminalGridPosition(editor.tempoMap()) == beatAt(10, 1));
    const common::core::SongTimeline inserted = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Left);
    REQUIRE(editor.meters() == metersAt({{"1:1", 4, 4}, {"6:1", 3, 4}}));
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.timeline() == inserted);
    CHECK(editor.undoEntryCount() == entries);
}

// A step onto or past a neighbouring meter, or to or past the terminal, is refused and records
// nothing, like a step of the measure-1 meter.
TEST_CASE("A meter move refuses neighbours and the song's end", "[core][meters]")
{
    MeterEditor editor;
    editor.set(3, "2/4");
    editor.set(4, "3/4");
    const common::core::SongTimeline two = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTimeSignatureSelected(4);
    editor.pressAlt(ChartStepDirection::Left);
    editor.controller.onTimeSignatureSelected(3);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.timeline() == two);
    CHECK(editor.undoEntryCount() == entries);

    // 2/4 from measure 7 (whole note 6) steps once to measure 8 (whole note 7); one more step would
    // land on the terminal's whole note 8.
    MeterEditor ending;
    ending.set(7, "2/4");
    ending.pressAlt(ChartStepDirection::Right);
    REQUIRE(ending.meters() == metersAt({{"1:1", 4, 4}, {"8:1", 2, 4}}));
    const common::core::SongTimeline stepped = ending.timeline();
    ending.pressAlt(ChartStepDirection::Right);
    CHECK(ending.timeline() == stepped);
    CHECK(ending.undoEntryCount() == 2);
}

// The meter edit's undo and redo restore the WHOLE song field-equal — the map, the sections and
// both arrangements — and each re-mirrors the host tempo, as every tempo-map change does.
TEST_CASE("A meter edit's undo and redo restore the whole song", "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline original = editor.timeline();
    const int loaded_mirrors = editor.audio.mirror_tempo_map_call_count;

    editor.set(3, "3/4");
    const common::core::SongTimeline edited = editor.timeline();
    REQUIRE_FALSE(edited == original);
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == original);
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 2);
    REQUIRE(editor.audio.last_mirrored_tempo_map.has_value());
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == original.tempo_map);
    }

    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == edited);
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 3);
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == edited.tempo_map);
    }
    // The undo's focus selects the meter it brought back.
    CHECK(editor.state().selected_time_signature_measure == std::optional{3});
}

// A landed meter verb asks the view to flash the measure numbers it renumbered (decision 5): from
// the first measure whose downbeat moved. 2/4 from measure 5 shortens measure 5, so measure 6 is
// the first renumbered; each step of a move, and the delete, renumber from the measure after the
// shorter one. A refusal, a no-change and an undo flash nothing.
TEST_CASE("A landed meter verb flashes the renumbered measures", "[core][meters]")
{
    MeterEditor editor;
    editor.set(5, "2/4");
    CHECK(editor.view.renumber_flashes == std::vector{6});

    editor.pressAlt(ChartStepDirection::Left, 2);
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 4});

    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 4, 4});

    editor.set(4, "x/8");
    editor.set(4, "4/4");
    editor.controller.onTimeSignatureSelected(1);
    editor.controller.onSelectionDeleteRequested();
    editor.controller.onUndoRequested();
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 4, 4});
}

// A restate that keeps every measure's length moves no downbeat, so it lands and renumbers nothing:
// 3/4 restated as 6/8 is three quarter notes a measure either way.
TEST_CASE("A meter restate that moves no downbeat flashes nothing", "[core][meters]")
{
    MeterEditor editor;
    editor.set(3, "3/4");
    REQUIRE(editor.view.renumber_flashes == std::vector{4});

    editor.set(3, "6/8");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 6, 8}}));
    CHECK(editor.undoTopLabel() == "Set Time Signature");
    CHECK(editor.view.renumber_flashes == std::vector{4});
}

// A meter edit keeps every object's time in both arrangements, the non-current one included, while
// the measures after it renumber: 3/4 from measure 3 turns the Chorus at 6:1 into 7:1.
TEST_CASE("A meter edit moves no resolved time in either arrangement", "[core][meters]")
{
    MeterEditor editor;
    const std::vector<double> before = resolvedTimes(editor.controller.session().song());

    editor.set(3, "3/4");
    const common::core::Song& song = editor.controller.session().song();
    const std::vector<double> after = resolvedTimes(song);
    REQUIRE(after.size() == before.size());
    for (std::size_t index = 0; index < before.size(); ++index)
    {
        CHECK_THAT(after[index], WithinAbs(before[index], 1e-9));
    }
    REQUIRE(song.sections.size() == 2);
    CHECK(song.sections[1].position == beatAt(7, 1));
    REQUIRE(song.arrangements.size() == 2);
    const std::optional<common::core::Chart>& bass = song.arrangements[1].chart;
    REQUIRE(bass.has_value());
    if (bass.has_value())
    {
        // The bass ring from 2:4 ran a half note to 3:2; under 3/4 it still ends there, its value
        // untouched.
        REQUIRE_FALSE(bass->notes.empty());
        CHECK(bass->notes.front().sustain == common::core::Fraction{1, 2});
    }
}

// The cursor keeps its TIME, armed or passive: the caret armed at 6:1 (10 s) dissolves into the
// selected meter, and the cursor stands where it stood, which under 3/4 from measure 3 is 7:1. The
// transport already stands there, so nothing seeks, and the commit publishes once and is no
// keyboard move.
TEST_CASE("A meter edit keeps the cursor and the caret on their music", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(10.0);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    REQUIRE(caretOrNull(editor.state().chart_edit) != nullptr);
    const int seeks = editor.transport.seek_call_count;
    const int publishes = editor.view.set_state_call_count;
    const std::uint64_t moves = editor.state().keyboard_move_count;

    editor.set(3, "3/4");
    CHECK(caretOrNull(editor.state().chart_edit) == nullptr);
    CHECK(editor.transport.seek_call_count == seeks);
    CHECK_THAT(editor.transport.position().seconds, WithinULP(10.0, 0));
    CHECK(editor.view.set_state_call_count == publishes + 1);
    CHECK(editor.state().keyboard_move_count == moves);
    const std::optional<KeyboardPositionViewState>& position = editor.state().keyboard_position;
    REQUIRE(position.has_value());
    if (position.has_value())
    {
        CHECK_THAT(position->seconds, WithinAbs(10.0, 1e-9));
        CHECK_THAT(position->measure_start_seconds, WithinAbs(10.0, 1e-9));
    }

    // The passive cursor between beats keeps its time just the same.
    editor.seek(13.25);
    editor.set(8, "6/8");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(13.25, 1e-9));
}

// The cursor is carried as one more slice, so the marker verbs author on the note it stood on even
// where the terminal re-seats. The eight measures of 4/4 end at 16016 ms; restated as 3/4 they end
// mid-measure and re-seat on 12:1, about 16516.5 ms, stored as 16516, re-timing the last span by
// about half a millisecond, more than half a tick. The note at 8:4 lands on 11:2: inside the
// cursor's trust tolerance, so the transport stays and the cursor's column is the note's.
TEST_CASE("A meter edit carries the cursor onto its note in a re-seated span", "[core][meters]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap(
        {common::core::testing::anchor("1:1", 0), common::core::testing::anchor("9:1", 16016)});
    REQUIRE(song.arrangements.front().chart.has_value());
    if (song.arrangements.front().chart.has_value())
    {
        song.arrangements.front().chart->notes.push_back(makeTestNote(beatAt(8, 4), 1, 5));
    }
    SongEditorHarness editor;
    editor.open(std::move(song));
    const double sounded = editor.tempoMap().secondsAt(beatAt(8, 4));
    editor.seek(sounded);
    const int seeks = editor.transport.seek_call_count;

    editor.controller.onTimeSignatureSetRequested(1, "3/4");
    REQUIRE(editor.tempoMap().anchors().back() == common::core::testing::anchor("12:1", 16516));
    const common::core::Chart* const chart = chartOrNull(editor.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 4);
        CHECK(chart->notes.back().position == beatAt(11, 2));
    }
    CHECK(editor.transport.seek_call_count == seeks);
    CHECK_THAT(editor.transport.position().seconds, WithinULP(sounded, 0));
    CHECK(
        editor.state().tempo_chord_target ==
        TempoChordTarget{InsertTempoAnchorTarget{.position = beatAt(11, 2)}});
}

} // namespace rock_hero::editor::core
