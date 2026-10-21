#include "log_capture.h"
#include "timeline/keep_notes_in_time.h"
#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using common::core::SongTimeline;
using common::core::testing::anchor;
using common::core::testing::beatAt;
using testing::checkTimesHeld;

// The eight-measure song (notes at 2:1 and 3:1, hand positions at 1:1 and 3:1) under a map whose
// tempo changes at 3:1 and 5:1: two measures in 3 s (375 ms a beat), two in 5 s (625 ms a beat),
// then four at 120 BPM to 9:1 at 16 s. Opened with the Note Lock on, the tempo lock off, and the
// anchor at 3:1 selected.
struct NoteLockEditor : SongEditorHarness
{
    NoteLockEditor()
    {
        common::core::Song song = makeEightMeasureSong();
        song.tempo_map = common::core::testing::commonTimeMap({
            anchor("1:1", 0),
            anchor("3:1", 3000),
            anchor("5:1", 8000),
            anchor("9:1", 16000),
        });
        open(std::move(song));
        controller.onTempoLockToggleRequested();
        controller.onNoteLockToggleRequested();
        REQUIRE_FALSE(state().tempo_lock);
        REQUIRE(state().note_lock);
        controller.onTempoAnchorSelected(beatAt(3, 1));
        REQUIRE(state().selected_tempo_anchor == std::optional{beatAt(3, 1)});
    }
};

// The eight-measure song with a note on every beat of every string, each string a different way
// into its beat (on it, a third, two thirds, a fifth, two fifths, three fifths), so a tempo edit
// whose anchor rounds to a whole millisecond leaves most of them between two ticks. Anchors on
// 1:1, 3:1 and 5:1 give three spans to retime.
[[nodiscard]] common::core::Song makeDenseSong()
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("3:1", 4000),
        anchor("5:1", 8000),
        anchor("9:1", 16000),
    });
    std::optional<common::core::Chart>& song_chart = song.arrangements.front().chart;
    REQUIRE(song_chart.has_value());
    if (song_chart.has_value())
    {
        common::core::Chart& chart = *song_chart;
        chart.fret_hand_positions.clear();
        chart.notes.clear();
        constexpr std::array<common::core::Fraction, 6> offsets{
            common::core::Fraction{},
            common::core::Fraction{1, 3},
            common::core::Fraction{2, 3},
            common::core::Fraction{1, 5},
            common::core::Fraction{2, 5},
            common::core::Fraction{3, 5},
        };
        for (int measure = 1; measure <= 8; ++measure)
        {
            for (int beat = 1; beat <= 4; ++beat)
            {
                for (int string = 1; string <= 6; ++string)
                {
                    chart.notes.push_back(makeTestNote(
                        common::core::GridPosition{
                            .measure = measure,
                            .beat = beat,
                            .offset = offsets.at(static_cast<std::size_t>(string - 1)),
                        },
                        string,
                        5));
                }
            }
        }
        // The chart's own order, (position, string), which every planner and projection reads.
        std::ranges::sort(
            chart.notes,
            [](const common::core::ChartNote& lhs, const common::core::ChartNote& rhs) {
                return lhs.position != rhs.position ? lhs.position < rhs.position
                                                    : lhs.string < rhs.string;
            });
    }
    return song;
}

// The dense song, opened with the Note Lock on.
struct DenseNoteLockEditor : SongEditorHarness
{
    explicit DenseNoteLockEditor(common::core::Song song = makeDenseSong())
    {
        open(std::move(song));
        controller.onNoteLockToggleRequested();
        REQUIRE(state().note_lock);
    }
};

// Eight typed tempos, each on a different span and each landing its anchors on whole milliseconds
// that put the notes between ticks. Applied one after another they must read exactly as ONE
// re-address of the song as it stood when the lock engaged under the final map.
struct TypedTempo
{
    common::core::GridPosition span;
    const char* bpm{};
};

constexpr std::array<TypedTempo, 8> g_typed_tempos{{
    {.span = beatAt(1, 1), .bpm = "97"},
    {.span = beatAt(3, 1), .bpm = "131"},
    {.span = beatAt(5, 1), .bpm = "113"},
    {.span = beatAt(1, 1), .bpm = "139"},
    {.span = beatAt(3, 1), .bpm = "101"},
    {.span = beatAt(5, 1), .bpm = "127"},
    {.span = beatAt(1, 1), .bpm = "107"},
    {.span = beatAt(3, 1), .bpm = "149"},
}};

