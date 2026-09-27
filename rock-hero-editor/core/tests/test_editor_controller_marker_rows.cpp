#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::GridPosition;
using common::core::SongSection;

constexpr const char* g_first_region_id = "10101010-1111-4111-8111-111111111111";
constexpr const char* g_second_region_id = "20202020-2222-4222-8222-222222222222";
constexpr const char* g_first_tone_ref = "tone-clean";
constexpr const char* g_second_tone_ref = "tone-drive";

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

// The same with a third section, so a step from the MIDDLE one has a neighbour either way and a
// step that read the cursor instead of the selection would land somewhere else.
[[nodiscard]] std::vector<SongSection> makeThreeMarkerSections()
{
    std::vector<SongSection> sections = makeMarkerSections();
    sections.push_back(SongSection{.position = downbeat(11), .name = "Bridge"});
    return sections;
}

// Two tone regions with the same starts as the marker tempo changes, so the tone row can prove the
// cursor and the selected holder differ exactly as the section row does.
[[nodiscard]] std::vector<common::core::ToneRegion> makeMarkerToneRegions()
{
    return {
        common::core::ToneRegion{
            .id = g_first_region_id,
            .start = downbeat(1),
            .tone_document_ref = g_first_tone_ref,
        },
        common::core::ToneRegion{
            .id = g_second_region_id,
            .start = downbeat(5),
            .tone_document_ref = g_second_tone_ref,
        },
    };
}

// Owns the fakes and a controller with the six-string chart loaded over the marker tempo map.
struct MarkerRowEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    // The deferring scheduler and clock the controller runs on, so a typed fret stays pending
    // exactly until a further digit or the test settles it; declared before the controller, whose
    // services read it.
    PendingEntryHarness pending;
    EditorController controller{
        audioPorts(transport, audio),
        pending.services(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        },
    };
    FakeEditorView view;

    explicit MarkerRowEditor(
        std::vector<SongSection> sections = makeMarkerSections(),
        std::vector<common::core::ToneRegion> tone_regions = {},
        common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        const bool loaded = loadChartArrangement(
            controller,
            project_services,
            audio,
            std::move(sections),
            std::move(chart),
            makeMarkerTempoMap(),
            std::move(tone_regions));
        REQUIRE(loaded);
        // The deferring scheduler holds the load's own follow-up work too; run it, as
        // test_chart_fret_entry does, so every case starts from a settled, published editor. That
        // work restores the project's grid, so the quarter-note grid the loader pinned (and every
        // step below is stated in) is pinned again after it.
        static_cast<void>(pending.scheduler.runDelayed());
        controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
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

    // Parks the passive cursor on a measure downbeat, the state a pointer selection leaves behind.
    // Every measure of the marker tempo map lasts two seconds, on both sides of its meter change.
    void parkAtMeasure(int measure)
    {
        controller.onTimelineSeekRequested(
            common::core::TimePosition{static_cast<double>(measure - 1) * 2.0});
    }

    // Parks the passive cursor on a measure downbeat and arms the caret there on string 1.
    void armAtMeasure(int measure)
    {
        parkAtMeasure(measure);
        step(ChartStepDirection::Right);
    }

    // A Ctrl+Shift row jump: the walk's direct route onto the row its letter names.
    void jump(FocusRowJump row)
    {
        controller.onFocusRowJumpRequested(row);
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

    // The id of the published tone region drawn selected, or an empty string.
    [[nodiscard]] std::string selectedToneRegionId() const
    {
        const std::vector<ToneRegionViewState>& regions = state().tone_track.regions;
        const auto found = std::ranges::find_if(regions, &ToneRegionViewState::selected);
        return found == regions.end() ? std::string{} : found->id;
    }

    // The index of the fret-hand chip the lane outlines as selected, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedHandIndex() const
    {
        return state().chart_edit.selected_fret_hand_position;
    }

    // The loaded chart's fret-hand placements as they now stand.
    [[nodiscard]] std::vector<common::core::FretHandPosition> placements() const
    {
        const common::core::Arrangement* const arrangement =
            controller.session().currentArrangement();
        REQUIRE(arrangement != nullptr);
        if (arrangement == nullptr)
        {
            throw std::logic_error("no arrangement is loaded");
        }
        const std::optional<common::core::Chart>& chart = arrangement->chart;
        REQUIRE(chart.has_value());
        if (!chart.has_value())
        {
            throw std::logic_error("the arrangement carries no chart");
        }
        return chart->fret_hand_positions;
    }

    // How many entries the undo history holds, so a refusal can be told from a recorded no-op.
    [[nodiscard]] std::size_t undoEntryCount() const
    {
        return state().undo_history.labels.size();
    }
};

// A placement arriving at a position with the index finger at a fret.
[[nodiscard]] common::core::FretHandPosition placementAt(
    const GridPosition position, const int fret)
{
    return common::core::FretHandPosition{.position = position, .fret = fret};
}

// The hand chart: two short notes in measure 6 holding frets 9 and 7, and four placements — fret 5
// at 1:1, fret 12 at 5:1, and frets 2 and 4 on the first two beats of measure 8 — so the stretch
// an insert at measure 3 would govern (up to 5:1) holds no stop, the stretch one at measure 6 would
// govern (up to 8:1) holds exactly the two, and 8:2 is a start one quantum past another.
[[nodiscard]] common::core::Chart makeHandChart()
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 6, .beat = 1}, 1, 9),
        makeTestNote({.measure = 6, .beat = 2}, 2, 7),
    };
    chart.fret_hand_positions = {
        placementAt(downbeat(1), 5),
        placementAt(downbeat(5), 12),
        placementAt(downbeat(8), 2),
        placementAt(GridPosition{.measure = 8, .beat = 2}, 4),
    };
    return chart;
}

