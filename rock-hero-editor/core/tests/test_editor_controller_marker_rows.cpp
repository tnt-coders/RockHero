#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
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

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::downbeat;

using common::core::GridPosition;
using common::core::SongSection;

constexpr const char* g_first_region_id = "10101010-1111-4111-8111-111111111111";
constexpr const char* g_second_region_id = "20202020-2222-4222-8222-222222222222";
constexpr const char* g_first_tone_ref = "tone-clean";
constexpr const char* g_second_tone_ref = "tone-drive";

// Two tempo spans and two meters whose measures all last two seconds: 4/4 at 120 BPM through
// measure 4, then 3/4 at 90 BPM from measure 5, so measure N's downbeat sits at (N - 1) * 2 seconds
// on both sides of the change. The ruler rows each carry two markers: tempo anchors at 1:1 and
// 5:1, signatures at measures 1 and 5.
[[nodiscard]] common::core::TempoMap makeMarkerTempoMap()
{
    return common::core::TempoMap{
        common::core::testing::metersAt({{"1:1", 4, 4}, {"5:1", 3, 4}}),
        std::vector{
            anchor("1:1", 0),
            anchor("5:1", 8000),
            anchor("21:1", 40000),
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

    // The index of the lane chip fronting at a position, or nothing (fretHandChipAt).
    [[nodiscard]] std::optional<std::size_t> handChipAt(const GridPosition& position) const
    {
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        return tab == nullptr ? std::nullopt : fretHandChipAt(*tab, position);
    }

    // Clicks the lane chip of the placement at `index` in the chart's list.
    void clickPlacement(const std::size_t index)
    {
        const std::optional<std::size_t> chip = handChipAt(placements().at(index).position);
        REQUIRE(chip.has_value());
        if (chip.has_value())
        {
            controller.onFretHandPositionSelected(*chip);
        }
    }

    // Clicks the lane chip fronting at a position, derived or authored.
    void clickChipAt(const GridPosition& position)
    {
        const std::optional<std::size_t> chip = handChipAt(position);
        REQUIRE(chip.has_value());
        if (chip.has_value())
        {
            controller.onFretHandPositionSelected(*chip);
        }
    }

    // The index of the lane chip outlined as selected, derived or authored, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedChip() const
    {
        return state().chart_edit.selected_fret_hand_position;
    }

    // What the chart states at the lane chip fronting at a position; nothing is a derived chip.
    [[nodiscard]] common::core::FhpAuthored chipAuthored(const GridPosition& position) const
    {
        const std::optional<std::size_t> chip = handChipAt(position);
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        REQUIRE(chip.has_value());
        REQUIRE(tab != nullptr);
        if (!chip.has_value() || tab == nullptr)
        {
            throw std::logic_error("no chip fronts there");
        }
        return tab->fret_hand_positions.at(*chip).authored;
    }

    // The index in the chart's list of the placement whose chip the lane outlines as selected, or
    // nothing.
    [[nodiscard]] std::optional<std::size_t> selectedHandIndex() const
    {
        const std::optional<std::size_t> chip = state().chart_edit.selected_fret_hand_position;
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        if (!chip.has_value() || tab == nullptr || *chip >= tab->fret_hand_positions.size())
        {
            return std::nullopt;
        }
        const std::vector<common::core::FretHandPosition> stream = placements();
        const auto found = std::ranges::find(
            stream,
            tab->fret_hand_positions[*chip].position,
            &common::core::FretHandPosition::position);
        if (found == stream.end())
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::distance(stream.begin(), found));
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

    // The loaded chart's notes as they now stand.
    [[nodiscard]] std::vector<common::core::ChartNote> notes() const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        REQUIRE(chart != nullptr);
        if (chart == nullptr)
        {
            throw std::logic_error("no chart is loaded");
        }
        return chart->notes;
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

// The hand chart: two short notes in measure 6 on frets 9 and 7, and four placements — fret 5 at
// 1:1, fret 12 at 5:1, and frets 2 and 4 on the first two beats of measure 8. Measure 6's notes lie
// outside the 5:1 window, so the derivation puts a window of its own on fret 7 there: the lane's
// chips are the four placements with that derived one between the second and the third. 8:2 is a
// start one quantum past another.
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

// The derived hand chart: one short note on each of the downbeats of measures 2, 3 and 4, on frets
// 3, 8 and 1, and no stored position. Each bar line breaks the run and each fret lies outside the
// window before it, so the notes alone front a window at each of the three downbeats.
[[nodiscard]] common::core::Chart makeDerivedHandChart()
{
    common::core::Chart chart = makeBareChart();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 3, .beat = 1}, 2, 8),
        makeTestNote({.measure = 4, .beat = 1}, 3, 1),
    };
    return chart;
}

} // namespace

