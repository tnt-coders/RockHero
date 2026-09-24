#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <rock_hero/editor/core/chart/chart_reveal.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

// A NOTE'S REVEAL HAS TWO GROUNDS, and either is enough: the lane reveal held over the whole lane,
// and the note being under scrutiny — selected itself, or through one of its keyframes. Anything
// else leaves it cropped, and a selection of ANOTHER note's keyframe is anything else: the answer
// is per note, so one selected ring never uncrops its neighbours.
TEST_CASE("Chart note reveal answers per note on the lane and selection grounds", "[core][chart]")
{
    const std::vector<std::size_t> no_notes;
    const std::vector<ChartKeyframeRef> no_keyframes;

    SECTION("no ground leaves the note cropped")
    {
        CHECK_FALSE(chartNoteRevealed(2, false, no_notes, no_keyframes));
    }

    SECTION("the lane reveal reveals every note")
    {
        CHECK(chartNoteRevealed(2, true, no_notes, no_keyframes));
        CHECK(chartNoteRevealed(7, true, no_notes, no_keyframes));
    }

    SECTION("a selected note is revealed, and only that note")
    {
        const std::vector<std::size_t> selected{1, 2, 5};
        CHECK(chartNoteRevealed(2, false, selected, no_keyframes));
        CHECK(chartNoteRevealed(5, false, selected, no_keyframes));
        CHECK_FALSE(chartNoteRevealed(3, false, selected, no_keyframes));
    }

    SECTION("a selected keyframe reveals the note it belongs to")
    {
        const std::vector<ChartKeyframeRef> selected{
            ChartKeyframeRef{.note_index = 2, .keyframe_index = 1}
        };
        CHECK(chartNoteRevealed(2, false, no_notes, selected));
    }

    SECTION("a selected keyframe of another note reveals nothing here")
    {
        const std::vector<ChartKeyframeRef> selected{
            ChartKeyframeRef{.note_index = 4, .keyframe_index = 0}
        };
        CHECK_FALSE(chartNoteRevealed(2, false, no_notes, selected));
    }
}

} // namespace rock_hero::editor::core
