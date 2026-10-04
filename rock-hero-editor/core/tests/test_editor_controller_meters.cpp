#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using common::core::GridPosition;
using common::core::SongSection;
using common::core::TimeSignature;
using common::core::TimeSignatureChange;
using common::core::testing::anchor;
using common::core::testing::commonTimeMap;

[[nodiscard]] GridPosition beatAt(int measure, int beat)
{
    return GridPosition{.measure = measure, .beat = beat, .offset = {}};
}

[[nodiscard]] TimeSignatureChange meterAt(int measure, int numerator, int denominator)
{
    return TimeSignatureChange{
        .measure = measure,
        .meter = TimeSignature{.numerator = numerator, .denominator = denominator},
    };
}

// A 4/4 map at 120 BPM whose terminal is 9:1 at 16 s, so measure N's downbeat sits at (N - 1) * 2
// seconds; the audio runs to 20 s. Two arrangements, so a meter edit has a non-current one to
// re-address too: the lead holds the shared test chart and two hand placements, the bass a note
// whose ring crosses measure 3 and one off the beat. Two sections give the ruler content to keep.
[[nodiscard]] common::core::Song makeMeterSong()
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(20.0),
        std::string{g_tone_document_ref});
    song.tempo_map = commonTimeMap({anchor("1:1", 0), anchor("9:1", 16000)});
    song.sections = {
        SongSection{.position = beatAt(2, 1), .name = "Verse"},
        SongSection{.position = beatAt(6, 1), .name = "Chorus"},
    };
    common::core::Chart lead = makeTestChart();
    lead.fret_hand_positions = {
        common::core::FretHandPosition{.position = beatAt(1, 1), .fret = 5, .end_fret = {}},
        common::core::FretHandPosition{.position = beatAt(3, 1), .fret = 7, .end_fret = {}},
    };
    song.arrangements.front().chart = std::move(lead);

    common::core::Arrangement bass = song.arrangements.front();
    bass.id = "bass";
    common::core::Chart bass_chart = makeTestChart();
    bass_chart.notes = {
        makeTestNote(beatAt(2, 4), 1, 3, common::core::Fraction{2}),
        makeTestNote(
            GridPosition{.measure = 5, .beat = 3, .offset = common::core::Fraction{1, 2}}, 2, 5),
    };
    bass.chart = std::move(bass_chart);
    song.arrangements.push_back(std::move(bass));
    return song;
}

// Owns the fakes and a controller with the meter song loaded and its rig ready.
struct MeterEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    MeterEditor()
        : MeterEditor{makeMeterSong()}
    {}

    explicit MeterEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
                  .save_function = project_services.saveFunction(),
              },
          }
    {
        audio.next_prepared_audio_duration = common::core::TimeDuration{20.0};
        audio.next_set_active_arrangement_result = true;
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
        REQUIRE(controller.session().arrangements().size() == 2);
    }

    [[nodiscard]] const EditorViewState& state() const
    {
        REQUIRE(view.last_state.has_value());
        if (!view.last_state.has_value())
        {
            throw std::logic_error("editor pushed no view state");
        }
        return *view.last_state;
    }

    [[nodiscard]] const common::core::TempoMap& tempoMap() const
    {
        return controller.session().song().tempo_map;
    }

    [[nodiscard]] std::vector<TimeSignatureChange> meters() const
    {
        return tempoMap().timeSignatures();
    }

    [[nodiscard]] common::core::SongTimeline timeline() const
    {
        return common::core::makeSongTimeline(controller.session().song());
    }

    void seek(double seconds)
    {
        controller.onTimelineSeekRequested(common::core::TimePosition{seconds});
    }

    [[nodiscard]] TimeSignatureChordTarget chord() const
    {
        return state().time_signature_chord_target;
    }

    void set(int measure, const std::string& text)
    {
        controller.onTimeSignatureSetRequested(measure, text);
    }

    void press(ChartStepDirection direction, int presses = 1)
    {
        for (int pressed = 0; pressed < presses; ++pressed)
        {
            controller.onSelectionMoveRequested(direction);
        }
    }

    [[nodiscard]] std::size_t undoEntryCount() const
    {
        return state().undo_history.labels.size();
    }

    // The label Ctrl+Z would undo next, or empty when nothing is undoable.
    [[nodiscard]] std::string undoTopLabel() const
    {
        const UndoHistoryState& history = state().undo_history;
        return history.position == 0 ? std::string{} : history.labels.at(history.position - 1);
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
            seconds.push_back(
                tempo_map.secondsAt(common::core::sustainEndPosition(tempo_map, note)));
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
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(3, 3, 4)});
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
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(3, 6, 8)});
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
    editor.press(ChartStepDirection::Right);
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4)});
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
    CHECK(editor.undoEntryCount() == 0);

    editor.set(3, "3/4");
    REQUIRE(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(3, 3, 4)});
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4)});
    CHECK(editor.undoTopLabel() == "Delete Time Signature");
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});

    editor.controller.onUndoRequested();
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(3, 3, 4)});
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

    editor.press(ChartStepDirection::Right, 2);
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(5, 2, 4)});
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

    editor.press(ChartStepDirection::Left);
    REQUIRE(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(6, 3, 4)});
    editor.press(ChartStepDirection::Right);
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
    editor.press(ChartStepDirection::Left);
    editor.controller.onTimeSignatureSelected(3);
    editor.press(ChartStepDirection::Right);
    CHECK(editor.timeline() == two);
    CHECK(editor.undoEntryCount() == entries);

    // 2/4 from measure 7 (whole note 6) steps once to measure 8 (whole note 7); one more step would
    // land on the terminal's whole note 8.
    MeterEditor ending;
    ending.set(7, "2/4");
    ending.press(ChartStepDirection::Right);
    REQUIRE(ending.meters() == std::vector{meterAt(1, 4, 4), meterAt(8, 2, 4)});
    const common::core::SongTimeline stepped = ending.timeline();
    ending.press(ChartStepDirection::Right);
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

    editor.press(ChartStepDirection::Left, 2);
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
    CHECK(editor.meters() == std::vector{meterAt(1, 4, 4), meterAt(3, 6, 8)});
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
        // The bass ring from 2:4 ran two quarter beats to 3:2; under 3/4 it still ends there.
        REQUIRE_FALSE(bass->notes.empty());
        CHECK(bass->notes.front().sustain == common::core::Fraction{2});
    }
}

// The cursor keeps its TIME, armed or passive: the caret armed at 6:1 (10 s) dissolves into the
// selected meter, and the cursor stands where it stood, which under 3/4 from measure 3 is 7:1.
TEST_CASE("A meter edit keeps the cursor and the caret on their music", "[core][meters]")
{
    MeterEditor editor;
    editor.seek(10.0);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    REQUIRE(caretOrNull(editor.state().chart_edit) != nullptr);

    editor.set(3, "3/4");
    CHECK(caretOrNull(editor.state().chart_edit) == nullptr);
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(10.0, 1e-9));
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

} // namespace rock_hero::editor::core
