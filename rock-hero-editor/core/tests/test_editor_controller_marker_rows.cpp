#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::GridPosition;
using common::core::SongSection;

[[nodiscard]] GridPosition downbeat(int measure)
{
    return GridPosition{.measure = measure, .beat = 1};
}

// Two tempo spans and two meters whose measures all last two seconds: 4/4 at 120 BPM through
// measure 4, then 3/4 at 90 BPM from measure 5, so measure N's downbeat sits at (N - 1) * 2 seconds
// on both sides of the change. The ruler rows each carry two markers: tempo anchors at 1:1 and
// 5:1, signatures at measures 1 and 5.
[[nodiscard]] common::core::TempoMap makeMarkerTempoMap()
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            common::core::TimeSignatureChange{.measure = 5, .numerator = 3, .denominator = 4},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 5, .beat = 1, .seconds = 8.0},
            common::core::BeatAnchor{.measure = 21, .beat = 1, .seconds = 40.0},
        },
    };
}

// Sections at measures 3 and 7, so the opening two measures are the first section's lead-in.
[[nodiscard]] std::vector<SongSection> makeMarkerSections()
{
    return {
        SongSection{.position = downbeat(3), .name = "Verse"},
        SongSection{.position = downbeat(7), .name = "Chorus"},
    };
}

// Owns the fakes and a controller with the six-string chart loaded over the marker tempo map.
struct MarkerRowEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        },
    };
    FakeEditorView view;

    explicit MarkerRowEditor(std::vector<SongSection> sections = makeMarkerSections())
    {
        controller.attachView(view);
        const bool loaded = loadChartArrangement(
            controller,
            project_services,
            audio,
            std::move(sections),
            makeTestChart(),
            makeMarkerTempoMap());
        REQUIRE(loaded);
    }

    [[nodiscard]] const EditorViewState& state() const
    {
        const EditorViewState* const published = stateOrNull(view.last_state);
        REQUIRE(published != nullptr);
        if (published == nullptr)
        {
            throw std::logic_error("editor pushed no view state");
        }
        return *published;
    }

    void step(ChartStepDirection direction, bool reach = false)
    {
        controller.onChartCaretStepRequested(direction, reach);
    }

    // Parks the passive cursor on a measure downbeat and arms the caret there on string 1.
    void armAtMeasure(int measure)
    {
        controller.onTimelineSeekRequested(
            common::core::TimePosition{static_cast<double>(measure - 1) * 2.0});
        step(ChartStepDirection::Right);
    }

    // The armed string caret's string, or nothing while no string caret is armed.
    [[nodiscard]] std::optional<int> caretString() const
    {
        const ChartCaretViewState* const caret = caretOrNull(state().chart_edit);
        return caret != nullptr ? std::optional{caret->string} : std::nullopt;
    }

    // The index of the published section drawn selected, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedSectionIndex() const
    {
        const std::vector<SongSectionViewState>& sections = state().sections;
        const auto found = std::ranges::find_if(sections, &SongSectionViewState::selected);
        if (found == sections.end())
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::distance(sections.begin(), found));
    }
};

} // namespace

// Up off the top string walks the ruler's rows by selection — time signature, tempo, section — and
// each lands on the marker holding the cursor, with the caret disarmed and the cursor left where it
// stood. Down retraces them and re-arms on the top string at the same slot.
TEST_CASE("EditorController walks up from the strings onto the ruler rows", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(6);
    for (int press = 0; press < 5; ++press)
    {
        editor.step(ChartStepDirection::Up);
    }
    REQUIRE(editor.caretString() == std::optional{6});

    editor.step(ChartStepDirection::Up);
    CHECK_FALSE(editor.caretString().has_value());
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));
    // A tempo or signature chip carries no Delete yet, so it is not a deletable selection.
    CHECK_FALSE(editor.state().selection_present);
    const std::optional<SelectedRowCursorViewState>& cursor = editor.state().selected_row_cursor;
    REQUIRE(cursor.has_value());
    if (cursor.has_value())
    {
        CHECK(cursor->seconds == Catch::Approx(10.0));
        CHECK(cursor->measure_start_seconds == Catch::Approx(10.0));
        CHECK(cursor->measure_end_seconds == Catch::Approx(12.0));
    }

    editor.step(ChartStepDirection::Up);
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    CHECK_FALSE(editor.state().selected_time_signature_measure.has_value());

    editor.step(ChartStepDirection::Up);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.state().selected_tempo_anchor.has_value());

    // The section row is the top of the stack.
    editor.step(ChartStepDirection::Up);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});

    editor.step(ChartStepDirection::Down);
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    editor.step(ChartStepDirection::Down);
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});
    editor.step(ChartStepDirection::Down);
    CHECK(editor.caretString() == std::optional{6});
    CHECK_FALSE(editor.state().selected_row_cursor.has_value());
    const ChartCaretViewState* const caret = caretOrNull(editor.state().chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(10.0));
}

// Ctrl's reach jumps a whole group and lands on its nearest row: any string reaches the signature
// row, and the signature row reaches the top string; between the ruler's one-row groups reach and
// the plain step coincide.
TEST_CASE("EditorController reaches between the ruler rows and the strings", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(2);
    editor.step(ChartStepDirection::Up);
    editor.step(ChartStepDirection::Up);
    REQUIRE(editor.caretString() == std::optional{3});

    editor.step(ChartStepDirection::Up, true);
    CHECK_FALSE(editor.caretString().has_value());
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
    editor.step(ChartStepDirection::Up, true);
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
    editor.step(ChartStepDirection::Down, true);
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
    editor.step(ChartStepDirection::Down, true);
    CHECK(editor.caretString() == std::optional{6});
}

