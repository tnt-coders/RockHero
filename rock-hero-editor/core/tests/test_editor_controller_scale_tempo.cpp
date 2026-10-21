#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
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
using common::core::SongTimeline;
using common::core::testing::anchor;
using common::core::testing::beatAt;

// The eight-measure song (4/4 at 120 BPM from 1:1 at 0 s to 9:1 at 16 s, audio to 20 s, sections
// at 2:1 and 6:1, notes at 2:1 and 3:1, hand positions at 1:1 and 3:1) under a map whose tempo
// changes at 3:1 and 5:1: two measures in 3 s, two in 5 s, then four at 120 BPM to 9:1 at 16 s.
[[nodiscard]] common::core::Song makeThreeSpanSong()
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("3:1", 3000),
        anchor("5:1", 8000),
        anchor("9:1", 16000),
    });
    return song;
}

// A controller with a song loaded, the Note Lock set as asked and the Tempo Lock off, which every
// open turns on and which refuses Scale Tempo. Opened with the eight-measure song unless given
// another.
struct ScaleEditor : SongEditorHarness
{
    // Opens the song, sets the Note Lock as asked, turns the Tempo Lock off and requires both.
    explicit ScaleEditor(const bool note_lock, common::core::Song song = makeEightMeasureSong())
    {
        open(std::move(song));
        if (note_lock)
        {
            controller.onNoteLockToggleRequested();
        }
        controller.onTempoLockToggleRequested();
        REQUIRE(state().note_lock == note_lock);
        REQUIRE_FALSE(state().tempo_lock);
    }

    // The loaded chart, failing the test when the arrangement has none.
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

    // The keyboard's time as last published, failing the test when the editor shows no keyboard.
    [[nodiscard]] double keyboardSeconds() const
    {
        const std::optional<KeyboardPositionViewState>& position = state().keyboard_position;
        REQUIRE(position.has_value());
        if (!position.has_value())
        {
            throw std::logic_error{"scale fixture without a keyboard position"};
        }
        return position->seconds;
    }

    void scale(const std::string& text)
    {
        controller.onScaleTempoRequested(text);
    }
};

// Every anchor of the timeline now against the one before: same positions, each time within a
// millisecond of the other's.
void checkAnchorsRestored(const SongTimeline& before, const SongTimeline& after)
{
    const std::vector<common::core::BeatAnchor>& was = before.tempo_map.anchors();
    const std::vector<common::core::BeatAnchor>& now = after.tempo_map.anchors();
    REQUIRE(now.size() == was.size());
    for (std::size_t index = 0; index < was.size(); ++index)
    {
        INFO("anchor " << index);
        CHECK(now[index].position == was[index].position);
        CHECK(std::abs(now[index].milliseconds - was[index].milliseconds) <= 1);
    }
}

} // namespace

// With the Note Lock on, x2 doubles every span's tempo and every address on the whole-note axis
// while every time holds: the terminal 9:1 lands on 17:1 still at 16 s, the sections and notes on
// the measures twice as far in, and the whole song is one entry, labelled as the form that ran.
TEST_CASE(
    "Locked Scale Tempo lands the whole song as one entry, every time held", "[core][scale-tempo]")
{
    ScaleEditor editor{true};

    editor.scale("2");
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Scale Tempo (notes keep their time)");
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
TEST_CASE("Locked Scale Tempo's undo and redo restore the whole song", "[core][scale-tempo]")
{
    ScaleEditor editor{true};
    const SongTimeline original = editor.timeline();

    editor.scale("3/2");
    const SongTimeline scaled = editor.timeline();
    REQUIRE_FALSE(scaled == original);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == original);
    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == scaled);
}