// Up off the top string walks the ruler's rows by selection — time signature, tempo, section — and
// each lands on the marker holding the cursor, with the caret disarmed and the cursor left where it
// stood. Down retraces them and re-arms on the top string at the same slot. The chart has no hand
// event, so no position row sits between the strings and the ruler.
TEST_CASE("EditorController walks up from the strings onto the ruler rows", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
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
    // A chip is a selection whether or not its kind has a Delete; the core declines what it lacks.
    CHECK(editor.state().selection_present);
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
// the plain step coincide. The chart has no hand event, so no position row stands between.
TEST_CASE("EditorController reaches between the ruler rows and the strings", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
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
// With no sections at all the row is absent and Up from the tempo row is inert. The chart has no
// hand event, so the reach from the strings lands on the ruler.
TEST_CASE("EditorController gives the lead-in to a row's first marker", "[core][marker-rows]")
{
    SECTION("the first section holds the lead-in")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
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
        MarkerRowEditor editor{std::vector<SongSection>{}, {}, makeBareChart()};
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

// The measure-1 time signature refuses Delete and Alt+arrows, leaving the tempo map untouched and
// the chip selected; Esc releases it, and a cursor move releases a tempo chip like every marker
// selection.
TEST_CASE(
    "EditorController keeps the first meter and releases chip selections", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    const common::core::TempoMap before = editor.controller.session().song().tempo_map;

    editor.controller.onTimeSignatureSelected(1);
    REQUIRE(editor.state().selected_time_signature_measure == std::optional{1});
    editor.controller.onSelectionDeleteRequested();
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.controller.session().song().tempo_map == before);
    CHECK(editor.state().selected_time_signature_measure == std::optional{1});

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

    // Stepping off it still keeps the cursor in the lead-in, which the walk's holder rule gives to
    // the first section.
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
// since only a string has keyframes to step over. The chart has no hand event, so the reach from
// the strings lands on the ruler.
TEST_CASE("EditorController steps a marker row to the neighbouring marker", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
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

// Tab reads the cursor on a marker row, which is what makes a step from inside a marker land on
// that marker's OWN start first — the media player's "previous" — while a chip selected far from
// the cursor still steps from the CHIP, because the column rule brings the cursor into it before
// the step measures anything.
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

        // The rows the song does have are still reachable.
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
// section restates and renames on its own name, a tempo anchor restates on the tempo of the span it
// opens and has no name to rename, a time signature restates on its meter and has no name either,
// and an empty selection answers nothing to both.
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
    const RestateTarget& tempo_restate = editor.state().restate_target;
    const auto* const restate_tempo = std::get_if<SetTempoTarget>(&tempo_restate);
    REQUIRE(restate_tempo != nullptr);
    CHECK(restate_tempo->position == downbeat(5));
    CHECK(restate_tempo->bpm_text == "90");
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));

    editor.controller.onTimeSignatureSelected(5);
    REQUIRE(editor.state().selected_time_signature_measure == std::optional{5});
    const RestateTarget& meter_restate = editor.state().restate_target;
    const auto* const restate_meter = std::get_if<SetTimeSignatureTarget>(&meter_restate);
    REQUIRE(restate_meter != nullptr);
    CHECK(*restate_meter == SetTimeSignatureTarget{.measure = 5, .meter_text = "3/4"});
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));
}

// The fret-hand position chord inserts at a free cursor stating the fret of the window already
// standing there — the last hand event at or before the cursor, derived or authored — else the nut
// window. The insert is left selected, and undo and redo round-trip it through the one marker
// commit.
TEST_CASE(
    "The fret-hand position chord inserts at the cursor with a defaulted fret",
    "[core][marker-rows]")
{
    SECTION("the derived window standing at the cursor")
    {
        // The 5:1 position's window ends where measure 6's notes leave it, and the window they
        // derive, on fret 7, stands through measure 7.
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.parkAtMeasure(7);
        editor.controller.onFretHandPositionChordRequested();
        const std::vector<common::core::FretHandPosition> placements = editor.placements();
        REQUIRE(placements.size() == 5);
        CHECK(placements[2] == placementAt(downbeat(7), 7));
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
        CHECK(editor.state().selection_present);

        editor.controller.onUndoRequested();
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
        editor.controller.onRedoRequested();
        CHECK(editor.placements().size() == 5);
    }

    SECTION("the authored window standing at the cursor")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.parkAtMeasure(3);
        editor.controller.onFretHandPositionChordRequested();
        const std::vector<common::core::FretHandPosition> placements = editor.placements();
        REQUIRE(placements.size() == 5);
        CHECK(placements[1] == placementAt(downbeat(3), 5));
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    }

    SECTION("an empty chart takes the nut window")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
        editor.parkAtMeasure(4);
        editor.controller.onFretHandPositionChordRequested();
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

        editor.controller.onFretHandPositionChordRequested();
        CHECK(editor.placements() == makeHandChart().fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
    }
}

namespace
{

// One open string bent behind the nut at 3:1 under `capo`: it demands the first playable fret of
// the hand, and nothing else sounds.
[[nodiscard]] common::core::Chart makeBehindTheNutBendChart(const int capo)
{
    common::core::Chart chart = makeBareChart();
    chart.tuning.capo = capo;
    common::core::ChartNote bent = makeTestNote(downbeat(3), 1, 0);
    bent.bend = 1.0;
    chart.notes = {std::move(bent)};
    return chart;
}

} // namespace

// Behind the nut under a capo is behind the capo, so a behind-the-nut bend demands the first
// playable fret: the window the derivation stands there, and the chord's copy of it, cover the
// bend, and the insert lands on capo + 1.
TEST_CASE(
    "The fret-hand position chord lands at a behind-the-nut bend under a capo",
    "[core][marker-rows]")
{
    for (const int capo : {0, 2})
    {
        CAPTURE(capo);
        MarkerRowEditor editor{makeMarkerSections(), {}, makeBehindTheNutBendChart(capo)};
        const std::optional<std::size_t> chip = editor.handChipAt(downbeat(3));
        REQUIRE(chip.has_value());
        REQUIRE(editor.state().tab != nullptr);
        if (chip.has_value() && editor.state().tab != nullptr)
        {
            CHECK(editor.state().tab->fret_hand_positions.at(*chip).fret == capo + 1);
        }
        editor.parkAtMeasure(3);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onFretHandPositionChordRequested();
        CHECK(editor.placements() == std::vector{placementAt(downbeat(3), capo + 1)});
        CHECK(editor.undoEntryCount() == entries_before + 1);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
    }
}

