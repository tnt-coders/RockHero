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
#include <variant>
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

// The same with a third section, so a step from the MIDDLE one has a neighbour either way and a
// step that read the cursor instead of the selection would land somewhere else.
[[nodiscard]] std::vector<SongSection> makeThreeMarkerSections()
{
    std::vector<SongSection> sections = makeMarkerSections();
    sections.push_back(SongSection{.position = downbeat(11), .name = "Bridge"});
    return sections;
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
    // The focus anchor stands on the selected chip for the reveal: the signature change at
    // measure 5 (8.0s) holds the cursor's measure 6, and its chip is what a verb reveals.
    const std::optional<double>& anchor = editor.state().focus_anchor_seconds;
    REQUIRE(anchor.has_value());
    if (anchor.has_value())
    {
        CHECK(*anchor == Catch::Approx(8.0));
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
    // Back on a string the anchor is the caret's slot.
    const std::optional<double>& landed = editor.state().focus_anchor_seconds;
    REQUIRE(landed.has_value());
    if (landed.has_value())
    {
        CHECK(*landed == Catch::Approx(10.0));
    }
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
// The focus anchor names the marker's START regardless — the chip is what a keyboard verb on the
// selection reveals — and stepping off the marker brings the cursor there first, so the next row's
// holder is the one found at that marker; its start is the anchor then.
TEST_CASE("EditorController steps off a clicked chip from its start", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    const int seeks_before = editor.transport.seek_call_count;
    editor.controller.onSongSectionSelected(downbeat(7));
    CHECK(editor.transport.seek_call_count == seeks_before);
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{1});
    const std::optional<double> clicked = editor.state().focus_anchor_seconds;
    REQUIRE(clicked.has_value());

    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(12.0));
    CHECK(editor.state().selected_tempo_anchor == std::optional{downbeat(5)});
    const std::optional<double> moved = editor.state().focus_anchor_seconds;
    REQUIRE(moved.has_value());
    if (clicked.has_value() && moved.has_value())
    {
        CHECK(*clicked == Catch::Approx(12.0));
        // The tempo anchor holding 12.0s starts at measure 5, and the chip is what is revealed.
        CHECK(*moved == Catch::Approx(8.0));
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
    // Passive with nothing selected, the keyboard stands nowhere the view need reveal.
    CHECK_FALSE(editor.state().focus_anchor_seconds.has_value());
}

// The anchor names the CHIP for a row's first marker too. The walk lets that marker own the
// lead-in, but a cursor in the lead-in is not standing in the section, and revealing it there
// would show neither the chip nor its name.
TEST_CASE(
    "EditorController anchors a first chip clicked from the lead-in at the chip",
    "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.controller.onSongSectionSelected(downbeat(3));
    REQUIRE(editor.selectedSectionIndex() == std::optional<std::size_t>{0});
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
    const std::optional<double>& anchor = editor.state().focus_anchor_seconds;
    REQUIRE(anchor.has_value());
    if (anchor.has_value())
    {
        CHECK(*anchor == Catch::Approx(4.0));
    }

    // Stepping off it still keeps the cursor in the lead-in: the walk's holder rule is unchanged.
    editor.step(ChartStepDirection::Down);
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
}

// While the transport plays, playback follow owns the view, so no focus anchor is published for
// the keyboard to reveal.
TEST_CASE("EditorController publishes no focus anchor while playing", "[core][marker-rows]")
{
    MarkerRowEditor editor;
    editor.controller.onSongSectionSelected(downbeat(3));
    REQUIRE(editor.state().focus_anchor_seconds.has_value());

    // The fake transport never flips its own state; the press is only what publishes a new view.
    editor.transport.current_state.playing = true;
    editor.controller.onPlayPausePressed();
    CHECK_FALSE(editor.state().focus_anchor_seconds.has_value());
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
    // Nothing selected, no caret: nothing stands for the view to reveal.
    CHECK_FALSE(editor.state().focus_anchor_seconds.has_value());
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

} // namespace rock_hero::editor::core