// Scale Tempo changes the whole song, so no one marker is what its undo or redo changed, and the
// window stays where it stands, as the verb itself left it: neither step focuses anything or moves
// the keyboard. Each carries the keyboard as the verb did instead, onto what it stood on: back on
// its music (the unlocked stretch kept it there, so it returns to 5 s) or at its time (the locked
// relabel held 5 s). Asked of the passive cursor and of a caret armed after the verb, whose
// address would otherwise name other music under the restored map.
TEST_CASE(
    "Scale Tempo's undo and redo leave the window and the keyboard on their music",
    "[core][scale-tempo]")
{
    const bool note_lock = GENERATE(true, false);
    const bool caret = GENERATE(false, true);
    CAPTURE(note_lock, caret);
    ScaleEditor editor{note_lock, makeThreeSpanSong()};
    editor.seek(5.0);
    const double before = editor.keyboardSeconds();

    editor.scale("2");
    REQUIRE(editor.undoEntryCount() == 1);
    if (caret)
    {
        editor.armCaretAtCursor();
    }
    const double after = editor.keyboardSeconds();
    CHECK_THAT(after, WithinAbs(note_lock ? 5.0 : 2.5, 1e-3));
    const std::uint64_t moves = editor.state().keyboard_move_count;
    const std::uint64_t focused = editor.state().focus_count;

    editor.controller.onUndoRequested();
    CHECK_THAT(editor.keyboardSeconds(), WithinAbs(before, 1e-3));
    CHECK(editor.state().keyboard_move_count == moves);
    CHECK(editor.state().focus_count == focused);

    editor.controller.onRedoRequested();
    CHECK_THAT(editor.keyboardSeconds(), WithinAbs(after, 1e-3));
    CHECK(editor.state().keyboard_move_count == moves);
    CHECK(editor.state().focus_count == focused);
}