// Checks the lock's law against a timeline the lock engaged on: the song now is that timeline laid
// under the map now, exactly, so every time holds within half a tick of where it stood at lock-on.
void checkHeldFrom(const SongTimeline& origin, const SongEditorHarness& editor)
{
    const SongTimeline after = editor.timeline();
    CHECK(after == timeKeptTimeline(origin, after.tempo_map));
    checkTimesHeld(origin, after, 0.0);
}

} // namespace

// Separate lock-on tempo edits never stack their rounding: however many are made, each place rounds
// once, from its time at lock-on onto the map the edits reach, and the song reads exactly as one
// re-address of the lock's origin.
TEST_CASE("Separate Note Lock edits round once from the lock's origin", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    const SongTimeline origin = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    for (const TypedTempo& typed : g_typed_tempos)
    {
        editor.controller.onTempoSpanBpmRequested(typed.span, typed.bpm);
    }
    REQUIRE(editor.undoEntryCount() == entries + g_typed_tempos.size());
    checkHeldFrom(origin, editor);
}

// Undo and redo inside the lock's own run keep its origin: the edits made after them still round
// once from the content at lock-on, never from the state the undo landed on.
TEST_CASE("Undo within the Note Lock's run keeps its origin", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    const SongTimeline origin = editor.timeline();

    for (std::size_t edit = 0; edit < 4; ++edit)
    {
        editor.controller.onTempoSpanBpmRequested(
            g_typed_tempos.at(edit).span, g_typed_tempos.at(edit).bpm);
    }
    editor.controller.onUndoRequested();
    editor.controller.onUndoRequested();
    editor.controller.onRedoRequested();
    editor.controller.onUndoRequested();
    for (std::size_t edit = 4; edit < g_typed_tempos.size(); ++edit)
    {
        editor.controller.onTempoSpanBpmRequested(
            g_typed_tempos.at(edit).span, g_typed_tempos.at(edit).bpm);
    }
    checkHeldFrom(origin, editor);
}

// The lock holds the content as the song shows it. An undo that reaches below the moment the lock
// engaged, or a redo onto an edit made before it, changes that content, so the lock re-engages on
// what the history landed: the next tempo edit holds THAT content — the undone section stays
// undone, the redone one stays — and rounds once from it.
TEST_CASE("The Note Lock re-engages where the history leaves its run", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    editor.controller.onNoteLockToggleRequested();
    REQUIRE_FALSE(editor.state().note_lock);
    const std::size_t sections = editor.sections().size();
    editor.controller.onSongSectionInsertRequested(beatAt(5, 1), "Bridge");
    REQUIRE(editor.sections().size() == sections + 1);

    SECTION("an undo below the lock-on point")
    {
        editor.controller.onNoteLockToggleRequested();
        editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
        editor.controller.onUndoRequested();
        editor.controller.onUndoRequested();
        REQUIRE(editor.sections().size() == sections);
        const SongTimeline landed = editor.timeline();

        editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
        editor.controller.onTempoSpanBpmRequested(g_typed_tempos[2].span, g_typed_tempos[2].bpm);
        CHECK(editor.sections().size() == sections);
        checkHeldFrom(landed, editor);
    }

    SECTION("a redo onto an edit made before the lock engaged")
    {
        editor.controller.onUndoRequested();
        REQUIRE(editor.sections().size() == sections);
        editor.controller.onNoteLockToggleRequested();
        editor.controller.onRedoRequested();
        REQUIRE(editor.sections().size() == sections + 1);
        const SongTimeline landed = editor.timeline();

        editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
        editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
        CHECK(editor.sections().size() == sections + 1);
        checkHeldFrom(landed, editor);
    }
}

// Turning the lock off and on again engages it on the content as it then stands, which the edits
// before it have already rounded once: the run from there is one rounding of that content.
TEST_CASE(
    "Re-engaging the Note Lock holds the content as it then stands", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
    editor.controller.onNoteLockToggleRequested();
    editor.controller.onNoteLockToggleRequested();
    REQUIRE(editor.state().note_lock);
    const SongTimeline relocked = editor.timeline();

    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[2].span, g_typed_tempos[2].bpm);
    checkHeldFrom(relocked, editor);
}