// On a start a placement already holds, the chord restates it: with no payload to re-enter, that
// only selects it — and it is what keeps a second press from inserting a duplicate there.
TEST_CASE(
    "The fret-hand position chord restates the placement at the cursor", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.parkAtMeasure(5);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onFretHandPositionChordRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(std::holds_alternative<std::monostate>(editor.state().rename_target));
}

// Delete removes the selected placement and selects the one before it; deleting the row's first
// leaves nothing selected; a chart may end with none.
TEST_CASE("EditorController deletes the selected fret-hand position", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(1);
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
    sole.controller.onFretHandPositionChordRequested();
    REQUIRE(sole.placements().size() == 1);
    sole.controller.onSelectionDeleteRequested();
    CHECK(sole.placements().empty());
    CHECK_FALSE(sole.selectedHandIndex().has_value());
}

// Delete selects the marker before the one it removed as a chip click selects it, seeking nothing:
// on every marker row the cursor stays where the charter was working, however far back that marker
// stands. Tab still steps from the selected chip, because the column
// rule brings the cursor into it first. The tone row's case lives with the tone-region tests, whose
// fixture carries the tones a region delete needs.
TEST_CASE(
    "Deleting a marker selects the previous one without moving the cursor", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeThreeMarkerSections(), {}, makeHandChart()};
    // Measure 12, 22.0s: past every marker the cases below delete.
    editor.parkAtMeasure(12);
    constexpr double cursor_seconds = 22.0;

    SECTION("a section")
    {
        editor.controller.onSongSectionSelected(downbeat(11));
        editor.controller.onSelectionDeleteRequested();
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
        CHECK(editor.transport.position().seconds == Catch::Approx(cursor_seconds));

        // A run of deletes walks back along the row with the cursor still standing.
        editor.controller.onSelectionDeleteRequested();
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
        CHECK(editor.transport.position().seconds == Catch::Approx(cursor_seconds));
    }

    SECTION("a fret-hand position, and Tab still steps from the selected chip")
    {
        // The row's marker before 8:1 is the window measure 6 derives.
        editor.clickPlacement(2);
        editor.controller.onSelectionDeleteRequested();
        CHECK(editor.selectedChip() == editor.handChipAt(downbeat(6)));
        CHECK(editor.transport.position().seconds == Catch::Approx(cursor_seconds));

        // The cursor stands under the 8:2 placement, not the selected 6:1 chip, so the step reads
        // the chip: Shift+Tab from 6:1 lands on 5:1.
        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
        CHECK(editor.transport.position().seconds == Catch::Approx(8.0));
    }

    SECTION("a tempo anchor")
    {
        editor.controller.onTempoAnchorSelected(downbeat(5));
        editor.controller.onSelectionDeleteRequested();
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
        // The map changed under the cursor, which keeps its musical place (measure 12) rather than
        // its seconds; it is not brought back to the 1:1 anchor at 0.0s.
        const double measure_12 =
            editor.controller.session().song().tempo_map.secondsAt(downbeat(12));
        CHECK(editor.transport.position().seconds == Catch::Approx(measure_12));
        CHECK(measure_12 > 20.0);
    }
}

// Delete on a row's FIRST marker leaves nothing selected and the keyboard where it stood, and no
// marker Delete asks the view to keep anything in sight (focus_count): it only selects, as a chip
// click does, so the view does not centre on the deleted chip at the song's opening. The tempo
// row's first anchor refuses the delete, so its selection, and the view, simply stay. The ruler
// menu's Delete is this same intent. The tone row's case lives with the tone-region tests.
TEST_CASE(
    "Deleting a row's first marker leaves the cursor and the view where they were",
    "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.parkAtMeasure(12);
    const auto keyboard_seconds = [&editor]() -> std::optional<double> {
        const std::optional<KeyboardPositionViewState>& position = editor.state().keyboard_position;
        return position.has_value() ? std::optional{position->seconds} : std::nullopt;
    };
    const std::optional<double> parked = keyboard_seconds();
    REQUIRE(parked.has_value());
    const std::uint64_t focused = editor.state().focus_count;

    const auto delete_marker = [&editor, focused]() {
        editor.controller.onSelectionDeleteRequested();
        CHECK(editor.transport.position().seconds == Catch::Approx(22.0));
        CHECK(editor.state().focus_count == focused);
    };

    SECTION("a section")
    {
        editor.controller.onSongSectionSelected(downbeat(3));
        delete_marker();
        CHECK_FALSE(editor.selectedSectionIndex().has_value());
        CHECK_FALSE(editor.state().selection_start_seconds.has_value());
    }

    SECTION("a fret-hand position")
    {
        editor.clickPlacement(0);
        delete_marker();
        CHECK_FALSE(editor.selectedHandIndex().has_value());
        CHECK_FALSE(editor.state().selection_start_seconds.has_value());
    }

    SECTION("a section with one before it")
    {
        editor.controller.onSongSectionSelected(downbeat(7));
        delete_marker();
        CHECK(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    }

    SECTION("the tempo row's first anchor, which refuses")
    {
        editor.controller.onTempoAnchorSelected(downbeat(1));
        delete_marker();
        CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(1)});
    }

    CHECK(keyboard_seconds() == parked);
}