// A chart with no notes and no placements, the nut window throughout.
[[nodiscard]] common::core::Chart makeBareChart()
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    return chart;
}

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
    // The keyboard stands at the paused cursor (measure 6, 10.0s); what a verb acts on is the
    // selected chip, the signature change at measure 5 (8.0s) that holds that measure.
    const std::optional<KeyboardPositionViewState>& position = editor.state().keyboard_position;
    REQUIRE(position.has_value());
    if (position.has_value())
    {
        CHECK(position->seconds == Catch::Approx(10.0));
        CHECK(position->measure_start_seconds == Catch::Approx(10.0));
        CHECK(position->measure_end_seconds == Catch::Approx(12.0));
    }
    const std::optional<double>& selection = editor.state().selection_start_seconds;
    REQUIRE(selection.has_value());
    if (selection.has_value())
    {
        CHECK(*selection == Catch::Approx(8.0));
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
    // Back on a string the keyboard stands at the caret's slot, and with nothing selected the
    // caret is also what a verb acts on.
    const std::optional<KeyboardPositionViewState>& landed = editor.state().keyboard_position;
    REQUIRE(landed.has_value());
    if (landed.has_value())
    {
        CHECK(landed->seconds == Catch::Approx(10.0));
    }
    CHECK(editor.state().selection_start_seconds == std::optional{10.0});
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
// What a verb acts on is the marker's START regardless — the chip — while the keyboard stands at
// the cursor; stepping off the marker brings the cursor to the chip first, so the next row's holder
// is the one found at that marker.
TEST_CASE("EditorController steps off a clicked chip from its start", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    const int seeks_before = editor.transport.seek_call_count;
    editor.controller.onSongSectionSelected(downbeat(7));
    CHECK(editor.transport.seek_call_count == seeks_before);
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
    CHECK(editor.state().selection_start_seconds == std::optional{12.0});
    const std::optional<KeyboardPositionViewState> clicked = editor.state().keyboard_position;
    REQUIRE(clicked.has_value());

    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(12.0));
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    // The tempo anchor holding 12.0s starts at measure 5: the chip a verb acts on.
    CHECK(editor.state().selection_start_seconds == std::optional{8.0});
    const std::optional<KeyboardPositionViewState> moved = editor.state().keyboard_position;
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
    // Passive with nothing selected, the keyboard stands at the paused cursor and no verb has a
    // selection to act on.
    const std::optional<KeyboardPositionViewState>& position = editor.state().keyboard_position;
    REQUIRE(position.has_value());
    if (position.has_value())
    {
        CHECK(position->seconds == Catch::Approx(3.0));
    }
    CHECK_FALSE(editor.state().selection_start_seconds.has_value());
}