// Switching arrangements changes no content, so the lock keeps its origin across the switch: the
// edits made on the other arrangement still round every arrangement's content once from lock-on.
TEST_CASE("The Note Lock keeps its origin across an arrangement switch", "[core][tempo][note-lock]")
{
    common::core::Song song = makeDenseSong();
    song.arrangements.push_back(song.arrangements.front());
    song.arrangements.back().id = g_bass_arrangement_id;
    song.arrangements.back().part = common::core::Part::Bass;
    DenseNoteLockEditor editor{std::move(song)};
    REQUIRE(editor.timeline().arrangements.size() == 2);
    const SongTimeline origin = editor.timeline();

    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
    editor.controller.onArrangementSelected(std::string{g_bass_arrangement_id});
    REQUIRE(editor.controller.session().currentArrangement() != nullptr);
    REQUIRE(editor.controller.session().currentArrangement()->part == common::core::Part::Bass);
    REQUIRE(editor.state().note_lock);
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[2].span, g_typed_tempos[2].bpm);
    checkHeldFrom(origin, editor);
}

// The grid verbs stay open under the lock. An anchor insert lays the map with its new anchor under
// the held content like every tempo verb; a meter edit keeps every place exactly, so the held
// content stays true through it and the tempo edits after it still round once from lock-on.
TEST_CASE("Grid verbs under the Note Lock hold the lock's origin", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    const SongTimeline origin = editor.timeline();

    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
    SECTION("an anchor insert")
    {
        editor.controller.onTempoAnchorInsertRequested(beatAt(2, 1));
        CHECK(editor.anchorMilliseconds(beatAt(2, 1)).has_value());
        CHECK(editor.undoTopLabel() == "Add Tempo Anchor");
    }
    SECTION("a meter edit")
    {
        editor.controller.onTimeSignatureSetRequested(beatAt(5, 1), "3/4");
        CHECK(editor.tempoMap().timeSignatures().size() == 2);
    }
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[2].span, g_typed_tempos[2].bpm);
    checkHeldFrom(origin, editor);
}

// Under the lock Scale Tempo relabels, every time held, and rounds the content it scales, so it
// re-engages the lock on what it wrote: the tempo edits after it round once from the scaled song.
TEST_CASE("Scale Tempo under the Note Lock re-engages it on its result", "[core][tempo][note-lock]")
{
    DenseNoteLockEditor editor;
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[0].span, g_typed_tempos[0].bpm);
    const SongTimeline before_scale = editor.timeline();
    editor.controller.onScaleTempoRequested("3/2");
    REQUIRE(editor.undoTopLabel() == "Scale Tempo (notes keep their time)");
    const SongTimeline scaled = editor.timeline();
    REQUIRE_FALSE(scaled == before_scale);
    checkTimesHeld(before_scale, scaled, 0.0);

    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[1].span, g_typed_tempos[1].bpm);
    editor.controller.onTempoSpanBpmRequested(g_typed_tempos[2].span, g_typed_tempos[2].bpm);
    checkHeldFrom(scaled, editor);
}

// A locked tempo edit can round two holds together: a none hold starting a tick after another ends
// rounds onto that end, and the two that now meet are one stretch. The edit writes the merged hold
// once, and the lock re-engages on what it wrote, so the next locked edit starts from the rewrite
// rather than making it, and logging it, again.
TEST_CASE("A locked tempo edit rewrites a shape hold once", "[core][tempo][note-lock]")
{
    const testing::LogCapture capture;
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = common::core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("3:1", 3000),
        anchor("5:1", 8000),
        anchor("9:1", 16000),
    });
    std::optional<common::core::Chart>& song_chart = song.arrangements.front().chart;
    REQUIRE(song_chart.has_value());
    if (!song_chart.has_value())
    {
        return;
    }
    common::core::Chart& chart = *song_chart;
    chart.fret_hand_positions.clear();
    // Two none holds a tick apart.
    const common::core::GridPosition resumed{
        .measure = 1, .beat = 2, .offset = common::core::Fraction{1, 960}
    };
    chart.shape_holds = {
        common::core::ShapeHold{
            .start = beatAt(1, 1),
            .end = beatAt(1, 2),
            .grip = common::core::NoneGrip{},
        },
        common::core::ShapeHold{
            .start = resumed,
            .end = beatAt(1, 3),
            .grip = common::core::NoneGrip{},
        },
    };
    DenseNoteLockEditor editor{std::move(song)};
    const auto conversions_logged = [&capture] {
        const std::string text = capture.text();
        std::size_t count = 0;
        for (std::size_t at = text.find("chart edit normalized"); at != std::string::npos;
             at = text.find("chart edit normalized", at + 1))
        {
            ++count;
        }
        return count;
    };
    const auto holds = [&editor] {
        const std::optional<common::core::Chart>& held =
            editor.controller.session().song().arrangements.front().chart;
        return held.has_value() ? held->shape_holds : std::vector<common::core::ShapeHold>{};
    };

    // Half the tempo halves every place: the second hold's start rounds onto the first's end.
    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "80");
    const std::vector<common::core::ShapeHold> merged{common::core::ShapeHold{
        .start = beatAt(1, 1),
        .end = beatAt(1, 2),
        .grip = common::core::NoneGrip{},
    }};
    CHECK(holds() == merged);
    CHECK(conversions_logged() == 1);

    // A slower tempo still would round the two together again from the content as it stood at
    // lock-on; from the rewritten content there is nothing left to rewrite.
    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "60");
    CHECK(conversions_logged() == 1);
    REQUIRE(holds().size() == 1);
    CHECK(holds().front().start == beatAt(1, 1));
}

