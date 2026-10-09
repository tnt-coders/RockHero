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
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
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
using common::core::testing::downbeat;
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
        : MeterEditor(makeMeterSong())
    {}

    explicit MeterEditor(common::core::Song song)
    {
        open(std::move(song));
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

    // Answers the meter prompt for a position: a reign's first downbeat to state it, else the
    // beat to insert at.
    void set(const GridPosition& position, const std::string& text)
    {
        controller.onTimeSignatureSetRequested(position, text);
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

// The chord publishes its whole decision as the verb: an insert at the BEAT the cursor is in,
// opened on the meter governing it, and — once that insert has landed and kept the cursor in
// place — the restate of the reign starting there, so a second press opens the prompt. The cursor
// on 6:3 under 4/4 typing 7/8 puts the meter at whole note 5+1/2: measure 6 is a derived 2/4, the
// 7/8 governs from measure 7, and the cursor, keeping its time, now stands on that downbeat.
TEST_CASE("The meter chord inserts at the cursor's beat, then restates", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(11.0); // 6:3

    const TimeSignatureChordTarget first = editor.chord();
    const auto* const insert = std::get_if<InsertTimeSignatureTarget>(&first);
    REQUIRE(insert != nullptr);
    CHECK(insert->position == beatAt(6, 3));
    CHECK(insert->meter_text == "4/4");

    editor.set(beatAt(6, 3), "7/8");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"6:3", 7, 8}}));
    CHECK(editor.meters().back().place == common::core::Fraction{11, 2});
    CHECK(
        editor.tempoMap().timeSignatureAt(6) == common::core::MeterReign{
                                                    .first_measure = 6,
                                                    .place = common::core::Fraction{5},
                                                    .meter = {.numerator = 2, .denominator = 4},
                                                    .derived = true,
                                                });
    CHECK(editor.tempoMap().timeSignatureAt(7).first_measure == 7);
    CHECK_FALSE(editor.tempoMap().timeSignatureAt(7).derived);
    CHECK(editor.state().selected_time_signature_measure == std::optional{7});
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Add Time Signature");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(11.0, 1e-9));

    const SetTimeSignatureTarget expected{.position = downbeat(7), .meter_text = "7/8"};
    const TimeSignatureChordTarget second = editor.chord();
    const auto* const restate = std::get_if<SetTimeSignatureTarget>(&second);
    REQUIRE(restate != nullptr);
    CHECK(*restate == expected);
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTimeSignatureTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == expected);

    editor.set(downbeat(7), " 6 / 8 ");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"6:3", 6, 8}}));
    CHECK(editor.undoEntryCount() == 2);
    CHECK(editor.undoTopLabel() == "Set Time Signature");
}

// A cursor between beats reads the beat it is IN, never the nearest: a quarter of a beat past 6:3
// still inserts at 6:3.
TEST_CASE("The meter chord reads the beat the cursor is in", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(11.125);

    const TimeSignatureChordTarget chord = editor.chord();
    const auto* const insert = std::get_if<InsertTimeSignatureTarget>(&chord);
    REQUIRE(insert != nullptr);
    CHECK(insert->position == beatAt(6, 3));
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
// two) is refused by the commit. Neither records anything.
TEST_CASE("A meter edit refuses bad text and records nothing", "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline before = editor.timeline();
    for (const char* const text : {"7", "x/8", "0/4", "4/0", "3/4/4", "", "4/3"})
    {
        INFO(text);
        editor.set(downbeat(3), text);
        CHECK(editor.timeline() == before);
        CHECK(editor.undoEntryCount() == 0);
    }
}

// Typing the prevailing meter on one of its own downbeats changes no measure, so the history holds
// nothing — but the chip SHOWS, selected, so the charter can retype it: the chord that finds it
// is the restate, and the retyped meter is one real change whose undo lands the song as it was,
// never the chip.
TEST_CASE(
    "The meter chord restating the prevailing meter shows a chip it records nothing for",
    "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline original = editor.timeline();
    editor.seek(4.0); // 3:1

    editor.set(downbeat(3), "4/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 4, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{3});
    CHECK(editor.undoEntryCount() == 0);
    CHECK(editor.view.renumber_flashes.empty());
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(4.0, 1e-9));

    const TimeSignatureChordTarget chord = editor.chord();
    const auto* const restate = std::get_if<SetTimeSignatureTarget>(&chord);
    REQUIRE(restate != nullptr);
    CHECK(*restate == SetTimeSignatureTarget{.position = downbeat(3), .meter_text = "4/4"});

    editor.set(downbeat(3), "3/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Set Time Signature");
    CHECK(editor.view.renumber_flashes == std::vector{4});

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == original);
}