// Text that is not a positive factor, a playing transport, and a factor of one all leave the song
// and the history untouched.
TEST_CASE(
    "Locked Scale Tempo refuses bad text and records nothing for no change", "[core][scale-tempo]")
{
    ScaleEditor editor{true};
    const SongTimeline original = editor.timeline();

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

// The locked form holds every time, so the transport is left exactly where it stood — no seek, even
// for a cursor between two ticks, whose place rounds onto the new lattice by less than the cursor's
// trust tolerance — and the commit is no keyboard move, so the window has nothing to follow. It
// publishes once, after the commit has settled. The selection is released: every address it held
// now names other music, even the anchor at 1:1, which still stands at 1:1.
TEST_CASE(
    "Locked Scale Tempo leaves the cursor's time alone and selects nothing", "[core][scale-tempo]")
{
    for (const char* const factor : {"2", "1/2", "3/2"})
    {
        INFO(factor);
        ScaleEditor editor{true};
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
TEST_CASE("Locked Scale Tempo dissolves an armed caret onto its own time", "[core][scale-tempo]")
{
    ScaleEditor editor{true};
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

// The cursor is carried as one more slice, so the marker verbs author on the note it stood on. Nine
// measures of 4/4 at about 240 BPM end at 9004 ms; halved they end on 5:3, the song's last bar
// half a measure, still at 9004 ms. The note at 9:4 lands on 5:2+1/2 exactly where it sounded, so
// the transport stays and the cursor's column is the note's.
TEST_CASE("Locked Scale Tempo carries the cursor onto its note", "[core][scale-tempo]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("10:1", 9004)});
    std::optional<common::core::Chart>& song_chart = song.arrangements.front().chart;
    REQUIRE(song_chart.has_value());
    if (song_chart.has_value())
    {
        song_chart->notes.push_back(makeTestNote(beatAt(9, 4), 1, 5));
    }
    ScaleEditor editor{true, std::move(song)};
    const double sounded = editor.tempoMap().secondsAt(beatAt(9, 4));
    editor.seek(sounded);
    const int seeks = editor.transport.seek_call_count;

    editor.scale("1/2");
    REQUIRE(editor.tempoMap().anchors().back() == anchor("5:3", 9004));
    const GridPosition landed{.measure = 5, .beat = 2, .offset = common::core::Fraction{1, 2}};
    const common::core::Chart& chart = editor.chart();
    REQUIRE(chart.notes.size() == 4);
    CHECK(chart.notes.back().position == landed);
    CHECK(editor.transport.seek_call_count == seeks);
    CHECK_THAT(editor.transport.position().seconds, WithinULP(sounded, 0));
    // The section chord asks for nothing but the cursor's slot, so it reads where the cursor
    // stands; asked with the Note Lock, which refuses the chord first, off.
    editor.controller.onNoteLockToggleRequested();
    REQUIRE_FALSE(editor.state().note_lock);
    const std::optional<MarkerPrompt> slot =
        editor.pressForPrompt(&EditorController::onSongSectionChordRequested);
    CHECK(slot == std::optional<MarkerPrompt>{InsertSectionTarget{.position = landed}});
}

// With the Note Lock off, Scale Tempo stretches: every anchor keeps its address and its distance
// from the first anchor halves, so every span's tempo doubles, and the terminal grows to cover the
// audio as a typed tempo's does. Every note, section, meter and hand position keeps its address, so
// each moves in time exactly as its beat does; the cursor too. One entry, labelled as the form
// that ran.
TEST_CASE("Unlocked Scale Tempo stretches the map and keeps every address", "[core][scale-tempo]")
{
    ScaleEditor editor{false, makeThreeSpanSong()};
    const SongTimeline before = editor.timeline();
    editor.seek(3.0);
    const int seeks = editor.transport.seek_call_count;

    editor.scale("2");
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Scale Tempo (notes follow the beat)");
    const SongTimeline after = editor.timeline();
    CHECK(
        after.tempo_map.anchors() ==
        std::vector{
            anchor("1:1", 0), anchor("3:1", 1500), anchor("5:1", 4000), anchor("21:1", 20000)
        });
    CHECK(after.tempo_map.timeSignatures() == before.tempo_map.timeSignatures());
    CHECK(tempoSpanBpmText(after.tempo_map, 0) == "320");
    CHECK(tempoSpanBpmText(after.tempo_map, 1) == "192");
    CHECK(tempoSpanBpmText(after.tempo_map, 2) == "240");

    CHECK(after.sections == before.sections);
    CHECK(after.arrangements == before.arrangements);
    for (const common::core::SongSection& section : after.sections)
    {
        CHECK_THAT(
            after.tempo_map.secondsAt(section.position),
            WithinAbs(before.tempo_map.secondsAt(section.position) / 2.0, 1e-9));
    }
    for (const common::core::ChartNote& note : editor.chart().notes)
    {
        CHECK_THAT(
            after.tempo_map.secondsAt(note.position),
            WithinAbs(before.tempo_map.secondsAt(note.position) / 2.0, 1e-9));
    }
    CHECK(editor.transport.seek_call_count == seeks + 1);
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(1.5, 1e-9));
    CHECK_FALSE(editor.state().selected_tempo_anchor.has_value());

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == before);
}

// Each form grows nothing it need not: /2 doubles every distance, so the terminal already covers
// the audio and stays on its address; x2 halves them, so the terminal grows at the last span's
// doubled tempo to the first downbeat covering the audio.
TEST_CASE("Unlocked Scale Tempo's terminal covers the audio", "[core][scale-tempo]")
{
    ScaleEditor editor{false};
    const double audio_end =
        editor.controller.session().song().arrangements.front().audio_duration.seconds;
    REQUIRE_THAT(audio_end, WithinAbs(20.0, 1e-9));

    SECTION("/2")
    {
        editor.scale("1/2");
        CHECK(
            editor.tempoMap() ==
            common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("9:1", 32000)}));
    }
    SECTION("x2")
    {
        editor.scale("2");
        CHECK(
            editor.tempoMap() ==
            common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("21:1", 20000)}));
    }
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.tempoMap().anchors().back().seconds() >= audio_end);
}