// While the lock holds the song's content, every edit that would change that content is refused —
// logged, nothing written, no entry — and the plugin removal, which takes automation out of the
// song, is unavailable. Turning the lock off opens them again.
TEST_CASE("The Note Lock refuses every content edit", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    SECTION("a typed note is refused at the plan, so its pending box is red")
    {
        editor.seek(6.0);
        editor.armCaretAtCursor();
        editor.controller.onChartFretDigitTyped(5);
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK_FALSE(pending->valid);
        }
        editor.seek(2.0);
        CHECK_FALSE(editor.state().chart_edit.pending_fret.has_value());
    }
    SECTION("a note delete is refused and flashes the notes it would take")
    {
        editor.seek(1.5);
        editor.armCaretAtCursor();
        const std::vector<std::size_t> selected = editor.state().chart_edit.selected_notes;
        REQUIRE_FALSE(selected.empty());
        editor.controller.onSelectionDeleteRequested();
        REQUIRE(editor.view.refusal_flashes.size() == 1);
        CHECK(editor.view.refusal_flashes.front().notes == selected);
    }
    SECTION("a note move is refused")
    {
        editor.seek(1.5);
        editor.armCaretAtCursor();
        REQUIRE_FALSE(editor.state().chart_edit.selected_notes.empty());
        editor.pressAlt(ChartStepDirection::Right);
        CHECK(editor.view.refusal_flashes.size() == 1);
    }
    SECTION("a section insert is refused")
    {
        editor.controller.onSongSectionInsertRequested(beatAt(5, 1), "Bridge");
    }
    SECTION("a fret-hand position is refused")
    {
        editor.seek(6.0);
        editor.armCaretAtCursor();
        editor.controller.onFretHandPositionChordRequested();
    }
    SECTION("a tone region is refused")
    {
        editor.controller.onToneRegionCreateRequested(
            beatAt(4, 1), "00000000-0000-4000-8000-00000000beef", g_tone_document_ref);
    }
    SECTION("automation points are refused")
    {
        editor.controller.onToneAutomationPointsEditRequested(
            "loaded-instance",
            "gain",
            {common::core::ToneAutomationPoint{.position = beatAt(2, 1), .norm_value = 0.3F}});
    }
    SECTION("the plugin removal is unavailable until the lock is off")
    {
        CHECK_FALSE(editor.state().signal_chain.remove_plugins_enabled);
        editor.controller.onNoteLockToggleRequested();
        CHECK(editor.state().signal_chain.remove_plugins_enabled);
    }

    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
}

// Off, the same verbs land: the refusal is the lock's and nothing else's.
TEST_CASE("With the Note Lock off the content verbs land", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.controller.onNoteLockToggleRequested();
    REQUIRE_FALSE(editor.state().note_lock);
    const std::size_t sections = editor.sections().size();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onSongSectionInsertRequested(beatAt(5, 1), "Bridge");
    CHECK(editor.sections().size() == sections + 1);
    CHECK(editor.undoEntryCount() == entries + 1);
}

// The switch is session state: off at every open, whatever the session before it left, and
// flipping it records nothing to undo.
TEST_CASE("The Note Lock is off at every open", "[core][tempo][note-lock]")
{
    SongEditorHarness editor;
    editor.open(makeEightMeasureSong());
    CHECK_FALSE(editor.state().note_lock);

    editor.controller.onNoteLockToggleRequested();
    CHECK(editor.state().note_lock);
    CHECK(editor.undoEntryCount() == 0);

    editor.open(makeEightMeasureSong());
    REQUIRE(editor.project_services.open_call_count == 2);
    CHECK_FALSE(editor.state().note_lock);
}