// What a verb acts on is the CHIP for a row's first marker too. The walk lets that marker own the
// lead-in, but a cursor in the lead-in is not the chip.
TEST_CASE(
    "EditorController names a first chip clicked from the lead-in as the selection",
    "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.controller.onSongSectionSelected(downbeat(3));
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    CHECK(editor.state().selection_start_seconds == std::optional{4.0});

    // Stepping off it still keeps the cursor in the lead-in: the walk's holder rule is unchanged.
    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
}

// While the transport plays, playback follow owns the view, so neither position is published.
TEST_CASE("EditorController publishes no keyboard position while playing", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.controller.onSongSectionSelected(downbeat(3));
    REQUIRE(editor.state().keyboard_position.has_value());
    REQUIRE(editor.state().selection_start_seconds.has_value());

    // The fake transport never flips its own state; the press is only what publishes a new view.
    editor.transport.current_state.playing = true;
    editor.controller.onPlayPausePressed();
    CHECK_FALSE(editor.state().keyboard_position.has_value());
    CHECK_FALSE(editor.state().selection_start_seconds.has_value());
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
    // Nothing selected, no caret: no verb has anything to act on.
    CHECK_FALSE(editor.state().selection_start_seconds.has_value());
}

// Tab on a marker row steps from the CURSOR, as it does on every row: the column rule brings the
// cursor into the selected marker first, then the step goes to the start strictly beyond it. From
// the lead-in — the one place the selected marker's start lies AFTER the cursor, since a row's
// first marker owns whatever precedes it — the first Tab therefore lands on that marker's own
// start. Past either end the press is inert. Ctrl+Tab steps a marker row exactly as Tab does,
// since only a string has keyframes to step over.
TEST_CASE("EditorController steps a marker row to the neighbouring marker", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(1);
    editor.step(ChartStepDirection::Up, true);
    editor.step(ChartStepDirection::Up);
    editor.step(ChartStepDirection::Up);
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    REQUIRE(editor.transport.position().seconds == Catch::Approx(0.0));

    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK(editor.transport.position().seconds == Catch::Approx(4.0));

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

// Tab reads the cursor on a marker row (Phase 3 re-ruling), which is what makes a step from inside
// a marker land on that marker's OWN start first — the media player's "previous" — while a chip
// selected far from the cursor still steps from the CHIP, because the column rule brings the cursor
// into it before the step measures anything.
TEST_CASE("EditorController steps a marker row from the cursor", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeThreeMarkerSections()};

    SECTION("a step back from inside a section lands on its own start, then on the previous one")
    {
        editor.parkAtMeasure(9);
        editor.controller.onSongSectionSelected(downbeat(7));
        REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});

        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
        CHECK(editor.transport.position().seconds == Catch::Approx(12.0));

        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(4.0));
    }

    SECTION("a step forward from inside a section reaches the next section's start")
    {
        editor.parkAtMeasure(9);
        editor.controller.onSongSectionSelected(downbeat(7));

        editor.controller.onRowObjectStepRequested(true, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{2});
        CHECK(editor.transport.position().seconds == Catch::Approx(20.0));
    }

    SECTION("a chip selected far from the cursor steps forward from the chip")
    {
        // Reading the cursor where it stands would reach the FIRST section; the column rule moves
        // it into the selected chip first, so the step reaches the chip's later neighbour.
        editor.parkAtMeasure(1);
        editor.controller.onSongSectionSelected(downbeat(7));

        editor.controller.onRowObjectStepRequested(true, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{2});
        CHECK(editor.transport.position().seconds == Catch::Approx(20.0));
    }

    SECTION("a chip selected far from the cursor steps back from the chip")
    {
        // Reading the cursor where it stands would find nothing before it and do nothing at all.
        editor.parkAtMeasure(1);
        editor.controller.onSongSectionSelected(downbeat(7));

        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(4.0));
    }

    SECTION("a step past either end of the row is inert")
    {
        editor.parkAtMeasure(3);
        editor.controller.onSongSectionSelected(downbeat(3));
        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(4.0));

        editor.parkAtMeasure(11);
        editor.controller.onSongSectionSelected(downbeat(11));
        editor.controller.onRowObjectStepRequested(true, false);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{2});
        CHECK(editor.transport.position().seconds == Catch::Approx(20.0));
    }
}