// The first marker of a row also owns whatever precedes it, so a cursor ahead of the first section
// still reaches the section row: the first section is selected and the cursor stays in the lead-in.
// With no sections at all the row is absent and Up from the tempo row is inert.
TEST_CASE("EditorController gives the lead-in to a row's first marker", "[core][marker-rows]")
{
    SECTION("the first section holds the lead-in")
    {
        MarkerRowEditor editor;
        editor.armAtMeasure(1);
        editor.step(ChartStepDirection::Up, true);
        editor.step(ChartStepDirection::Up);
        editor.step(ChartStepDirection::Up);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(0.0));

        // Stepping off it keeps the cursor in the lead-in, since the section holds it.
        editor.step(ChartStepDirection::Down);
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
        CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    }

    SECTION("no sections leaves no section row")
    {
        MarkerRowEditor editor{std::vector<SongSection>{}};
        editor.armAtMeasure(1);
        editor.step(ChartStepDirection::Up, true);
        editor.step(ChartStepDirection::Up);
        REQUIRE(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
        editor.step(ChartStepDirection::Up);
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
        CHECK_FALSE(editor.selectedSectionIndex().has_value());
    }
}

// A chip clicked with the pointer seeks nothing, so the cursor may stand outside its marker's span.
// Stepping off the marker brings the cursor to its start first, so the next row's holder is the one
// found at that marker, and the published cursor moves with it for the view to reveal.
TEST_CASE("EditorController steps off a clicked chip from its start", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    const int seeks_before = editor.transport.seek_call_count;
    editor.controller.onSongSectionSelected(downbeat(7));
    CHECK(editor.transport.seek_call_count == seeks_before);
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
    const std::optional<SelectedRowCursorViewState> clicked = editor.state().selected_row_cursor;
    REQUIRE(clicked.has_value());

    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(12.0));
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    const std::optional<SelectedRowCursorViewState> moved = editor.state().selected_row_cursor;
    REQUIRE(moved.has_value());
    if (clicked.has_value() && moved.has_value())
    {
        CHECK(clicked->seconds == Catch::Approx(0.0));
        CHECK(moved->seconds == Catch::Approx(12.0));
    }

    // A tempo chip clicked away from the cursor behaves the same way.
    editor.controller.onTempoAnchorSelected(downbeat(1));
    REQUIRE(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
}

// A tempo or time-signature chip selection has no verbs yet: Delete and Alt+arrows leave the tempo
// map untouched, Esc releases it, and a cursor move releases it like every marker selection.
TEST_CASE("EditorController keeps tempo and signature selections inert", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    const common::core::TempoMap before = editor.controller.session().song().tempo_map;

    editor.controller.onTimeSignatureSelected(5);
    REQUIRE(editor.state().selected_time_signature_measure == std::optional{5});
    editor.controller.onSelectionDeleteRequested();
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.controller.session().song().tempo_map == before);
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});

    editor.controller.onChartEscapePressed();
    CHECK_FALSE(editor.state().selected_time_signature_measure.has_value());

    // A position no anchor pins names no chip, so it selects nothing.
    editor.controller.onTempoAnchorSelected(downbeat(3));
    CHECK_FALSE(editor.state().selected_tempo_anchor.has_value());

    editor.controller.onTempoAnchorSelected(downbeat(5));
    REQUIRE(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.0});
    CHECK_FALSE(editor.state().selected_tempo_anchor.has_value());
    CHECK_FALSE(editor.state().selected_row_cursor.has_value());
}

// Undo can take away the marker a selection names; nothing is left to select then, exactly as
// Delete leaves nothing behind.
TEST_CASE("EditorController releases a marker selection an undo takes away", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.controller.onSongSectionInsertRequested(downbeat(5), "Bridge");
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});

    editor.controller.onUndoRequested();
    CHECK(editor.controller.session().song().sections.size() == 2);
    CHECK_FALSE(editor.selectedSectionIndex().has_value());
    CHECK_FALSE(editor.state().selection_present);
    CHECK_FALSE(editor.state().selected_row_cursor.has_value());
}

// Tab on a marker row steps from the SELECTED marker to its neighbour, selecting it and bringing
// the cursor to its start; past either end the press is inert. Ctrl+Tab steps a marker row exactly
// as Tab does, since only a string has keyframes to step over.
TEST_CASE("EditorController steps a marker row to the neighbouring marker", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(1);
    editor.step(ChartStepDirection::Up, true);
    editor.step(ChartStepDirection::Up);
    editor.step(ChartStepDirection::Up);
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{0});

    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(12.0));
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(12.0));

    editor.controller.onRowObjectStepRequested(false, true);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK(editor.transport.position().seconds == Catch::Approx(4.0));
    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});

    // A chip clicked far from the cursor is where the step starts, not the cursor.
    editor.controller.onTimeSignatureSelected(5);
    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    CHECK(caretOrNull(editor.state().chart_edit) == nullptr);
}

} // namespace rock_hero::editor::core