// With the switch on, every tempo verb that changes a time leaves every object where it sounds,
// within half a tick of the new grid, as ONE entry that undo takes back whole. A held Alt+arrow
// run is one entry too, re-addressed from where the run began.
TEST_CASE("The Note Lock holds every time through each tempo verb", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    std::string label;
    SECTION("a typed tempo")
    {
        editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
        CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4800});
        label = "Set Tempo to 100";
    }
    SECTION("a delete")
    {
        editor.controller.onSelectionDeleteRequested();
        CHECK_FALSE(editor.anchorMilliseconds(beatAt(3, 1)).has_value());
        label = "Delete Tempo Anchor";
    }
    SECTION("a held nudge run")
    {
        editor.pressAlt(ChartStepDirection::Right, 7);
        CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{3070});
        label = "Move Tempo Anchor";
    }

    const SongTimeline after = editor.timeline();
    REQUIRE_FALSE(after.arrangements == before.arrangements);
    checkTimesHeld(before, after, 0.0);
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == label);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == before);
    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == after);
}

// A nudge run that walks back to where it began rounds nothing, because every press re-addresses
// from the run's origin, so it lands the song exactly as it was and leaves no entry.
TEST_CASE("A walked-back nudge run under the Note Lock leaves no entry", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right, 3);
    editor.pressAlt(ChartStepDirection::Left, 3);
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
}

// With the switch off the grid is truth: the same typed tempo keeps every address, so the notes
// move with their beats.
TEST_CASE("With the Note Lock off a tempo edit keeps every address", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.controller.onNoteLockToggleRequested();
    REQUIRE_FALSE(editor.state().note_lock);
    const SongTimeline before = editor.timeline();

    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
    const SongTimeline after = editor.timeline();
    CHECK(after.arrangements == before.arrangements);
    CHECK(after.sections == before.sections);
    CHECK_FALSE(after.tempo_map == before.tempo_map);
}

// The cursor is carried as one more slice through the content map, so it keeps its time. At 2 s it
// stands on 2:2+1/3; at 100 BPM the same instant is 1:4+1/3, a tick-lattice place, so it holds
// exactly.
TEST_CASE("The Note Lock keeps the cursor's time", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.seek(2.0);

    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(2.0, 1e-9));
}

// The lock refuses moves first, whatever the switch says: a nudge and a drag both leave the song
// and the history alone, and the drag holds and previews nothing.
TEST_CASE("The tempo lock refuses moves under the Note Lock", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    editor.controller.onTempoLockToggleRequested();
    REQUIRE(editor.state().tempo_lock);
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK(editor.state().grid_map == before.tempo_map);
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(flashedRulerChips(editor.view).size() == 2);
}

// During a drag the song is not written: the grid's preview map is published beside the session's,
// and content keeps drawing under the session's map, holding its time. The release lands the whole
// drag as one entry from the map the drag started on, every time held, and the preview ends.
TEST_CASE("A Note Lock drag previews the grid and commits on release", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4300);
    // Onto the neighbour at 5:1: refused, so the last legal time stays held.
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 9000);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK(editor.timeline() == before);
    CHECK(editor.state().tempo_map == before.tempo_map);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.state().content_map == before.tempo_map);
    const TempoMapEditResult expected = moveTempoAnchor(before.tempo_map, beatAt(3, 1), 4200);
    REQUIRE(expected.has_value());
    if (expected.has_value())
    {
        CHECK(editor.state().grid_map == *expected);
    }

    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    CHECK(editor.state().grid_map == editor.tempoMap());
    CHECK(editor.state().content_map == editor.tempoMap());
    const SongTimeline after = editor.timeline();
    checkTimesHeld(before, after, 0.0);
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
    CHECK(editor.state().selected_tempo_anchor == std::optional{beatAt(3, 1)});

    // A second release has nothing left to commit.
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.undoEntryCount() == entries + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == before);
}

// A drag released where it began commits nothing, and one whose anchor stopped being the selection
// (Esc released it) drops its preview at once and commits nothing at the release.
TEST_CASE("A Note Lock drag that goes nowhere commits nothing", "[core][tempo][note-lock]")
{
    NoteLockEditor editor;
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    SECTION("released at its origin")
    {
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 3000);
    }
    SECTION("released after Esc")
    {
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        REQUIRE_FALSE(editor.state().grid_map == before.tempo_map);
        editor.controller.onChartEscapePressed();
        CHECK(editor.state().grid_map == before.tempo_map);
    }

    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.state().grid_map == before.tempo_map);
}

} // namespace rock_hero::editor::core