// A Ctrl+Shift row jump is the walk's direct route: it lands through the same landing, so each
// ruler row selects the marker holding the cursor, the caret dissolves in place and the cursor does
// not move. A following Left/Right then re-arms on the string the caret last rode, which is why the
// string rows need no jump chord of their own.
TEST_CASE("EditorController jumps from the strings onto a ruler row", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(6);
    REQUIRE(editor.caretString() == std::optional{1});

    editor.jump(FocusRowJump::Section);
    CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.caretString().has_value());
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

    // Right from the passive marker arms in place on the remembered string, at the same slot.
    editor.step(ChartStepDirection::Right);
    CHECK(editor.caretString() == std::optional{1});
    CHECK_FALSE(editor.selectedSectionIndex().has_value());
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

    editor.jump(FocusRowJump::Tempo);
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    CHECK_FALSE(editor.caretString().has_value());
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

    editor.step(ChartStepDirection::Left);
    CHECK(editor.caretString() == std::optional{1});

    editor.jump(FocusRowJump::TimeSignature);
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});
    CHECK_FALSE(editor.caretString().has_value());
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

    // Pressing the same chord again re-lands on the row it already holds and changes nothing.
    editor.jump(FocusRowJump::TimeSignature);
    CHECK(editor.state().selected_time_signature_measure == std::optional{5});
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));
}

// The lead-in, the silence rule and the column rule, each on the jump: a row's first marker owns
// whatever precedes it, a row the stack does not list is not landed on at all, and a marker
// selected far from the cursor brings the cursor inside itself before the target row's holder is
// read — the same three rules the walk obeys, because the jump reuses its listing.
TEST_CASE("EditorController jumps by the walk's own row rules", "[core][marker-rows]")
{
    SECTION("the first section holds the lead-in")
    {
        MarkerRowEditor editor;
        editor.armAtMeasure(1);
        editor.jump(FocusRowJump::Section);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    }

    SECTION("a song with no sections lists no section row, so the jump is silent")
    {
        MarkerRowEditor editor{std::vector<SongSection>{}};
        editor.armAtMeasure(4);
        REQUIRE(editor.caretString() == std::optional{1});

        editor.jump(FocusRowJump::Section);
        CHECK_FALSE(editor.selectedSectionIndex().has_value());
        // The armed caret is left exactly as it was, not demoted by a landing that never happened.
        CHECK(editor.caretString() == std::optional{1});
        CHECK(editor.transport.position().seconds == Catch::Approx(6.0));

        // The rows the song does have are reached the same as ever.
        editor.jump(FocusRowJump::Tempo);
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
    }

    SECTION("a silent jump still brings the cursor into a chip selected elsewhere")
    {
        // The column rule runs before the stack is listed, for the jump as for every walk step, so
        // a press that then finds no row to land on has still moved the cursor — which is why the
        // handler publishes even when it selects nothing.
        MarkerRowEditor editor{std::vector<SongSection>{}};
        editor.parkAtMeasure(1);
        editor.controller.onTempoAnchorSelected(downbeat(5));
        REQUIRE(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
        REQUIRE(editor.transport.position().seconds == Catch::Approx(0.0));

        editor.jump(FocusRowJump::Section);
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
        CHECK(editor.transport.position().seconds == Catch::Approx(8.0));
    }

    SECTION("a passive cursor with nothing selected jumps from where it stands")
    {
        MarkerRowEditor editor;
        editor.parkAtMeasure(4);
        REQUIRE_FALSE(editor.caretString().has_value());

        editor.jump(FocusRowJump::Section);
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(6.0));
    }

    SECTION("a chip selected far from the cursor is where the target row is read")
    {
        // The cursor stands in the lead-in, where the tempo holder is the FIRST anchor; the column
        // rule moves it into the selected section first, so the jump reads the anchor there.
        MarkerRowEditor editor;
        editor.parkAtMeasure(1);
        editor.controller.onSongSectionSelected(downbeat(7));
        REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
        REQUIRE(editor.transport.position().seconds == Catch::Approx(0.0));

        editor.jump(FocusRowJump::Tempo);
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
        CHECK(editor.transport.position().seconds == Catch::Approx(12.0));
    }
}

