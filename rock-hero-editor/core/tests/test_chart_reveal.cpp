#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/editor/core/chart/chart_reveal.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The projection's notes in onset order. The one under test, index 2, is on string 3 and its ink
// stops well before its stored ring does, so a caret between the two ends tells the stored ring
// apart from the drawn one. Its neighbours stand on other strings, outside its ring.
[[nodiscard]] std::vector<common::core::NoteViewState> makeNotes()
{
    const auto note =
        [](const double start, const double ring_end, const double ink_end, const int string) {
            return common::core::NoteViewState{
                .start_seconds = start,
                .ring_end_seconds = ring_end,
                .ink_end_seconds = ink_end,
                .string = string,
                .fret = 5,
                .bend = {},
                .slides = {},
                .keyframes = {},
                .vibrato = {},
            };
        };
    return {
        note(0.0, 0.5, 0.5, 1),
        note(1.0, 1.5, 1.5, 2),
        note(2.0, 6.0, 3.0, 3),
        note(7.0, 7.5, 7.5, 4),
    };
}

// A ring on string 3 from 1.0s ending at 4.0s on the next head of its string, which rings to 6.0s,
// with a chord tone on string 2 struck beside that head.
[[nodiscard]] std::vector<common::core::NoteViewState> makeSeam()
{
    const auto note = [](const double start,
                         const double ring_end,
                         const int string,
                         const std::optional<std::size_t>
                             end_head) {
        return common::core::NoteViewState{
            .start_seconds = start,
            .ring_end_seconds = ring_end,
            .ink_end_seconds = ring_end,
            .string = string,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
            .end_head = end_head,
        };
    };
    return {
        note(1.0, 4.0, 3, 1),
        note(4.0, 6.0, 3, std::nullopt),
        note(4.0, 5.0, 2, std::nullopt),
    };
}

// Whether the note at `index` is revealed, read off the one presence answer.
[[nodiscard]] bool revealed(
    const std::vector<common::core::NoteViewState>& notes, const std::size_t index,
    const bool lane_reveal, const ChartEditViewState& edit)
{
    return chartPresence(notes, lane_reveal, edit)[index].revealing();
}

// Which notes step back, ascending.
[[nodiscard]] std::vector<std::size_t> receded(
    const std::vector<common::core::NoteViewState>& notes, const bool lane_reveal,
    const ChartEditViewState& edit)
{
    const std::vector<common::ui::TabNotePresence> presence =
        chartPresence(notes, lane_reveal, edit);
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < presence.size(); ++index)
    {
        if (presence[index].receded())
        {
            indices.push_back(index);
        }
    }
    return indices;
}

// A span over [2, 6) whose furniture is drawn only to 5.
[[nodiscard]] common::core::ShapeViewState makeSpan()
{
    return common::core::ShapeViewState{
        .start_seconds = 2.0,
        .drawn_end_seconds = 5.0,
        .close_seconds = 6.0,
        .arpeggio = false,
        .strings = {},
    };
}

} // namespace

// A NOTE'S REVEAL HAS THREE GROUNDS, and any one is enough: the lane reveal held over the whole
// lane; the note being under scrutiny — selected itself, or through one of its keyframes; and the
// caret peek — the caret on the note's string inside its STORED ring, ends included and the end
// widened by the rounding tolerance. Anything else leaves it cropped, and a selection of ANOTHER
// note's keyframe is anything else: the answer is per note, so one selected ring never uncrops its
// neighbours.
TEST_CASE(
    "Chart note reveal answers per note on the lane, selection and caret grounds", "[core][chart]")
{
    const std::vector<common::core::NoteViewState> notes = makeNotes();
    ChartEditViewState edit;

    SECTION("no ground leaves the note cropped")
    {
        CHECK_FALSE(revealed(notes, 2, false, edit));
    }

    SECTION("the lane reveal reveals every note")
    {
        for (std::size_t index = 0; index < notes.size(); ++index)
        {
            CHECK(revealed(notes, index, true, edit));
        }
    }

    SECTION("a selected note is revealed, and only that note")
    {
        edit.selected_notes = {0, 2, 3};
        CHECK(revealed(notes, 2, false, edit));
        CHECK(revealed(notes, 3, false, edit));
        CHECK_FALSE(revealed(notes, 1, false, edit));
    }

    SECTION("a selected keyframe reveals the note it belongs to")
    {
        edit.selected_keyframes = {ChartKeyframeRef{.note_index = 2, .keyframe_index = 1}};
        CHECK(revealed(notes, 2, false, edit));
    }

    SECTION("a selected keyframe of another note reveals nothing here")
    {
        edit.selected_keyframes = {ChartKeyframeRef{.note_index = 3, .keyframe_index = 0}};
        CHECK_FALSE(revealed(notes, 2, false, edit));
    }

    // The peek reads the STORED ring, not the drawn one: a caret past the ink end but inside the
    // ring is exactly the position whose truth the crop hides.
    SECTION("the caret past the ink end but inside the ring reveals the note")
    {
        edit.caret = ChartCaretViewState{.seconds = 4.5, .string = 3};
        CHECK(revealed(notes, 2, false, edit));
    }

    SECTION("the caret at the onset or exactly at the ring end reveals the note, ends included")
    {
        edit.caret = ChartCaretViewState{.seconds = 2.0, .string = 3};
        CHECK(revealed(notes, 2, false, edit));
        edit.caret = ChartCaretViewState{.seconds = 6.0, .string = 3};
        CHECK(revealed(notes, 2, false, edit));
    }

    // A last-bit slip, well inside common::core::g_onset_match_epsilon: the ring end is reached by
    // a second arithmetic path from the same instant, so the caret must not fall off it.
    SECTION("the caret a rounding slip past the ring end still reveals the note")
    {
        edit.caret = ChartCaretViewState{.seconds = 6.0 + 1.0e-12, .string = 3};
        CHECK(revealed(notes, 2, false, edit));
    }

    SECTION("the caret on another string reveals nothing here")
    {
        edit.caret = ChartCaretViewState{.seconds = 4.5, .string = 2};
        CHECK_FALSE(revealed(notes, 2, false, edit));
    }

    SECTION("the caret before the onset or past the ring end reveals nothing")
    {
        edit.caret = ChartCaretViewState{.seconds = 1.5, .string = 3};
        CHECK_FALSE(revealed(notes, 2, false, edit));
        edit.caret = ChartCaretViewState{.seconds = 7.0, .string = 3};
        CHECK_FALSE(revealed(notes, 2, false, edit));
    }
}