// Alt+arrows step the selected placement one placement-quantum line, keep it selected, and bring
// the cursor to where it landed.
TEST_CASE("EditorController moves the selected fret-hand position", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(1);

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
    editor.clickPlacement(1);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    editor.clickPlacement(3);
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
    editor.clickPlacement(2);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
    CHECK(editor.undoEntryCount() == entries_before);

    editor.clickPlacement(0);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
    CHECK(editor.undoEntryCount() == entries_before);

    editor.clickPlacement(4);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.placements() == stream);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{4});
    CHECK(editor.undoEntryCount() == entries_before);
}

// The position row sits between the time signature and the strings: the jump lands on the position
// holding the cursor — here the window measure 6 derives — exactly as the ruler jumps land, and the
// walk passes through the row. With no hand event at all the row is not listed, so the jump is
// silent and the walk goes straight to the ruler.
TEST_CASE("EditorController jumps onto and walks through the position row", "[core][marker-rows]")
{
    SECTION("the jump selects the holder and the walk passes through the row")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
        editor.armAtMeasure(6);
        REQUIRE(editor.caretString() == std::optional{1});
        const std::optional<std::size_t> derived = editor.handChipAt(downbeat(6));
        REQUIRE(derived.has_value());

        editor.jump(FocusRowJump::FretHandPosition);
        CHECK(editor.selectedChip() == derived);
        CHECK_FALSE(editor.caretString().has_value());
        CHECK(editor.transport.position().seconds == Catch::Approx(10.0));

        editor.step(ChartStepDirection::Up);
        CHECK(editor.state().selected_time_signature_measure == std::optional{5});
        editor.step(ChartStepDirection::Down);
        CHECK(editor.selectedChip() == derived);
        editor.step(ChartStepDirection::Down);
        CHECK(editor.caretString() == std::optional{6});
    }

    SECTION("a chart with no hand events lists no position row")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeBareChart()};
        editor.armAtMeasure(4);
        REQUIRE(editor.caretString() == std::optional{1});

        editor.jump(FocusRowJump::FretHandPosition);
        CHECK_FALSE(editor.selectedHandIndex().has_value());
        CHECK(editor.caretString() == std::optional{1});
    }
}

// A click on a hand chip reaches the same select the keyboard does: it selects the same marker and
// publishes the same verbs on it.
TEST_CASE("A hand chip click selects what the position-row jump selects", "[core][marker-rows]")
{
    MarkerRowEditor keyboard{makeMarkerSections(), {}, makeHandChart()};
    keyboard.armAtMeasure(6);
    keyboard.jump(FocusRowJump::FretHandPosition);
    REQUIRE(keyboard.selectedChip() == keyboard.handChipAt(downbeat(6)));

    MarkerRowEditor pointer{makeMarkerSections(), {}, makeHandChart()};
    pointer.armAtMeasure(6);
    pointer.clickChipAt(downbeat(6));
    CHECK(pointer.selectedChip() == keyboard.selectedChip());
    CHECK(pointer.state().restate_target == keyboard.state().restate_target);
    CHECK_FALSE(pointer.caretString().has_value());
    CHECK(pointer.transport.position().seconds == Catch::Approx(10.0));

    // An index naming no chip selects nothing.
    pointer.controller.onFretHandPositionSelected(9);
    CHECK(pointer.selectedChip() == keyboard.selectedChip());
}

// Every chip is a row object: a click on a derived chip selects it as a click on an authored one
// does, each outlined on its own chip, and a pending fret entry on the authored one rides its chip
// in the projection the entry previews. The authored chip draws solid, the derived ones as ghosts.
TEST_CASE("A click selects a derived chip and an authored one alike", "[core][marker-rows]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 3, .beat = 1}, 2, 8),
        makeTestNote({.measure = 4, .beat = 1}, 3, 1),
    };
    chart.fret_hand_positions = {placementAt(downbeat(3), 8)};
    MarkerRowEditor editor{makeMarkerSections(), {}, std::move(chart)};
    const std::shared_ptr<const common::core::ChartViewState> tab = editor.state().tab;
    REQUIRE(tab != nullptr);
    REQUIRE(tab->fret_hand_positions.size() == 3);
    CHECK(tab->fret_hand_positions[0].position == downbeat(2));
    CHECK(tab->fret_hand_positions[1].position == downbeat(3));
    CHECK(tab->fret_hand_positions[2].position == downbeat(4));
    CHECK(tab->fret_hand_positions[0].authored == common::core::FhpAuthored::Nothing);
    CHECK(tab->fret_hand_positions[1].authored == common::core::FhpAuthored::Fret);
    CHECK(tab->fret_hand_positions[2].authored == common::core::FhpAuthored::Nothing);

    editor.controller.onFretHandPositionSelected(0);
    CHECK(editor.state().chart_edit.selected_fret_hand_position == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.selectedHandIndex().has_value());
    CHECK(editor.state().selection_present);
    editor.controller.onFretHandPositionSelected(2);
    CHECK(editor.state().chart_edit.selected_fret_hand_position == std::optional<std::size_t>{2});
    // Selecting it demotes the caret and seeks nothing, as every marker click does.
    CHECK_FALSE(editor.caretString().has_value());

    editor.controller.onFretHandPositionSelected(1);
    CHECK(editor.state().chart_edit.selected_fret_hand_position == std::optional<std::size_t>{1});
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});

    // A first digit waits for a second; the box rides the authored chip, which the preview of fret
    // 1 still draws second, after the derived window before it.
    editor.controller.onChartFretDigitTyped(1);
    const std::optional<ChartPendingFretViewState>& pending =
        editor.state().chart_edit.pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        const auto* const chip = std::get_if<ChartPendingFretHandPosition>(&pending->at);
        REQUIRE(chip != nullptr);
        CHECK(chip->index == 1);
    }
    const std::shared_ptr<const common::core::ChartViewState> preview = editor.state().tab;
    REQUIRE(preview != nullptr);
    REQUIRE(preview->fret_hand_positions.size() >= 2);
    CHECK(preview->fret_hand_positions[1].position == downbeat(3));
    CHECK(preview->fret_hand_positions[1].fret == 1);
}