// A marker chord typed while keyboard focus is already on that marker's own row still authors at
// the cursor column: the selected holder chip is only the row focus, not the chord's target.
TEST_CASE("Marker-row chords author at the cursor, not the holder", "[core][marker-rows]")
{
    SECTION("the section row inserts at a free cursor measure")
    {
        MarkerRowEditor editor;
        editor.armAtMeasure(4);
        editor.jump(FocusRowJump::Section);
        REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        REQUIRE(editor.transport.position().seconds == Catch::Approx(6.0));

        const SectionChordTarget target = editor.state().section_chord_target;
        const auto* const insert = std::get_if<InsertSectionTarget>(&target);
        REQUIRE(insert != nullptr);
        CHECK(insert->downbeat == downbeat(4));
    }

    SECTION("the tone row splits at a cursor inside the selected region")
    {
        MarkerRowEditor editor{makeMarkerSections(), makeMarkerToneRegions()};
        editor.armAtMeasure(6);
        editor.jump(FocusRowJump::Tone);
        REQUIRE(editor.selectedToneRegionId() == g_second_region_id);
        REQUIRE(editor.transport.position().seconds == Catch::Approx(10.0));

        const ToneChordTarget target = editor.state().tone_chord_target;
        const auto* const split = std::get_if<SplitToneRegionTarget>(&target);
        REQUIRE(split != nullptr);
        CHECK(split->position == downbeat(6));
        CHECK(split->containing_tone_document_ref == g_second_tone_ref);
    }
}

// The jumps are paused-only with the rest of the marker plane: arming requires a paused transport,
// and play clears the chart selection, so a jump while playing is refused by the action gate.
TEST_CASE("EditorController refuses a row jump while playing", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.armAtMeasure(6);
    REQUIRE(editor.caretString() == std::optional{1});

    editor.transport.current_state.playing = true;
    editor.jump(FocusRowJump::Section);
    CHECK_FALSE(editor.selectedSectionIndex().has_value());
    CHECK(editor.caretString() == std::optional{1});
}

// Enter and Ctrl+R are the SELECTION's verbs, published as the verb each selected kind has: a
// section restates and renames on its own name, while a tempo anchor and a time signature state no
// name at all and answer nothing to either — as does an empty selection.
TEST_CASE(
    "EditorController publishes the selection's restate and rename verbs", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    CHECK(std::holds_alternative<std::monostate>(editor.state().restate_target));
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));

    editor.controller.onSongSectionSelected(downbeat(3));
    const RestateTarget& section_restate = editor.state().restate_target;
    const auto* const restate_section = std::get_if<RenameSectionTarget>(&section_restate);
    REQUIRE(restate_section != nullptr);
    CHECK(restate_section->position == downbeat(3));
    CHECK(restate_section->name == "Verse");
    const RenameTarget& section_rename = editor.state().rename_target;
    const auto* const rename_section = std::get_if<RenameSectionTarget>(&section_rename);
    REQUIRE(rename_section != nullptr);
    CHECK(rename_section->position == downbeat(3));
    CHECK(rename_section->name == "Verse");

    editor.controller.onTempoAnchorSelected(downbeat(5));
    REQUIRE(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    CHECK(std::holds_alternative<std::monostate>(editor.state().restate_target));
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));

    editor.controller.onTimeSignatureSelected(5);
    REQUIRE(editor.state().selected_time_signature_measure == std::optional{5});
    CHECK(std::holds_alternative<std::monostate>(editor.state().restate_target));
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));
}