// A chip restating its reign is authoring state the editor holds only while it is selected: the
// selection leaving it — onto another chip, or cleared by a seek — dissolves it with no entry,
// and nothing moves in time, since dropping it joins two reigns of one meter at a barline.
TEST_CASE("A redundant meter chip dissolves when the selection leaves it", "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline original = editor.timeline();
    const std::vector<double> times = resolvedTimes(editor.controller.session().song());

    editor.set(downbeat(3), "4/4");
    REQUIRE(editor.meters().size() == 2);
    editor.controller.onTimeSignatureSelected(1);
    CHECK(editor.timeline() == original);
    CHECK(editor.undoEntryCount() == 0);
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});

    editor.set(downbeat(5), "4/4");
    REQUIRE(editor.meters().size() == 2);
    editor.seek(2.0);
    CHECK(editor.timeline() == original);
    CHECK(editor.undoEntryCount() == 0);

    const std::vector<double> settled = resolvedTimes(editor.controller.session().song());
    REQUIRE(settled.size() == times.size());
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        CHECK_THAT(settled[index], WithinAbs(times[index], 1e-9));
    }
}

// Alt+arrows can walk a repeat across its predecessor's downbeats without the chip vanishing
// under the key: 4/4 at 3:3 cuts measure 3, and two beat steps put it on measure 4's own downbeat,
// where it restates its reign. The run's entry holds the WRITTEN form — the chip gone — so a redo
// lands the normal form; the chip itself stays while selected and dissolves on leaving only while
// it still restates.
TEST_CASE(
    "A moved meter restating its reign stays while selected and dissolves on leaving",
    "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline original = editor.timeline();
    editor.set(beatAt(3, 3), "4/4");
    REQUIRE(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:3", 4, 4}}));
    const common::core::SongTimeline inserted = editor.timeline();

    editor.pressAlt(ChartStepDirection::Right, 2);
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"4:1", 4, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{4});
    CHECK(editor.undoEntryCount() == 2);
    CHECK(editor.undoTopLabel() == "Move Time Signature");

    SECTION("leaving the downbeat dissolves it, and undo and redo never bring it back")
    {
        editor.controller.onTimeSignatureSelected(1);
        CHECK(editor.timeline() == original);
        CHECK(editor.undoEntryCount() == 2);

        editor.controller.onUndoRequested();
        CHECK(editor.timeline() == inserted);
        editor.controller.onRedoRequested();
        CHECK(editor.timeline() == original);
    }
    SECTION("one more step cuts the next bar, and the chip stays when the selection leaves")
    {
        editor.pressAlt(ChartStepDirection::Right);
        CHECK(editor.undoEntryCount() == 2);
        editor.controller.onTimeSignatureSelected(1);
        CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"4:2", 4, 4}}));
        CHECK(editor.undoEntryCount() == 2);
    }
    SECTION("the run walked back lands where it began")
    {
        editor.pressAlt(ChartStepDirection::Left, 2);
        CHECK(editor.timeline() == inserted);
        CHECK(editor.undoEntryCount() == 1);
    }
}

// The history describes the written song, so a chip it never held goes before an undo replays:
// Ctrl+Z over a focused redundant chip undoes the edit before it and leaves no chip behind.
TEST_CASE("Undo never brings back a redundant meter chip", "[core][meters]")
{
    MeterEditor editor;
    const common::core::SongTimeline original = editor.timeline();
    editor.set(downbeat(3), "3/4");
    const common::core::SongTimeline edited = editor.timeline();

    editor.set(downbeat(2), "4/4");
    REQUIRE(editor.meters().size() == 3);
    REQUIRE(editor.undoEntryCount() == 1);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == original);
    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == edited);
}