// A selected placement is one more operand the digits retype: its fret. One typed value is one
// undo entry, the placement stays selected, and undo restores the fret it had.
TEST_CASE("A digit retypes the selected fret-hand position's fret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(1);
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
    editor.clickPlacement(0);
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
        const auto* const chip = std::get_if<ChartPendingFretHandPosition>(&pending->at);
        REQUIRE(chip != nullptr);
        CHECK(chip->index == 0);
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
        editor.clickPlacement(1);
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
        editor.clickPlacement(1);
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

// The ring plane states the far end of what is selected: over a placement, its window's END fret,
// as one undo entry, the placement staying selected, and the chip reading the authored range. The
// `Insert` chords, which need an armed caret, do nothing while a placement holds the selection.
TEST_CASE("The ring plane types a fret-hand position's end fret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(1);

    editor.controller.onRingPointInsertRequested();
    editor.controller.onInsertAtCaretRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});

    const std::size_t entries_before = editor.undoEntryCount();
    editor.controller.onChartRingDigitTyped(1);
    editor.controller.onChartRingDigitTyped(4);
    REQUIRE(editor.placements().size() == 4);
    common::core::FretHandPosition expected = placementAt(downbeat(5), 12);
    expected.end_fret = 14;
    CHECK(editor.placements()[1] == expected);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK(editor.state().undo_history.labels.back() == "Set Hand Position End Fret");
    const std::shared_ptr<const common::core::ChartViewState>& tab = editor.state().tab;
    REQUIRE(tab != nullptr);
    const std::optional<std::size_t> chip = editor.handChipAt(downbeat(5));
    REQUIRE(chip.has_value());
    if (chip.has_value())
    {
        CHECK(tab->fret_hand_positions[*chip].width == 3);
        CHECK(tab->fret_hand_positions[*chip].authored == common::core::FhpAuthored::FretAndEnd);
    }

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
}

// Retyping the start keeps an authored end where it is, and a start retyped above that end is
// refused like any other illegal fret, its box carrying the chip the typed value would have made so
// the refused half is readable in the chip's own notation; an end typed below the start is refused
// the same way.
TEST_CASE(
    "A fret-hand position's two frets refuse a window reaching nowhere", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(0);
    editor.controller.onChartRingDigitTyped(7);
    common::core::FretHandPosition expected = placementAt(downbeat(1), 5);
    expected.end_fret = 7;
    REQUIRE(editor.placements()[0] == expected);

    editor.controller.onChartFretDigitTyped(6);
    expected.fret = 6;
    CHECK(editor.placements()[0] == expected);
    CHECK(editor.state().undo_history.labels.back() == "Set Hand Position Start Fret");

    const std::size_t entries_before = editor.undoEntryCount();
    editor.controller.onChartFretDigitTyped(8);
    {
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK(pending->text == "8");
            CHECK_FALSE(pending->valid);
            const auto* const chip = std::get_if<ChartPendingFretHandPosition>(&pending->at);
            REQUIRE(chip != nullptr);
            if (chip != nullptr)
            {
                CHECK(chip->index == 0);
                CHECK(chip->typed.fret == 8);
                CHECK(common::core::FretWindow{.fret = 8, .width = chip->typed.width}.top() == 7);
                CHECK(chip->typed.authored == common::core::FhpAuthored::FretAndEnd);
            }
        }
    }
    CHECK(editor.placements()[0] == expected);
    CHECK(editor.undoEntryCount() == entries_before);

    editor.controller.onChartEscapePressed();
    editor.clickPlacement(1);
    editor.controller.onChartRingDigitTyped(9);
    {
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK(pending->text == "9");
            CHECK_FALSE(pending->valid);
            const auto* const chip = std::get_if<ChartPendingFretHandPosition>(&pending->at);
            REQUIRE(chip != nullptr);
            if (chip != nullptr)
            {
                CHECK(chip->typed.fret == 12);
                CHECK(common::core::FretWindow{.fret = 12, .width = chip->typed.width}.top() == 9);
                CHECK(chip->typed.authored == common::core::FhpAuthored::FretAndEnd);
            }
        }
    }
    CHECK(editor.placements()[1] == placementAt(downbeat(5), 12));
    CHECK(editor.undoEntryCount() == entries_before);
}

// Alt+Delete clears an authored end back to the derived window as one undo entry, the placement
// staying selected; over an end that is already derived it does nothing at all — no undo entry,
// and never the placement's deletion, which is Delete's.
TEST_CASE("Alt+Delete clears a fret-hand position's authored end fret", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(1);

    const std::size_t entries_before = editor.undoEntryCount();
    editor.controller.onFretHandPositionEndClearRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});

    editor.controller.onChartRingDigitTyped(1);
    editor.controller.onChartRingDigitTyped(4);
    REQUIRE(editor.placements()[1].end_fret == std::optional{14});

    editor.controller.onFretHandPositionEndClearRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before + 2);
    CHECK(editor.state().undo_history.labels.back() == "Clear Hand Position End Fret");
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});

    editor.controller.onFretHandPositionEndClearRequested();
    CHECK(editor.undoEntryCount() == entries_before + 2);
    CHECK(editor.placements().size() == 4);
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