// The hand chord inserts at a free cursor at the fret the new placement's stretch defaults it to:
// the lowest stop the stretch holds, else the fret of the placement before it, else fret 1. The
// insert is left selected, and undo and redo round-trip it through the one marker commit.
TEST_CASE("The hand chord inserts at the cursor with a defaulted fret", "[core][marker-rows]")
{
    SECTION("the lowest stop the stretch holds")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.parkAtMeasure(6);
        editor.controller.onHandChordRequested();
        const std::vector<common::core::FretHandPosition> placements = editor.placements();
        REQUIRE(placements.size() == 5);
        CHECK(placements[2] == placementAt(downbeat(6), 7));
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
        CHECK(editor.state().selection_present);

        editor.controller.onUndoRequested();
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
        editor.controller.onRedoRequested();
        CHECK(editor.placements().size() == 5);
    }

    SECTION("an empty stretch keeps the fret of the placement before it")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.parkAtMeasure(3);
        editor.controller.onHandChordRequested();
        const std::vector<common::core::FretHandPosition> placements = editor.placements();
        REQUIRE(placements.size() == 5);
        CHECK(placements[1] == placementAt(downbeat(3), 5));
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    }

    SECTION("an empty chart takes the nut window")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
        editor.parkAtMeasure(4);
        editor.controller.onHandChordRequested();
        const std::vector<common::core::FretHandPosition> placements = editor.placements();
        REQUIRE(placements.size() == 1);
        CHECK(placements.front() == placementAt(downbeat(4), 1));
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
    }

    SECTION("the closing barline holds no placement, so the chord is inert there")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.controller.onChartCaretJumpRequested(ChartCaretJump::ChartEnd);
        REQUIRE(editor.caretString().has_value());
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onHandChordRequested();
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
    }
}

// On a start a placement already holds, the chord restates it: with no payload to re-enter, that
// only selects it — and it is what keeps a second press from inserting a duplicate there.
TEST_CASE("The hand chord restates the placement at the cursor", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.parkAtMeasure(5);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onHandChordRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));
}

// Delete removes the selected placement and selects the one before it, as Shift+Tab from the
// deleted start would; deleting the row's first leaves nothing selected; a chart may end with none.
TEST_CASE("EditorController deletes the selected fret-hand position", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(1);
    REQUIRE(editor.selectedHandIndex() == std::optional<std::size_t>{1});

    editor.controller.onSelectionDeleteRequested();
    const std::vector<common::core::FretHandPosition> placements = editor.placements();
    REQUIRE(placements.size() == 3);
    CHECK(placements[1] == placementAt(downbeat(8), 2));
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
    CHECK(editor.state().selection_present);

    // The row's first has nothing before it, so nothing stays selected.
    editor.controller.onSelectionDeleteRequested();
    REQUIRE(editor.placements().size() == 2);
    CHECK_FALSE(editor.selectedHandIndex().has_value());
    CHECK_FALSE(editor.state().selection_present);
    editor.controller.onUndoRequested();

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);

    MarkerRowEditor sole{makeMarkerSections(), {}, makeBareChart()};
    sole.parkAtMeasure(2);
    sole.controller.onHandChordRequested();
    REQUIRE(sole.placements().size() == 1);
    sole.controller.onSelectionDeleteRequested();
    CHECK(sole.placements().empty());
    CHECK_FALSE(sole.selectedHandIndex().has_value());
}