// A SPAN'S REVEAL reads the same three grounds for lane furniture: the lane reveal; a selected note
// whose onset stands inside the span; and the caret anywhere in the span's tenure, its string
// ignored.
TEST_CASE("Chart span reveal answers on the lane, selection and caret grounds", "[core][chart]")
{
    const common::core::ShapeViewState span = makeSpan();
    const std::vector<common::core::NoteViewState> notes = makeNotes();
    ChartEditViewState edit;

    SECTION("no ground leaves the span at its drawn extent")
    {
        CHECK_FALSE(chartSpanRevealed(span, false, notes, edit));
    }

    SECTION("the lane reveal reveals the span")
    {
        CHECK(chartSpanRevealed(span, true, notes, edit));
    }

    SECTION("a selected note inside the span reveals it")
    {
        edit.selected_notes = {2};
        CHECK(chartSpanRevealed(span, false, notes, edit));
    }

    SECTION("the caret inside the span reveals it on any string")
    {
        edit.caret = ChartCaretViewState{.seconds = 5.5, .string = 1};
        CHECK(chartSpanRevealed(span, false, notes, edit));
    }

    // The caret is a position, not a member, so the close is inside it; the close is reached by
    // adding the extent to the start, so a last-bit slip past it must still count.
    SECTION("the caret at the close or a rounding slip past it reveals the span")
    {
        edit.caret = ChartCaretViewState{.seconds = 6.0, .string = 1};
        CHECK(chartSpanRevealed(span, false, notes, edit));
        edit.caret = ChartCaretViewState{.seconds = 6.0 + 1.0e-12, .string = 1};
        CHECK(chartSpanRevealed(span, false, notes, edit));
    }

    SECTION("the caret past the close reveals nothing")
    {
        edit.caret = ChartCaretViewState{.seconds = 7.0, .string = 1};
        CHECK_FALSE(chartSpanRevealed(span, false, notes, edit));
    }
}

// A caret ON A HEAD is on that head's note, not on the ring ending under it: where a ring ends on
// the next head of its string, the caret at the seam reveals only the note struck there.
TEST_CASE("Chart note reveal gives the seam to the head a ring ends on", "[core][chart]")
{
    const std::vector<common::core::NoteViewState> notes = makeSeam();
    ChartEditViewState edit;

    edit.caret = ChartCaretViewState{.seconds = 4.0, .string = 3};
    CHECK_FALSE(revealed(notes, 0, false, edit));
    CHECK(revealed(notes, 1, false, edit));

    edit.caret = ChartCaretViewState{.seconds = 3.5, .string = 3};
    CHECK(revealed(notes, 0, false, edit));
    CHECK_FALSE(revealed(notes, 1, false, edit));
}

// THE HEAD A FOCUSED RING ENDS ON STEPS BACK. Focus is the selection or the caret, never the lane
// reveal, and a focused note never steps back, so selecting both notes of the seam steps neither.
TEST_CASE("Chart presence steps back the head a focused ring ends on", "[core][chart]")
{
    const std::vector<common::core::NoteViewState> notes = makeSeam();
    ChartEditViewState edit;

    SECTION("the caret inside the ring steps its end head back, and only that head")
    {
        edit.caret = ChartCaretViewState{.seconds = 3.0, .string = 3};
        CHECK(receded(notes, false, edit) == std::vector<std::size_t>{1});
        const std::vector<common::ui::TabNotePresence> presence = chartPresence(notes, false, edit);
        CHECK_THAT(presence[0].reveal, Catch::Matchers::WithinULP(1.0f, 0));
        CHECK_THAT(presence[1].reveal, Catch::Matchers::WithinULP(0.0f, 0));
        CHECK_THAT(presence[1].recede, Catch::Matchers::WithinULP(1.0f, 0));
    }

    SECTION("a selected keyframe of the ring steps its end head back")
    {
        edit.selected_keyframes = {ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}};
        CHECK(receded(notes, false, edit) == std::vector<std::size_t>{1});
    }

    SECTION("the caret on the head steps nothing back")
    {
        edit.caret = ChartCaretViewState{.seconds = 4.0, .string = 3};
        CHECK(receded(notes, false, edit).empty());
    }

    SECTION("a focused head never steps back")
    {
        edit.selected_notes = {0, 1};
        CHECK(receded(notes, false, edit).empty());
    }

    SECTION("the lane reveal steps nothing back, and leaves a focused ring's step in place")
    {
        CHECK(receded(notes, true, edit).empty());
        edit.selected_notes = {0};
        CHECK(receded(notes, true, edit) == std::vector<std::size_t>{1});
    }
}

} // namespace rock_hero::editor::core