// Tab and Shift+Tab walk the position row through every hand event in order, the window measure 6
// derives between the authored 5:1 and 8:1 included.
TEST_CASE("The position row walks derived and authored positions in order", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickPlacement(0);

    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{1});
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedChip() == editor.handChipAt(downbeat(6)));
    CHECK_FALSE(editor.selectedHandIndex().has_value());
    CHECK(editor.transport.position().seconds == Catch::Approx(10.0));
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedChip() == editor.handChipAt(downbeat(6)));
}

// A chart storing no position still has a position row: its objects are the windows the notes
// derive. Up off the top string lands on the window holding the cursor and Down from the time
// signature returns to it; Tab and Shift+Tab step across the derived fronts, inert past either end;
// the jump lands on the holder, the first window holding the lead-in.
TEST_CASE(
    "The position row walks the derived windows of a chart storing none", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeDerivedHandChart()};
    REQUIRE(editor.placements().empty());
    const std::shared_ptr<const common::core::ChartViewState> tab = editor.state().tab;
    REQUIRE(tab != nullptr);
    REQUIRE(tab->fret_hand_positions.size() == 3);
    for (const common::core::FhpViewState& chip : tab->fret_hand_positions)
    {
        CHECK(chip.authored == common::core::FhpAuthored::Nothing);
    }
    const std::optional<std::size_t> first = editor.handChipAt(downbeat(2));
    const std::optional<std::size_t> second = editor.handChipAt(downbeat(3));
    const std::optional<std::size_t> third = editor.handChipAt(downbeat(4));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(third.has_value());

    SECTION("Up off the top string and Down from the time signature land on the holder")
    {
        editor.armAtMeasure(3);
        for (int press = 0; press < 5; ++press)
        {
            editor.step(ChartStepDirection::Up);
        }
        REQUIRE(editor.caretString() == std::optional{6});

        editor.step(ChartStepDirection::Up);
        CHECK(editor.selectedChip() == second);
        CHECK_FALSE(editor.caretString().has_value());
        CHECK(editor.transport.position().seconds == Catch::Approx(4.0));

        editor.step(ChartStepDirection::Up);
        CHECK(editor.state().selected_time_signature_measure == std::optional{1});
        CHECK_FALSE(editor.selectedChip().has_value());
        editor.step(ChartStepDirection::Down);
        CHECK(editor.selectedChip() == second);
        editor.step(ChartStepDirection::Down);
        CHECK(editor.caretString() == std::optional{6});
    }

    SECTION("Tab and Shift+Tab step across the derived fronts")
    {
        editor.clickChipAt(downbeat(3));
        REQUIRE(editor.selectedChip() == second);

        editor.controller.onRowObjectStepRequested(true, false);
        CHECK(editor.selectedChip() == third);
        CHECK(editor.transport.position().seconds == Catch::Approx(6.0));
        editor.controller.onRowObjectStepRequested(true, false);
        CHECK(editor.selectedChip() == third);

        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedChip() == second);
        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedChip() == first);
        CHECK(editor.transport.position().seconds == Catch::Approx(2.0));
        editor.controller.onRowObjectStepRequested(false, false);
        CHECK(editor.selectedChip() == first);
    }

    SECTION("the jump lands on the window holding the caret")
    {
        editor.controller.onTimelineSeekRequested(common::core::TimePosition{5.0});
        editor.step(ChartStepDirection::Right);
        REQUIRE(editor.caretString() == std::optional{1});

        editor.jump(FocusRowJump::FretHandPosition);
        CHECK(editor.selectedChip() == second);
        CHECK_FALSE(editor.caretString().has_value());
        CHECK(editor.transport.position().seconds == Catch::Approx(5.0));
    }

    SECTION("the first window holds the lead-in")
    {
        editor.armAtMeasure(1);
        editor.jump(FocusRowJump::FretHandPosition);
        CHECK(editor.selectedChip() == first);
        CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    }
}

// A stored position pinning a front the notes already derive is one row object, not two: the chips
// stand exactly where the notes alone put them, the pinned one drawn authored, and the row's stops
// visit it once. Restating the derived fret and stating another one both leave a single chip there.
TEST_CASE("A stored position on a derived front is one row object", "[core][marker-rows]")
{
    const MarkerRowEditor derived{makeMarkerSections(), {}, makeDerivedHandChart()};
    const std::shared_ptr<const common::core::ChartViewState> derived_tab = derived.state().tab;
    REQUIRE(derived_tab != nullptr);
    const std::vector<common::core::FhpViewState>& derived_chips = derived_tab->fret_hand_positions;
    REQUIRE(derived_chips.size() == 3);
    REQUIRE(derived_chips[1].position == downbeat(3));
    REQUIRE(derived_chips[1].fret == 8);

    for (const int stated_fret : {8, 7})
    {
        common::core::Chart chart = makeDerivedHandChart();
        chart.fret_hand_positions = {placementAt(downbeat(3), stated_fret)};
        MarkerRowEditor pinned{makeMarkerSections(), {}, std::move(chart)};
        const std::shared_ptr<const common::core::ChartViewState> tab = pinned.state().tab;
        REQUIRE(tab != nullptr);
        const std::vector<common::core::FhpViewState>& chips = tab->fret_hand_positions;
        REQUIRE(chips.size() == derived_chips.size());
        for (std::size_t index = 0; index < chips.size(); ++index)
        {
            CHECK(chips[index].position == derived_chips[index].position);
        }
        CHECK(chips[0].authored == common::core::FhpAuthored::Nothing);
        CHECK(chips[1].authored == common::core::FhpAuthored::Fret);
        CHECK(chips[1].fret == stated_fret);
        CHECK(chips[2].authored == common::core::FhpAuthored::Nothing);

        // From the first window's own front, since a Tab from the lead-in lands there first.
        pinned.parkAtMeasure(2);
        pinned.clickChipAt(downbeat(2));
        pinned.controller.onRowObjectStepRequested(true, false);
        CHECK(pinned.selectedChip() == std::optional<std::size_t>{1});
        CHECK(pinned.selectedHandIndex() == std::optional<std::size_t>{0});
        pinned.controller.onRowObjectStepRequested(true, false);
        CHECK(pinned.selectedChip() == std::optional<std::size_t>{2});
        pinned.controller.onRowObjectStepRequested(false, false);
        CHECK(pinned.selectedChip() == std::optional<std::size_t>{1});
        pinned.controller.onRowObjectStepRequested(false, false);
        CHECK(pinned.selectedChip() == std::optional<std::size_t>{0});
    }
}