// Alt+arrows step the selected placement one placement-quantum line, keep it selected, and bring
// the cursor to where it landed.
TEST_CASE("EditorController moves the selected fret-hand position", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(1);

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    const std::vector<common::core::FretHandPosition> placements = editor.placements();
    REQUIRE(placements.size() == 4);
    CHECK(placements[1] == placementAt(GridPosition{.measure = 5, .beat = 2}, 12));
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    // Measure 5 is 3/4 at 90 BPM, so its second beat sits two thirds of a second past 8.0s.
    CHECK(editor.transport.position().seconds == Catch::Approx(8.0 + 2.0 / 3.0));

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
}

// Undo and redo bring the placement they change into focus — selected, with the cursor at its start
// — even after the selection moved on to another placement.
TEST_CASE("EditorController brings an undone placement move into focus", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(1);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    editor.controller.onFretHandPositionSelected(3);
    REQUIRE(editor.selectedHandIndex() == std::optional<std::size_t>{3});

    // Back at the measure-5 downbeat, 8.0s.
    editor.controller.onUndoRequested();
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(8.0));

    // Measure 5 is 3/4 at 90 BPM, so its second beat sits two thirds of a second past 8.0s.
    editor.controller.onRedoRequested();
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(8.0 + 2.0 / 3.0));
}

// A move onto a start another placement holds, off the chart's start, or onto the song's closing
// barline is refused whole: the stream, the selection and the history stay exactly as they were.
TEST_CASE("Fret-hand move refuses an occupied start and both chart ends", "[core][marker-rows]")
{
    // The hand chart plus a placement on the last beat before the closing barline (21:1, the
    // marker tempo map's terminal anchor; measure 20 is 3/4).
    common::core::Chart chart = makeHandChart();
    chart.fret_hand_positions.push_back(placementAt(GridPosition{.measure = 20, .beat = 3}, 5));
    const std::vector<common::core::FretHandPosition> stream = chart.fret_hand_positions;
    MarkerRowEditor editor{makeMarkerSections(), {}, std::move(chart)};
    editor.controller.onFretHandPositionSelected(2);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
    CHECK(editor.undoEntryCount() == entries_before);

    editor.controller.onFretHandPositionSelected(0);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
    CHECK(editor.undoEntryCount() == entries_before);

    editor.controller.onFretHandPositionSelected(4);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{4});
    CHECK(editor.undoEntryCount() == entries_before);
}

// The hand row sits between the time signature and the strings: the jump lands on the placement
// holding the cursor exactly as the ruler jumps land, and the walk passes through the row. With no
// placements the row is not listed, so the jump is silent and the walk goes straight to the ruler.
TEST_CASE("EditorController jumps onto and walks through the hand row", "[core][marker-rows]")
{
    SECTION("the jump selects the holder and the walk passes through the row")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.armAtMeasure(6);
        REQUIRE(editor.caretString() == std::optional{1});

        editor.jump(FocusRowJump::Hand);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
        CHECK_FALSE(editor.caretString().has_value());
        CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

        editor.step(ChartStepDirection::Up);
        CHECK(editor.state().selected_time_signature_measure == std::optional{5});
        editor.step(ChartStepDirection::Down);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
        editor.step(ChartStepDirection::Down);
        CHECK(editor.caretString() == std::optional{6});
    }

    SECTION("a chart with no placements lists no hand row")
    {
        MarkerRowEditor editor;
        editor.armAtMeasure(4);
        REQUIRE(editor.caretString() == std::optional{1});

        editor.jump(FocusRowJump::Hand);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
        CHECK(editor.caretString() == std::optional{1});
    }
}

// A click on a hand chip reaches the same select the keyboard does: it selects the same marker and
// publishes the same verbs on it.
TEST_CASE("A hand chip click selects what the hand-row jump selects", "[core][marker-rows]")
{
    MarkerRowEditor keyboard{makeMarkerSections(), {}, makeHandChart()};
    keyboard.armAtMeasure(6);
    keyboard.jump(FocusRowJump::Hand);
    REQUIRE(keyboard.selectedHandIndex() == std::optional<std::size_t>{1});

    MarkerRowEditor pointer{makeMarkerSections(), {}, makeHandChart()};
    pointer.armAtMeasure(6);
    pointer.controller.onFretHandPositionSelected(1);
    CHECK(pointer.selectedHandIndex() == keyboard.selectedHandIndex());
    CHECK(pointer.state().restate_target == keyboard.state().restate_target);
    CHECK_FALSE(pointer.caretString().has_value());
    CHECK(pointer.transport.position().seconds == Catch::Approx(10.0));

    // An index naming no placement selects nothing.
    pointer.controller.onFretHandPositionSelected(9);
    CHECK(pointer.selectedHandIndex() == std::optional<std::size_t>{1});
}

