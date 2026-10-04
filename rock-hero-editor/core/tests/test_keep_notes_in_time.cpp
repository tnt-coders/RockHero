#include "timeline/keep_notes_in_time.h"
#include "timeline/tempo_map_edits.h"
#include "timeline/timeline_readdress.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::testing::anchor;
using common::core::testing::commonTimeMap;
using testing::addressesErased;
using testing::at;
using testing::chartOf;
using testing::checkTimesHeld;
using testing::Dice;
using testing::makeNote;
using testing::oneNoteTimeline;
using testing::randomTimeline;
using testing::requireValid;

// One of the three tempo verbs that change a time, rolled against a map with at least two spans
// where it needs an interior anchor: a move between its neighbours, a typed tempo that keeps the
// terminal covering the old end, or a delete.
[[nodiscard]] TempoMapEditResult rollTempoEdit(Dice& dice, const TempoMap& tempo_map, int verb)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    const auto interior = [&dice, &anchors] {
        return static_cast<std::size_t>(dice.roll(1, static_cast<int>(anchors.size()) - 2));
    };
    if (verb == 0)
    {
        const std::size_t index = interior();
        return moveTempoAnchor(
            tempo_map,
            anchors[index].position,
            dice.roll(
                static_cast<int>(anchors[index - 1].milliseconds) + 1,
                static_cast<int>(anchors[index + 1].milliseconds) - 1));
    }
    if (verb == 1)
    {
        const auto span =
            static_cast<std::size_t>(dice.roll(0, static_cast<int>(anchors.size()) - 2));
        return setTempoSpanBpm(
            tempo_map, anchors[span].position, dice.roll(60, 200), anchors.back().seconds());
    }
    return deleteTempoAnchor(tempo_map, anchors[interior()].position);
}

} // namespace

// Every verb's new map laid under a random song holds every time within half a tick of the new
// grid, writes the verb's map exactly and touches nothing but addresses. Where two places round
// onto one tick the rules refuse the result, as they refuse any edit.
TEST_CASE("Keep Notes in Time holds every time over random songs", "[core][keep-notes-in-time]")
{
    int landed = 0;
    for (std::uint32_t seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);

        // The unchanged map is the identity: a drag released where it began commits nothing.
        const std::expected<SongTimeline, ReaddressError> kept =
            timeKeptTimeline(before, before.tempo_map);
        REQUIRE(kept.has_value());
        CHECK(*kept == before);

        if (before.tempo_map.anchors().size() < 3)
        {
            continue;
        }
        for (int verb = 0; verb < 3; ++verb)
        {
            INFO("verb " << verb);
            const TempoMapEditResult edited = rollTempoEdit(dice, before.tempo_map, verb);
            if (!edited.has_value())
            {
                continue;
            }
            const std::expected<SongTimeline, ReaddressError> held =
                timeKeptTimeline(before, *edited);
            REQUIRE(held.has_value());
            CHECK(held->tempo_map == *edited);
            CHECK(addressesErased(*held) == addressesErased(before));
            if (!common::core::validateSongTimelineRules(*held).has_value())
            {
                continue;
            }
            checkTimesHeld(before, *held, 0.0);
            ++landed;
        }
    }
    // The dice must land most edits, or the property proves less than it claims.
    CHECK(landed > 50);
}

// 4/4 at 120 BPM with anchors on 1:1, 3:1 and 5:1. Pulling 3:1 from 4 s to 3 s plays the first two
// measures in 3 s (375 ms a beat) and the next two in 5 s (625 ms a beat). The note at 2 s is then
// 5 1/3 beats in, on 2:2+1/3, and its two-beat ring, which ended at 3 s, ends on 3:1; the note at
// 6 s is 4 4/5 beats past 3:1, on 4:1+4/5, and its half-beat ring, which ended at 6.25 s, ends on
// 4:2+1/5. Every place lands exactly, so every time holds exactly.
TEST_CASE("Keep Notes in Time re-addresses content where it sounds", "[core][keep-notes-in-time]")
{
    SongTimeline before = oneNoteTimeline(
        commonTimeMap({anchor("1:1", 0), anchor("3:1", 4000), anchor("5:1", 8000)}),
        makeNote(at("2:1"), 1, 5, Fraction{2}, {}));
    REQUIRE(before.arrangements.front().chart.has_value());
    if (before.arrangements.front().chart.has_value())
    {
        before.arrangements.front().chart->notes.push_back(
            makeNote(at("4:1"), 2, 7, Fraction{1, 2}, {}));
    }
    requireValid(before);

    const TempoMapEditResult moved = moveTempoAnchor(before.tempo_map, at("3:1"), 3000);
    REQUIRE(moved.has_value());
    const std::expected<SongTimeline, ReaddressError> after = timeKeptTimeline(before, *moved);
    REQUIRE(after.has_value());
    requireValid(*after);
    const common::core::Chart& chart = chartOf(*after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(
        chart.notes[0].position == GridPosition{.measure = 2, .beat = 2, .offset = Fraction{1, 3}});
    CHECK(chart.notes[0].sustain == Fraction{8, 3});
    CHECK(
        chart.notes[1].position == GridPosition{.measure = 4, .beat = 1, .offset = Fraction{4, 5}});
    CHECK(chart.notes[1].sustain == Fraction{2, 5});
    checkTimesHeld(before, *after, 0.0);
}

} // namespace rock_hero::editor::core