// A digit on a selected DERIVED position states its fret the way it states an authored one's: the
// position is authored there first, as Ctrl+P would make it, and the typed fret applies to it, the
// two one undo entry. The chip stays selected, now drawn solid.
TEST_CASE("A digit on a derived position authors it there", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickChipAt(downbeat(6));
    REQUIRE(editor.chipAuthored(downbeat(6)) == common::core::FhpAuthored::Nothing);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onChartFretDigitTyped(9);
    std::vector<common::core::FretHandPosition> expected = makeHandChart().fret_hand_positions;
    expected.insert(std::next(expected.begin(), 2), placementAt(downbeat(6), 9));
    CHECK(editor.placements() == expected);
    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK(editor.state().undo_history.labels.back() == "Set Hand Position Start Fret");
    CHECK(editor.chipAuthored(downbeat(6)) == common::core::FhpAuthored::Fret);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.chipAuthored(downbeat(6)) == common::core::FhpAuthored::Nothing);
}

// The ring plane's end fret states on a derived position too: it authors the position there first
// with the fret standing there (the window measure 6 derives sits on fret 7), as one undo entry.
TEST_CASE("An end fret typed on a derived position authors it there", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickChipAt(downbeat(6));
    const std::size_t entries_before = editor.undoEntryCount();
    std::vector<common::core::FretHandPosition> expected = makeHandChart().fret_hand_positions;

    editor.controller.onChartRingDigitTyped(1);
    editor.controller.onChartRingDigitTyped(1);
    common::core::FretHandPosition stated = placementAt(downbeat(6), 7);
    stated.end_fret = 11;
    expected.insert(std::next(expected.begin(), 2), stated);
    CHECK(editor.placements() == expected);
    CHECK(editor.state().undo_history.labels.back() == "Set Hand Position End Fret");
    CHECK(editor.chipAuthored(downbeat(6)) == common::core::FhpAuthored::FretAndEnd);

    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{2});
    editor.controller.onUndoRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
}

// Alt+Delete clears a stated end, and a derived position states none: nothing is authored, nothing
// is recorded, and the chip stays derived and selected.
TEST_CASE("Alt+Delete on a derived position does nothing", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    editor.clickChipAt(downbeat(6));
    const std::size_t entries_before = editor.undoEntryCount();
    const std::optional<std::size_t> selected_before = editor.selectedChip();
    REQUIRE(selected_before.has_value());

    editor.controller.onFretHandPositionEndClearRequested();

    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.chipAuthored(downbeat(6)) == common::core::FhpAuthored::Nothing);
    CHECK(editor.selectedChip() == selected_before);
}

// A derived position stores nothing to delete or move, so Delete and Alt+arrows REFUSE it: the
// stream, the history and the selection stay exactly as they were, and the chip glows red.
TEST_CASE("Delete and the move refuse a derived position", "[core][marker-rows]")
{
    MarkerRowEditor editor{makeMarkerSections(), {}, makeHandChart()};
    const std::optional<std::size_t> derived = editor.handChipAt(downbeat(6));
    REQUIRE(derived.has_value());
    editor.clickChipAt(downbeat(6));
    const std::size_t entries_before = editor.undoEntryCount();
    const std::size_t flashes_before = editor.view.refusal_flashes.size();

    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.selectedChip() == derived);
    REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 1);
    CHECK(editor.view.refusal_flashes.back().fret_hand_position == derived);

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.placements() == makeHandChart().fret_hand_positions);
    CHECK(editor.undoEntryCount() == entries_before);
    CHECK(editor.selectedChip() == derived);
    REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 2);
    CHECK(editor.view.refusal_flashes.back().fret_hand_position == derived);
}