// A selected placement is one more operand the digits retype: its fret. One typed value is one
// undo entry, the placement stays selected, and undo restores the fret it had.
TEST_CASE("A digit retypes the selected fret-hand position's fret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(1);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onChartFretDigitTyped(5);
    REQUIRE(editor.placements().size() == 4);
    CHECK(editor.placements()[1] == placementAt(downbeat(5), 5));
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK_FALSE(editor.state().chart_edit.pending_fret.has_value());

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
}

// The placement's entry is the note retype's entry: the first digit decides, a second inside the
// window widens it, and while it waits its box rides the placement's chip.
TEST_CASE("Two digits in the window compose a fret-hand position's fret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(0);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onChartFretDigitTyped(1);
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    const std::optional<ChartPendingFretViewState>& pending =
        editor.state().chart_edit.pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "1");
        CHECK(pending->valid);
        CHECK(pending->at == decltype(pending->at){ChartPendingFretHandPosition{.index = 0}});
    }
    // The value is previewed live, as a creation is: the published chip already states fret 1.
    const std::shared_ptr<const common::core::ChartViewState>& tab = editor.state().tab;
    REQUIRE(tab != nullptr);
    REQUIRE_FALSE(tab->fret_hand_positions.empty());
    CHECK(tab->fret_hand_positions.front().fret == 1);

    editor.controller.onChartFretDigitTyped(2);
    CHECK(editor.placements()[0] == placementAt(downbeat(1), 12));
    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
}

// A fret the board cannot hold — the open string, or an index finger too high for the narrowest
// window — is refused by the stream's own rules: the box goes red, and the stream, the history and
// the selection stay exactly as they were.
TEST_CASE("A fret-hand position refuses a fret the board cannot hold", "[core][marker-rows]")
{
    SECTION("fret 0")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.controller.onFretHandPositionSelected(1);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onChartFretDigitTyped(0);
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK_FALSE(pending->valid);
        }
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    }

    SECTION("past the board")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.controller.onFretHandPositionSelected(1);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onChartFretDigitTyped(2);
        editor.controller.onChartFretDigitTyped(3);
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK(pending->text == "23");
            CHECK_FALSE(pending->valid);
        }
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    }
}

// No ring reaches a placement, so the ring plane's digit does exactly what the bare one does, and
// the `Insert` chords, which need an armed caret, do nothing while a placement holds the selection.
TEST_CASE("The ring plane over a fret-hand position is the bare digit", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.controller.onFretHandPositionSelected(1);

    editor.controller.onRingPointInsertRequested();
    editor.controller.onInsertAtCaretRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});

    editor.controller.onChartRingDigitTyped(5);
    CHECK(editor.placements()[1] == placementAt(downbeat(5), 5));
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
}

// With no placement selected a digit keeps its caret-slot meaning: it types a note there and leaves
// the placements alone.
TEST_CASE("A digit with no placement selected still types at the caret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.armAtMeasure(4);

    editor.controller.onChartFretDigitTyped(5);
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    const common::core::Arrangement* const arrangement =
        editor.controller.session().currentArrangement();
    REQUIRE(arrangement != nullptr);
    if (arrangement == nullptr)
    {
        return;
    }
    const std::optional<common::core::Chart>& chart = arrangement->chart;
    REQUIRE(chart.has_value());
    if (chart.has_value())
    {
        CHECK(std::ranges::any_of(chart->notes, [](const common::core::ChartNote& note) {
            return note.position == downbeat(4) && note.string == 1 && note.fret == 5;
        }));
    }
}

} // namespace rock_hero::editor::core