// Halves round away from zero, so x2 then /2 puts every anchor back within a millisecond of where
// it stood, on its own address: 3001 ms halves to 1501 and doubles to 3002. The audio ends at 8 s,
// which the halved terminal at 8003 ms still covers, so no growth moves it.
TEST_CASE("Unlocked x2 then /2 restores every anchor within the rounding", "[core][scale-tempo]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("3:1", 3001),
        anchor("5:1", 8003),
        anchor("9:1", 16005),
    });
    song.arrangements.front().audio_duration = common::core::TimeDuration{8.0};
    ScaleEditor editor{false, std::move(song)};
    const SongTimeline before = editor.timeline();

    editor.scale("2");
    CHECK(
        editor.tempoMap().anchors() ==
        std::vector{
            anchor("1:1", 0), anchor("3:1", 1501), anchor("5:1", 4002), anchor("9:1", 8003)
        });
    editor.scale("1/2");
    CHECK(editor.undoEntryCount() == 2);
    const SongTimeline after = editor.timeline();
    checkAnchorsRestored(before, after);
    CHECK(after.sections == before.sections);
    CHECK(after.arrangements == before.arrangements);
}

// Nothing is re-placed unlocked, so the one refusal the locked form has — a factor carrying a
// meter off the meter lattice — does not apply: 4/3 lands on a song with a 6/8 at 5:1, the meters
// where they stood.
TEST_CASE("Unlocked Scale Tempo applies no meter-lattice refusal", "[core][scale-tempo]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::TempoMap{
        common::core::testing::metersAt(
            {{.at = "1:1", .numerator = 4, .denominator = 4},
             {.at = "5:1", .numerator = 6, .denominator = 8}}),
        {anchor("1:1", 0), anchor("7:1", 11000)},
    };
    ScaleEditor editor{false, std::move(song)};
    const SongTimeline before = editor.timeline();

    editor.scale("4/3");
    CHECK(editor.undoEntryCount() == 1);
    const SongTimeline after = editor.timeline();
    CHECK(after.tempo_map.timeSignatures() == before.tempo_map.timeSignatures());
    CHECK(after.arrangements == before.arrangements);
    REQUIRE(after.tempo_map.anchors().size() == 2);
    CHECK(after.tempo_map.anchors()[0] == anchor("1:1", 0));
    CHECK(after.tempo_map.anchors()[1].seconds() >= 20.0);
}

// A factor that lands two anchors on one millisecond is refused by the tempo-map rules at the
// commit: 7 ms from the first anchor is nothing after x16, so the song and the history stand.
TEST_CASE("Unlocked Scale Tempo refuses a factor collapsing two anchors", "[core][scale-tempo]")
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap(
        {anchor("1:1", 0), anchor("2:1", 7), anchor("9:1", 16000)});
    ScaleEditor editor{false, std::move(song)};
    const SongTimeline before = editor.timeline();

    editor.scale("16");
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == 0);
}

// The command asks the core for its prompt, which titles it as the entry it will record: the form
// the Note Lock chooses, read where the lock lives rather than off the published state.
TEST_CASE("Scale Tempo's prompt is titled by the form the Note Lock chooses", "[core][scale-tempo]")
{
    const bool note_lock = GENERATE(true, false);
    CAPTURE(note_lock);
    ScaleEditor editor{note_lock};
    const std::size_t prompts = editor.view.shown_marker_prompts.size();

    editor.controller.onScaleTempoPromptRequested();
    REQUIRE(editor.view.shown_marker_prompts.size() == prompts + 1);
    CHECK(
        editor.view.shown_marker_prompts.back() ==
        MarkerPrompt{ScaleTempoTarget{
            .label = note_lock ? "Scale Tempo (notes keep their time)"
                               : "Scale Tempo (notes follow the beat)",
        }});
}

} // namespace rock_hero::editor::core