// The prune drops every stored position the derivation reproduces and keeps the rest, as one undo
// entry; with nothing left to drop it records nothing. Measure 2's position states the very window
// its notes derive, so it goes; measure 4's states an end fret 5 over a note the notes alone would
// give the narrowest window from fret 1, so it stays.
TEST_CASE("The prune removes exactly the redundant stored positions", "[core][marker-rows]")
{
    common::core::Chart chart = makeBareChart();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 3, .beat = 1}, 1, 4),
        makeTestNote({.measure = 4, .beat = 1}, 2, 1),
    };
    const common::core::FretHandPosition kept{.position = downbeat(4), .fret = 1, .end_fret = 5};
    chart.fret_hand_positions = {placementAt(downbeat(2), 3), kept};
    const std::vector<common::core::FretHandPosition> stored = chart.fret_hand_positions;
    MarkerRowEditor editor{makeMarkerSections(), {}, std::move(chart)};
    const std::shared_ptr<const common::core::ChartViewState> before = editor.state().tab;
    REQUIRE(before != nullptr);
    const std::size_t entries_before = editor.undoEntryCount();

    editor.controller.onFretHandPositionsPruneRequested();
    CHECK(editor.placements() == std::vector{kept});
    CHECK(editor.undoEntryCount() == entries_before + 1);
    CHECK(editor.state().undo_history.labels.back() == "Prune Hand Positions");
    // The hand is unchanged; the pruned position's chip simply draws derived now.
    const std::shared_ptr<const common::core::ChartViewState> after = editor.state().tab;
    REQUIRE(after != nullptr);
    REQUIRE(after->fret_hand_positions.size() == before->fret_hand_positions.size());
    for (std::size_t index = 0; index < before->fret_hand_positions.size(); ++index)
    {
        CHECK(
            after->fret_hand_positions[index].position ==
            before->fret_hand_positions[index].position);
        CHECK(after->fret_hand_positions[index].fret == before->fret_hand_positions[index].fret);
        CHECK(after->fret_hand_positions[index].width == before->fret_hand_positions[index].width);
    }
    CHECK(editor.chipAuthored(downbeat(2)) == common::core::FhpAuthored::Nothing);
    CHECK(editor.chipAuthored(downbeat(4)) == common::core::FhpAuthored::FretAndEnd);

    editor.controller.onFretHandPositionsPruneRequested();
    CHECK(editor.placements() == std::vector{kept});
    CHECK(editor.undoEntryCount() == entries_before + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.placements() == stored);
}

namespace
{

// The coverage chart: one fret-3 note on string 1 ringing from 2:1 to 3:1, and one placement on
// fret 5 at 3:1, which the ring reaches exactly and therefore does not grip.
[[nodiscard]] common::core::Chart makeCoverageChart()
{
    common::core::Chart chart = makeBareChart();
    chart.notes = {makeTestNote(downbeat(2), 1, 3, common::core::Fraction{1})};
    chart.fret_hand_positions = {placementAt(downbeat(3), 5)};
    return chart;
}

} // namespace

// AN AUTHORED POSITION COVERS EVERY FRETTED NOTE GRIPPED AT ITS INSTANT, asked by the placement
// commit: a move into the ring, or a retyped fret above the note struck there, is refused whole and
// the chip glows red; a move that keeps every gripped note covered lands.
TEST_CASE("A fret-hand position refuses leaving a gripped note uncovered", "[core][marker-rows]")
{
    SECTION("a move into the ring")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeCoverageChart()};
        editor.clickPlacement(0);
        const std::optional<std::size_t> chip = editor.selectedChip();
        const std::size_t entries_before = editor.undoEntryCount();
        const std::size_t flashes_before = editor.view.refusal_flashes.size();

        editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
        CHECK(editor.placements() == makeCoverageChart().fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
        CHECK(editor.selectedHandIndex() == std::optional<std::size_t>{0});
        REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 1);
        CHECK(editor.view.refusal_flashes.back().fret_hand_position == chip);
    }

    SECTION("a fret above the note struck at its instant")
    {
        common::core::Chart chart = makeCoverageChart();
        chart.fret_hand_positions = {placementAt(downbeat(2), 3)};
        MarkerRowEditor editor{makeMarkerSections(), {}, chart};
        editor.clickPlacement(0);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onChartFretDigitTyped(4);
        const std::optional<ChartPendingFretViewState>& pending =
            editor.state().chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK_FALSE(pending->valid);
        }
        CHECK(editor.placements() == chart.fret_hand_positions);
        CHECK(editor.undoEntryCount() == entries_before);
    }

    SECTION("a move that leaves the ring alone")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeCoverageChart()};
        editor.clickPlacement(0);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
        CHECK(
            editor.placements() ==
            std::vector{placementAt(GridPosition{.measure = 3, .beat = 2}, 5)});
        CHECK(editor.undoEntryCount() == entries_before + 1);
    }
}

// The same rule asked by the note commit: a note struck at a placement's instant below its fret,
// or a ring lengthened through it, is refused whole and the note glows red; a note the placement
// covers lands.
TEST_CASE("A note edit refuses leaving an authored position uncovered", "[core][marker-rows]")
{
    SECTION("a note entered below the window")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeCoverageChart()};
        editor.armAtMeasure(3);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onChartFretDigitTyped(3);
        CHECK(editor.notes() == makeCoverageChart().notes);
        CHECK(editor.undoEntryCount() == entries_before);
    }

    SECTION("a ring lengthened through the position")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeCoverageChart()};
        // The note's head: measure 2 on string 1 (makeGeometry).
        click(editor.controller, 40.0f, 220.0f);
        REQUIRE(editor.state().chart_edit.selected_notes == std::vector<std::size_t>{0});
        const std::size_t entries_before = editor.undoEntryCount();
        const std::size_t flashes_before = editor.view.refusal_flashes.size();

        editor.controller.onChartSustainAdjustRequested(1);
        CHECK(editor.notes() == makeCoverageChart().notes);
        CHECK(editor.undoEntryCount() == entries_before);
        REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 1);
        CHECK(editor.view.refusal_flashes.back().notes == std::vector<std::size_t>{0});
    }

    SECTION("a note the window covers")
    {
        MarkerRowEditor editor{makeMarkerSections(), {}, makeCoverageChart()};
        editor.armAtMeasure(3);
        const std::size_t entries_before = editor.undoEntryCount();

        editor.controller.onChartFretDigitTyped(7);
        CHECK(editor.notes().size() == 2);
        CHECK(editor.undoEntryCount() == entries_before + 1);
    }
}

} // namespace rock_hero::editor::core