// A save hands the writer the song as it stands, the focused chip included, and the writer sheds
// it (the meter-edits suite reads the written file); the chip itself survives the save in memory,
// still selected.
TEST_CASE("A save while a redundant meter chip is focused keeps it in memory", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "4/4");
    REQUIRE(editor.meters().size() == 2);

    editor.controller.onSaveRequested();
    REQUIRE(editor.project_services.save_call_count == 1);
    REQUIRE(editor.project_services.last_saved_song.has_value());
    if (editor.project_services.last_saved_song.has_value())
    {
        CHECK(editor.project_services.last_saved_song->tempo_map == editor.tempoMap());
    }
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 4, 4}}));
    CHECK(editor.state().selected_time_signature_measure == std::optional{3});
    CHECK(editor.undoEntryCount() == 0);
}

// A meter placed at a beat starts a bar there, so the prevailing meter repeated OFF its own
// downbeat grid restates nothing: 4/4 at 4:3 cuts measure 4 to a derived 2/4 and renumbers every
// bar after it, while every anchor, every span's tempo and every object keep their time, the
// anchors under the cut re-addressed (5:3 is 6:1). A repeat ON the grid, before the cut and after
// it alike, is a chip the history never holds: it lives while selected and dissolves when the
// selection leaves it, leaving the song as the cut left it.
TEST_CASE("A repeated meter off the downbeat grid cuts a bar and moves nothing", "[core][meters]")
{
    using common::core::BeatAnchor;
    using common::core::testing::anchor;
    common::core::Song song = makeMeterSong();
    song.tempo_map = common::core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("3:1", 4000),
        anchor("5:3", 7000),
        anchor("7:1", 10000),
        anchor("9:1", 16000),
    });
    MeterEditor editor{std::move(song)};
    const common::core::TempoMap before = editor.tempoMap();
    const std::vector<double> times = resolvedTimes(editor.controller.session().song());

    editor.set(beatAt(4, 3), "4/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"4:3", 4, 4}}));
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Add Time Signature");
    CHECK(editor.view.renumber_flashes == std::vector{5});
    const common::core::TempoMap& after = editor.tempoMap();
    CHECK(after.reigns().size() == 3);
    CHECK(
        after.timeSignatureAt(4) == common::core::MeterReign{
                                        .first_measure = 4,
                                        .place = common::core::Fraction{3},
                                        .meter = {.numerator = 2, .denominator = 4},
                                        .derived = true,
                                    });
    CHECK(after.timeSignatureAt(5).first_measure == 5);
    CHECK_FALSE(after.timeSignatureAt(5).derived);

    CHECK(
        after.anchors() == std::vector<BeatAnchor>{
                               anchor("1:1", 0),
                               anchor("3:1", 4000),
                               anchor("6:1", 7000),
                               anchor("7:3", 10000),
                               anchor("9:3", 16000),
                           });
    REQUIRE(after.anchors().size() == before.anchors().size());
    for (std::size_t index = 0; index < before.anchors().size(); ++index)
    {
        CHECK(
            after.wholeNotePositionAt(after.anchors()[index].position) ==
            before.wholeNotePositionAt(before.anchors()[index].position));
    }
    for (std::size_t span = 0; span + 1 < before.anchors().size(); ++span)
    {
        CHECK(tempoSpanBpmText(after, span) == tempoSpanBpmText(before, span));
    }
    const std::vector<double> landed = resolvedTimes(editor.controller.session().song());
    REQUIRE(landed.size() == times.size());
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        CHECK_THAT(landed[index], WithinAbs(times[index], 1e-9));
    }

    const common::core::SongTimeline cut = editor.timeline();
    editor.set(downbeat(3), "4/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 4, 4}, {"4:3", 4, 4}}));
    editor.set(downbeat(7), "4/4");
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"4:3", 4, 4}, {"7:1", 4, 4}}));
    editor.controller.onTimeSignatureSelected(1);
    CHECK(editor.timeline() == cut);
    CHECK(editor.undoEntryCount() == 1);
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

    editor.set(downbeat(3), "3/4");
    REQUIRE(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}}));
    CHECK(editor.undoTopLabel() == "Delete Time Signature");
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});

    editor.controller.onUndoRequested();
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}}));
}

// The row's objects are every reign start, the derived short bar's included: its ghost chip can be
// selected, and a stating verb on it AUTHORS it — a stored meter is inserted at its place with the
// typed meter, one undo entry, and the chip turns solid. 5/4 from measure 3 before the 4/4 at
// whole note 3+1/2 fills one bar and leaves a quarter: measure 4 is a derived 1/4.
TEST_CASE("A stating verb on a derived short bar authors it", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "3/4");
    editor.set(downbeat(5), "4/4");
    editor.set(downbeat(3), "5/4");
    REQUIRE(editor.tempoMap().timeSignatureAt(4).derived);
    REQUIRE(editor.tempoMap().reigns().size() == 4);
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTimeSignatureSelected(4);
    CHECK(editor.state().selected_time_signature_measure == std::optional{4});
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTimeSignatureTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == SetTimeSignatureTarget{.position = downbeat(4), .meter_text = "1/4"});

    editor.set(downbeat(4), "1/4");
    CHECK(
        editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 5, 4}, {"4:1", 1, 4}, {"5:1", 4, 4}}));
    CHECK_FALSE(editor.tempoMap().timeSignatureAt(4).derived);
    CHECK(editor.state().selected_time_signature_measure == std::optional{4});
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == "Set Time Signature");
}

// A derived short bar is never stored, so changing the meter that left it back makes it vanish by
// itself.
TEST_CASE("Changing a meter back makes the derived short bar vanish", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "3/4");
    editor.set(downbeat(5), "4/4");
    editor.set(downbeat(3), "5/4");
    REQUIRE(editor.tempoMap().reigns().size() == 4);

    editor.set(downbeat(3), "3/4");
    CHECK(editor.tempoMap().reigns().size() == 3);
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:1", 3, 4}, {"5:1", 4, 4}}));
}

// Delete and the move need a STORED meter, so on a derived start both refuse: the chip glows red
// and nothing is recorded, as on a derived fret-hand position.
TEST_CASE("Delete and the move refuse a derived short bar", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "3/4");
    editor.set(downbeat(5), "4/4");
    editor.set(downbeat(3), "5/4");
    REQUIRE(editor.tempoMap().timeSignatureAt(4).derived);
    const common::core::SongTimeline authored = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTimeSignatureSelected(4);
    REQUIRE(editor.state().selected_time_signature_measure == std::optional{4});
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.timeline() == authored);
    CHECK(editor.view.time_signature_refusal_flashes == std::vector{4});
    editor.pressAlt(ChartStepDirection::Right);
    editor.pressAlt(ChartStepDirection::Left);
    CHECK(editor.timeline() == authored);
    CHECK(editor.view.time_signature_refusal_flashes == std::vector{4, 4, 4});
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.state().selected_time_signature_measure == std::optional{4});
}

// Alt+arrows step a meter ONE BEAT of the meter before it, re-addressing everything after it, and a
// held run is ONE undo entry: 2/4 from measure 3 stepped twice stands two quarter notes later, at
// 3:3 as the 4/4 counted it, so measure 3 is a derived 2/4 and the 2/4 governs from measure 4. The
// tempo lock guards anchor times only, and a meter edit moves no anchor in time, so the lock every
// open turns on blocks none of this.
TEST_CASE("A meter moves a beat per press and a held run is one entry", "[core][meters]")
{
    MeterEditor editor;
    REQUIRE(editor.state().tempo_lock);
    editor.set(downbeat(3), "2/4");
    const common::core::SongTimeline inserted = editor.timeline();

    editor.pressAlt(ChartStepDirection::Right, 2);
    CHECK(editor.meters() == metersAt({{"1:1", 4, 4}, {"3:3", 2, 4}}));
    CHECK(editor.tempoMap().timeSignatureAt(3).derived);
    CHECK(editor.state().selected_time_signature_measure == std::optional{4});
    CHECK(editor.undoEntryCount() == 2);
    CHECK(editor.undoTopLabel() == "Move Time Signature");

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == inserted);
}

// A run walked back lands the origin exactly and leaves no entry: every press keeps every place,
// the terminal's included, so the step back is the step's exact inverse.
TEST_CASE("A meter run walked back restores the song exactly", "[core][meters]")
{
    MeterEditor editor;
    // 3/4 from measure 7 leaves the terminal's whole note 8 on 9:3, the song ending mid-measure.
    editor.set(downbeat(7), "3/4");
    REQUIRE(common::core::terminalGridPosition(editor.tempoMap()) == beatAt(9, 3));
    const common::core::SongTimeline inserted = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Left);
    REQUIRE(editor.meters() == metersAt({{"1:1", 4, 4}, {"6:4", 3, 4}}));
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.timeline() == inserted);
    CHECK(editor.undoEntryCount() == entries);
}

// A step that leaves a reign shorter than one of its measures is the tempo-map rules' refusal
// (ReignTooShort), and a step to or past the terminal is refused too; neither records anything,
// like a step of the measure-1 meter.
TEST_CASE("A meter move refuses a reign too short and the song's end", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "2/4");
    editor.set(downbeat(4), "3/4");
    const common::core::SongTimeline two = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    // One beat either way leaves the 2/4 a quarter note, less than one of its bars; each refusal
    // flashes the chip that was stepped.
    editor.controller.onTimeSignatureSelected(4);
    editor.pressAlt(ChartStepDirection::Left);
    editor.controller.onTimeSignatureSelected(3);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.timeline() == two);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.view.time_signature_refusal_flashes == std::vector{4, 3});

    // 2/4 from measure 8 (whole note 7) steps three quarter notes to 8:4; the fourth step would
    // land on the terminal's whole note 8. The last bar before the terminal is a cut, never a
    // short reign, so the 2/4 standing a quarter note before the end is legal.
    MeterEditor ending;
    ending.set(downbeat(8), "2/4");
    ending.pressAlt(ChartStepDirection::Right, 3);
    REQUIRE(ending.meters() == metersAt({{"1:1", 4, 4}, {"8:4", 2, 4}}));
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

    editor.set(downbeat(3), "3/4");
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
// the first renumbered; each beat step of the move cuts measure 4 short, moving measure 5's
// downbeat, and the delete moves it back. A refusal, a no-change and an undo flash nothing.
TEST_CASE("A landed meter verb flashes the renumbered measures", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(5), "2/4");
    CHECK(editor.view.renumber_flashes == std::vector{6});

    editor.pressAlt(ChartStepDirection::Left, 2);
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 5});

    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 5, 5});

    editor.set(downbeat(4), "x/8");
    editor.set(downbeat(4), "4/4");
    editor.controller.onTimeSignatureSelected(1);
    editor.controller.onSelectionDeleteRequested();
    editor.controller.onUndoRequested();
    CHECK(editor.view.renumber_flashes == std::vector{6, 5, 5, 5});
}

// A restate that keeps every measure's length moves no downbeat, so it lands and renumbers nothing:
// 3/4 restated as 6/8 is three quarter notes a measure either way.
TEST_CASE("A meter restate that moves no downbeat flashes nothing", "[core][meters]")
{
    MeterEditor editor;
    editor.set(downbeat(3), "3/4");
    REQUIRE(editor.view.renumber_flashes == std::vector{4});

    editor.set(downbeat(3), "6/8");
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

    editor.set(downbeat(3), "3/4");
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

    editor.set(downbeat(3), "3/4");
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
    editor.set(downbeat(8), "6/8");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(13.25, 1e-9));
}

} // namespace rock_hero::editor::core
